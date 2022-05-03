/*
gl_image.c - texture uploading and processing
Copyright (C) 2010 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "gl_local.h"
#include "crclib.h"

static gl_texture_t		gl_textures[MAX_TEXTURES];
static uint		gl_numTextures;

static byte    dottexture[8][8] =
{
	  {0,1,1,0,0,0,0,0},
	  {1,1,1,1,0,0,0,0},
	  {1,1,1,1,0,0,0,0},
	  {0,1,1,0,0,0,0,0},
	  {0,0,0,0,0,0,0,0},
	  {0,0,0,0,0,0,0,0},
	  {0,0,0,0,0,0,0,0},
	  {0,0,0,0,0,0,0,0},
};


#define IsLightMap( tex )	( FBitSet(( tex )->flags, TF_ATLAS_PAGE ))

gl_texture_t *R_GetTexture( GLenum texnum )
{
	ASSERT( texnum >= 0 && texnum < MAX_TEXTURES );
	return &gl_textures[texnum];
}

static const char *GL_TargetToString( GLenum target )
{
	switch( target )
	{
	case GL_TEXTURE_1D:
		return "1D";
	case GL_TEXTURE_2D:
		return "2D";
	case GL_TEXTURE_2D_MULTISAMPLE:
		return "2D Multisample";
	case GL_TEXTURE_3D:
		return "3D";
	case GL_TEXTURE_CUBE_MAP_ARB:
		return "Cube";
	case GL_TEXTURE_2D_ARRAY_EXT:
		return "Array";
	case GL_TEXTURE_RECTANGLE_EXT:
		return "Rect";
	}
	return "??";
}

void GL_Bind( GLint tmu, GLenum texnum )
{
	gl_texture_t	*texture;
	GLuint		glTarget;

	// missed or invalid texture?
	if( texnum <= 0 || texnum >= MAX_TEXTURES )
	{
		if( texnum != 0 )
			gEngfuncs.Con_DPrintf( S_ERROR "GL_Bind: invalid texturenum %d\n", texnum );
		texnum = tr.defaultTexture;
	}
	if( tmu != GL_KEEP_UNIT )
		GL_SelectTexture( tmu );
	else tmu = glState.activeTMU;

	texture = &gl_textures[texnum];
	glTarget = texture->target;

	if( glTarget == GL_TEXTURE_2D_ARRAY_EXT )
		glTarget = GL_TEXTURE_2D;

	if( glState.currentTextureTargets[tmu] != glTarget )
	{
		if( glState.currentTextureTargets[tmu] != GL_NONE )
			pglDisable( glState.currentTextureTargets[tmu] );
		
		glState.currentTextureTargets[tmu] = glTarget;

		pglEnable( glState.currentTextureTargets[tmu] );
	}

	if( glState.currentTextures[tmu] == texture->texnum )
		return;

	pglBindTexture( texture->target, texture->texnum );
	glState.currentTextures[tmu] = texture->texnum;
}

void GL_ApplyTextureParams( int texnum )
{
	vec4_t	border = { 0.0f, 0.0f, 0.0f, 1.0f };

	gl_texture_t *tex = &(gl_textures[texnum]);

	if( !glw_state.initialized )
		return;

	Assert( tex != NULL );

	// multisample textures does not support any sampling state changing
	if( FBitSet( tex->flags, TF_MULTISAMPLE ))
		return;

	// set texture filter
	if( FBitSet( tex->flags, TF_DEPTHMAP ))
	{
		if( !FBitSet( tex->flags, TF_NOCOMPARE ))
		{
			pglTexParameteri( tex->target, GL_TEXTURE_COMPARE_MODE_ARB, GL_COMPARE_R_TO_TEXTURE_ARB );
			pglTexParameteri( tex->target, GL_TEXTURE_COMPARE_FUNC_ARB, GL_LEQUAL );
		}

		if( FBitSet( tex->flags, TF_LUMINANCE ))
			pglTexParameteri( tex->target, GL_DEPTH_TEXTURE_MODE_ARB, GL_LUMINANCE );
		else pglTexParameteri( tex->target, GL_DEPTH_TEXTURE_MODE_ARB, GL_INTENSITY );

		if( FBitSet( tex->flags, TF_NEAREST ))
		{
			pglTexParameteri( tex->target, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
			pglTexParameteri( tex->target, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		}
		else
		{
			pglTexParameteri( tex->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
			pglTexParameteri( tex->target, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		}

		// allow max anisotropy as 1.0f on depth textures
		if( GL_Support( GL_ANISOTROPY_EXT ))
			pglTexParameterf( tex->target, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1.0f );
	}
	else if( FBitSet( tex->flags, TF_NOMIPMAP ) || tex->numMips <= 1 )
	{
		if( FBitSet( tex->flags, TF_NEAREST ) || ( IsLightMap( tex ) && gl_lightmap_nearest->value ))
		{
			pglTexParameteri( tex->target, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
			pglTexParameteri( tex->target, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		}
		else
		{
			pglTexParameteri( tex->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
			pglTexParameteri( tex->target, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		}
	}
	else
	{
		if( FBitSet( tex->flags, TF_NEAREST ) || gl_texture_nearest->value )
		{
			pglTexParameteri( tex->target, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST );
			pglTexParameteri( tex->target, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		}
		else
		{
			pglTexParameteri( tex->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
			pglTexParameteri( tex->target, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		}

		// set texture anisotropy if available
		if( GL_Support( GL_ANISOTROPY_EXT ) && ( tex->numMips > 1 ) && !FBitSet( tex->flags, TF_ALPHACONTRAST ))
			pglTexParameterf( tex->target, GL_TEXTURE_MAX_ANISOTROPY_EXT, gl_texture_anisotropy->value );

		// set texture LOD bias if available
		if( GL_Support( GL_TEXTURE_LOD_BIAS ) && ( tex->numMips > 1 ))
			pglTexParameterf( tex->target, GL_TEXTURE_LOD_BIAS_EXT, gl_texture_lodbias->value );
	}

	// check if border is not supported
	if( FBitSet( tex->flags, TF_BORDER ) && !GL_Support( GL_CLAMP_TEXBORDER_EXT ))
	{
		ClearBits( tex->flags, TF_BORDER );
		SetBits( tex->flags, TF_CLAMP );
	}

	// only seamless cubemaps allows wrap 'clamp_to_border"
	if( tex->target == GL_TEXTURE_CUBE_MAP_ARB && !GL_Support( GL_ARB_SEAMLESS_CUBEMAP ) && FBitSet( tex->flags, TF_BORDER ))
		ClearBits( tex->flags, TF_BORDER );

	// set texture wrap
	if( FBitSet( tex->flags, TF_BORDER ))
	{
		pglTexParameteri( tex->target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER );

		if( tex->target != GL_TEXTURE_1D )
			pglTexParameteri( tex->target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER );

		if( tex->target == GL_TEXTURE_3D || tex->target == GL_TEXTURE_CUBE_MAP_ARB )
			pglTexParameteri( tex->target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_BORDER );

		pglTexParameterfv( tex->target, GL_TEXTURE_BORDER_COLOR, border );
	}
	else if( FBitSet( tex->flags, TF_CLAMP ))
	{
		if( GL_Support( GL_CLAMPTOEDGE_EXT ))
		{
			pglTexParameteri( tex->target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );

			if( tex->target != GL_TEXTURE_1D )
				pglTexParameteri( tex->target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );

			if( tex->target == GL_TEXTURE_3D || tex->target == GL_TEXTURE_CUBE_MAP_ARB )
				pglTexParameteri( tex->target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE );
		}
		else
		{
			pglTexParameteri( tex->target, GL_TEXTURE_WRAP_S, GL_CLAMP );

			if( tex->target != GL_TEXTURE_1D )
				pglTexParameteri( tex->target, GL_TEXTURE_WRAP_T, GL_CLAMP );

			if( tex->target == GL_TEXTURE_3D || tex->target == GL_TEXTURE_CUBE_MAP_ARB )
				pglTexParameteri( tex->target, GL_TEXTURE_WRAP_R, GL_CLAMP );
		}
	}
	else
	{
		pglTexParameteri( tex->target, GL_TEXTURE_WRAP_S, GL_REPEAT );

		if( tex->target != GL_TEXTURE_1D )
			pglTexParameteri( tex->target, GL_TEXTURE_WRAP_T, GL_REPEAT );

		if( tex->target == GL_TEXTURE_3D || tex->target == GL_TEXTURE_CUBE_MAP_ARB )
			pglTexParameteri( tex->target, GL_TEXTURE_WRAP_R, GL_REPEAT );
	}
}

static void GL_UpdateTextureParams( int texnum )
{
	gl_texture_t	*tex = &gl_textures[texnum];

	Assert( tex != NULL );

	if( !tex->texnum ) return; // free slot

	GL_Bind( XASH_TEXTURE0, texnum );

	// set texture anisotropy if available
	if( GL_Support( GL_ANISOTROPY_EXT ) && ( tex->numMips > 1 ) && !FBitSet( tex->flags, TF_DEPTHMAP|TF_ALPHACONTRAST ))
		pglTexParameterf( tex->target, GL_TEXTURE_MAX_ANISOTROPY_EXT, gl_texture_anisotropy->value );

	// set texture LOD bias if available
	if( GL_Support( GL_TEXTURE_LOD_BIAS ) && ( tex->numMips > 1 ) && !FBitSet( tex->flags, TF_DEPTHMAP ))
		pglTexParameterf( tex->target, GL_TEXTURE_LOD_BIAS_EXT, gl_texture_lodbias->value );

	if( IsLightMap( tex ))
	{
		if( gl_lightmap_nearest->value )
		{
			pglTexParameteri( tex->target, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
			pglTexParameteri( tex->target, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		}
		else
		{
			pglTexParameteri( tex->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
			pglTexParameteri( tex->target, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		}
	}

	if( tex->numMips <= 1 ) return;

	if( FBitSet( tex->flags, TF_NEAREST ) || gl_texture_nearest->value )
	{
		pglTexParameteri( tex->target, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST );
		pglTexParameteri( tex->target, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	}
	else
	{
		pglTexParameteri( tex->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
		pglTexParameteri( tex->target, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	}
}

void R_SetTextureParameters( void )
{
	int	i;

	if( GL_Support( GL_ANISOTROPY_EXT ))
	{
		if( gl_texture_anisotropy->value > glConfig.max_texture_anisotropy )
			gEngfuncs.Cvar_SetValue( "gl_anisotropy", glConfig.max_texture_anisotropy );
		else if( gl_texture_anisotropy->value < 1.0f )
			gEngfuncs.Cvar_SetValue( "gl_anisotropy", 1.0f );
	}

	if( GL_Support( GL_TEXTURE_LOD_BIAS ))
	{
		if( gl_texture_lodbias->value < -glConfig.max_texture_lod_bias )
			gEngfuncs.Cvar_SetValue( "gl_texture_lodbias", -glConfig.max_texture_lod_bias );
		else if( gl_texture_lodbias->value > glConfig.max_texture_lod_bias )
			gEngfuncs.Cvar_SetValue( "gl_texture_lodbias", glConfig.max_texture_lod_bias );
	}

	ClearBits( gl_texture_anisotropy->flags, FCVAR_CHANGED );
	ClearBits( gl_texture_lodbias->flags, FCVAR_CHANGED );
	ClearBits( gl_texture_nearest->flags, FCVAR_CHANGED );
	ClearBits( gl_lightmap_nearest->flags, FCVAR_CHANGED );

	// change all the existing mipmapped texture objects
	for( i = 0; i < gl_numTextures; i++ )
		GL_UpdateTextureParams( i );
}

static int GL_CalcTextureSamples( int flags )
{
	if( FBitSet( flags, IMAGE_HAS_COLOR ))
		return FBitSet( flags, IMAGE_HAS_ALPHA ) ? 4 : 3;
	return FBitSet( flags, IMAGE_HAS_ALPHA ) ? 2 : 1;
}

static size_t GL_CalcImageSize( pixformat_t format, int width, int height, int depth )
{
	size_t	size = 0;

	// check the depth error
	depth = Q_max( 1, depth );

	switch( format )
	{
	case PF_LUMINANCE:
		size = width * height * depth;
		break;
	case PF_RGB_24:
	case PF_BGR_24:
		size = width * height * depth * 3;
		break;
	case PF_BGRA_32:
	case PF_RGBA_32:
		size = width * height * depth * 4;
		break;
	case PF_DXT1:
		size = (((width + 3) >> 2) * ((height + 3) >> 2) * 8) * depth;
		break;
	case PF_DXT3:
	case PF_DXT5:
	case PF_BC6H_SIGNED:
	case PF_BC6H_UNSIGNED:
	case PF_BC7:
	case PF_ATI2:
		size = (((width + 3) >> 2) * ((height + 3) >> 2) * 16) * depth;
		break;
	}

	return size;
}

static size_t GL_CalcTextureSize( GLenum format, int width, int height, int depth )
{
	size_t	size = 0;

	// check the depth error
	depth = Q_max( 1, depth );

	switch( format )
	{
	case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
	case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
		size = (((width + 3) >> 2) * ((height + 3) >> 2) * 8) * depth;
		break;
	case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
	case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
	case GL_COMPRESSED_RED_GREEN_RGTC2_EXT:
	case GL_COMPRESSED_LUMINANCE_ALPHA_ARB:
	case GL_COMPRESSED_LUMINANCE_ALPHA_3DC_ATI:
	case GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM_ARB:
	case GL_COMPRESSED_RGBA_BPTC_UNORM_ARB:
	case GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT_ARB:
	case GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT_ARB:
		size = (((width + 3) >> 2) * ((height + 3) >> 2) * 16) * depth;
		break;
	case GL_RGBA8:
	case GL_RGBA:
		size = width * height * depth * 4;
		break;
	case GL_RGB8:
	case GL_RGB:
		size = width * height * depth * 3;
		break;
	case GL_RGB5:
		size = (width * height * depth * 3) / 2;
		break;
	case GL_RGBA4:
		size = (width * height * depth * 4) / 2;
		break;
	case GL_INTENSITY:
	case GL_LUMINANCE:
	case GL_INTENSITY8:
	case GL_LUMINANCE8:
		size = (width * height * depth);
		break;
	case GL_LUMINANCE_ALPHA:
	case GL_LUMINANCE8_ALPHA8:
		size = width * height * depth * 2;
		break;
	case GL_R8:
		size = width * height * depth;
		break;
	case GL_RG8:
		size = width * height * depth * 2;
		break;
	case GL_R16:
		size = width * height * depth * 2;
		break;
	case GL_RG16:
		size = width * height * depth * 4;
		break;
	case GL_R16F:
	case GL_LUMINANCE16F_ARB:
		size = width * height * depth * 2;	// half-floats
		break;
	case GL_R32F:
	case GL_LUMINANCE32F_ARB:
		size = width * height * depth * 4;
		break;
	case GL_RG16F:
	case GL_LUMINANCE_ALPHA16F_ARB:
		size = width * height * depth * 4;
		break;
	case GL_RG32F:
	case GL_LUMINANCE_ALPHA32F_ARB:
		size = width * height * depth * 8;
		break;
	case GL_RGB16F_ARB:
		size = width * height * depth * 6;
		break;
	case GL_RGBA16F_ARB:
		size = width * height * depth * 8;
		break;
	case GL_RGB32F_ARB:
		size = width * height * depth * 12;
		break;
	case GL_RGBA32F_ARB:
		size = width * height * depth * 16;
		break;
	case GL_DEPTH_COMPONENT16:
		size = width * height * depth * 2;
		break;
	case GL_DEPTH_COMPONENT24:
		size = width * height * depth * 3;
		break;
	case GL_DEPTH_COMPONENT32F:
		size = width * height * depth * 4;
		break;
	default:
		gEngfuncs.Host_Error( "GL_CalcTextureSize: bad texture internal format (%u)\n", format );
		break;
	}

	return size;
}

static int GL_CalcMipmapCount( gl_texture_t *tex, qboolean haveBuffer )
{
	int	width, height;
	int	mipcount;

	Assert( tex != NULL );

	if( !haveBuffer || tex->target == GL_TEXTURE_3D )
		return 1;

	// generate mip-levels by user request
	if( FBitSet( tex->flags, TF_NOMIPMAP ))
		return 1;

	// mip-maps can't exceeds 16
	for( mipcount = 0; mipcount < 16; mipcount++ )
	{
		width = Q_max( 1, ( tex->width >> mipcount ));
		height = Q_max( 1, ( tex->height >> mipcount ));
		if( width == 1 && height == 1 )
			break;
	}

	return mipcount + 1;
}

static void GL_SetTextureDimensions( gl_texture_t *tex, int width, int height, int depth )
{
	int	maxTextureSize = 0;
	int	maxDepthSize = 1;

	Assert( tex != NULL );

	switch( tex->target )
	{
	case GL_TEXTURE_1D:
	case GL_TEXTURE_2D:
	case GL_TEXTURE_2D_MULTISAMPLE:
		maxTextureSize = glConfig.max_2d_texture_size;
		break;
	case GL_TEXTURE_2D_ARRAY_EXT:
		maxDepthSize = glConfig.max_2d_texture_layers;
		maxTextureSize = glConfig.max_2d_texture_size;
		break;
	case GL_TEXTURE_RECTANGLE_EXT:
		maxTextureSize = glConfig.max_2d_rectangle_size;
		break;
	case GL_TEXTURE_CUBE_MAP_ARB:
		maxTextureSize = glConfig.max_cubemap_size;
		break;
	case GL_TEXTURE_3D:
		maxDepthSize = glConfig.max_3d_texture_size;
		maxTextureSize = glConfig.max_3d_texture_size;
		break;
	default:
		Assert( false );
	}

	// store original sizes
	tex->srcWidth = width;
	tex->srcHeight = height;

	if( !GL_Support( GL_ARB_TEXTURE_NPOT_EXT ))
	{
		int	step = (int)gl_round_down->value;
		int	scaled_width, scaled_height;

		for( scaled_width = 1; scaled_width < width; scaled_width <<= 1 );

		if( step > 0 && width < scaled_width && ( step == 1 || ( scaled_width - width ) > ( scaled_width >> step )))
			scaled_width >>= 1;

		for( scaled_height = 1; scaled_height < height; scaled_height <<= 1 );

		if( step > 0 && height < scaled_height && ( step == 1 || ( scaled_height - height ) > ( scaled_height >> step )))
			scaled_height >>= 1;

		width = scaled_width;
		height = scaled_height;
	}

	if( width > maxTextureSize || height > maxTextureSize || depth > maxDepthSize )
	{
		if( tex->target == GL_TEXTURE_1D )
		{
			while( width > maxTextureSize )
				width >>= 1;
		}
		else if( tex->target == GL_TEXTURE_3D || tex->target == GL_TEXTURE_2D_ARRAY_EXT )
		{
			while( width > maxTextureSize || height > maxTextureSize || depth > maxDepthSize )
			{
				width >>= 1;
				height >>= 1;
				depth >>= 1;
			}
		}
		else // all remaining cases
		{
			while( width > maxTextureSize || height > maxTextureSize )
			{
				width >>= 1;
				height >>= 1;
			}
		}
	}

	// set the texture dimensions
	tex->width = Q_max( 1, width );
	tex->height = Q_max( 1, height );
	tex->depth = Q_max( 1, depth );
}

static void GL_SetTextureTarget( gl_texture_t *tex, rgbdata_t *pic )
{
	Assert( pic != NULL );
	Assert( tex != NULL );

	// correct depth size
	pic->depth = Q_max( 1, pic->depth );
	tex->numMips = 0; // begin counting

	// correct mip count
	pic->numMips = Q_max( 1, pic->numMips );

	// trying to determine texture type
	if( pic->width > 1 && pic->height <= 1 )
		tex->target = GL_TEXTURE_1D;
	else if( FBitSet( pic->flags, IMAGE_CUBEMAP ))
		tex->target = GL_TEXTURE_CUBE_MAP_ARB;
	else if( FBitSet( pic->flags, IMAGE_MULTILAYER ) && pic->depth >= 1 )
		tex->target = GL_TEXTURE_2D_ARRAY_EXT;
	else if( pic->width > 1 && pic->height > 1 && pic->depth > 1 )
		tex->target = GL_TEXTURE_3D;
	else if( FBitSet( tex->flags, TF_RECTANGLE ))
		tex->target = GL_TEXTURE_RECTANGLE_EXT;
	else if( FBitSet(tex->flags, TF_MULTISAMPLE ))
		tex->target = GL_TEXTURE_2D_MULTISAMPLE;
	else tex->target = GL_TEXTURE_2D; // default case

	// check for hardware support
	if(( tex->target == GL_TEXTURE_CUBE_MAP_ARB ) && !GL_Support( GL_TEXTURE_CUBEMAP_EXT ))
		tex->target = GL_NONE;

	if(( tex->target == GL_TEXTURE_RECTANGLE_EXT ) && !GL_Support( GL_TEXTURE_2D_RECT_EXT ))
		tex->target = GL_TEXTURE_2D;	// fallback

	if(( tex->target == GL_TEXTURE_2D_ARRAY_EXT ) && !GL_Support( GL_TEXTURE_ARRAY_EXT ))
		tex->target = GL_NONE;

	if(( tex->target == GL_TEXTURE_3D ) && !GL_Support( GL_TEXTURE_3D_EXT ))
		tex->target = GL_NONE;

	// check if depth textures are not supported
	if( FBitSet( tex->flags, TF_DEPTHMAP ) && !GL_Support( GL_DEPTH_TEXTURE ))
		tex->target = GL_NONE;

	// depth cubemaps only allowed when GL_EXT_gpu_shader4 is supported
	if( tex->target == GL_TEXTURE_CUBE_MAP_ARB && !GL_Support( GL_EXT_GPU_SHADER4 ) && FBitSet( tex->flags, TF_DEPTHMAP ))
		tex->target = GL_NONE;

	if(( tex->target == GL_TEXTURE_2D_MULTISAMPLE ) && !GL_Support( GL_TEXTURE_MULTISAMPLE ))
		tex->target = GL_NONE;
}

static void GL_SetTextureFormat( gl_texture_t *tex, pixformat_t format, int channelMask )
{
	qboolean	haveColor = ( channelMask & IMAGE_HAS_COLOR );
	qboolean	haveAlpha = ( channelMask & IMAGE_HAS_ALPHA );

	Assert( tex != NULL );

	if( ImageDXT( format ))
	{
		switch( format )
		{
		case PF_DXT1: tex->format = GL_COMPRESSED_RGB_S3TC_DXT1_EXT; break;	// never use DXT1 with 1-bit alpha
		case PF_DXT3: tex->format = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT; break;
		case PF_DXT5: tex->format = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT; break;
		case PF_BC6H_SIGNED: tex->format = GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT_ARB; break;
		case PF_BC6H_UNSIGNED: tex->format = GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT_ARB; break;
		case PF_BC7: tex->format = GL_COMPRESSED_RGBA_BPTC_UNORM_ARB; break;
		case PF_ATI2:
			if( glConfig.hardware_type == GLHW_RADEON )
				tex->format = GL_COMPRESSED_LUMINANCE_ALPHA_3DC_ATI;
			else tex->format = GL_COMPRESSED_RED_GREEN_RGTC2_EXT;
			break;
		}
		return;
	}
	else if( FBitSet( tex->flags, TF_DEPTHMAP ))
	{
		if( FBitSet( tex->flags, TF_ARB_16BIT ))
			tex->format = GL_DEPTH_COMPONENT16;
		else if( FBitSet( tex->flags, TF_ARB_FLOAT ) && GL_Support( GL_ARB_DEPTH_FLOAT_EXT ))
			tex->format = GL_DEPTH_COMPONENT32F;
		else tex->format = GL_DEPTH_COMPONENT24;
	}
	else if( FBitSet( tex->flags, TF_ARB_FLOAT|TF_ARB_16BIT ) && GL_Support( GL_ARB_TEXTURE_FLOAT_EXT ))
	{
		if( haveColor && haveAlpha )
		{
			if( FBitSet( tex->flags, TF_ARB_16BIT ) || gpGlobals->desktopBitsPixel == 16 )
				tex->format = GL_RGBA16F_ARB;
			else tex->format = GL_RGBA32F_ARB;
		}
		else if( haveColor )
		{
			if( FBitSet( tex->flags, TF_ARB_16BIT ) || gpGlobals->desktopBitsPixel == 16 )
				tex->format = GL_RGB16F_ARB;
			else tex->format = GL_RGB32F_ARB;
		}
		else if( haveAlpha )
		{
			if( FBitSet( tex->flags, TF_ARB_16BIT ) || gpGlobals->desktopBitsPixel == 16 )
				tex->format = GL_RG16F;
			else tex->format = GL_RG32F;
		}
		else
		{
			if( FBitSet( tex->flags, TF_ARB_16BIT ) || gpGlobals->desktopBitsPixel == 16 )
				tex->format = GL_LUMINANCE16F_ARB;
			else tex->format = GL_LUMINANCE32F_ARB;
		}
	}
	else
	{
		// NOTE: not all the types will be compressed
		int	bits = gpGlobals->desktopBitsPixel;

		switch( GL_CalcTextureSamples( channelMask ))
		{
		case 1:
			if( FBitSet( tex->flags, TF_ALPHACONTRAST ))
				tex->format = GL_INTENSITY8;
			else tex->format = GL_LUMINANCE8;
			break;
		case 2: tex->format = GL_LUMINANCE8_ALPHA8; break;
		case 3:
			switch( bits )
			{
			case 16: tex->format = GL_RGB5; break;
			case 32: tex->format = GL_RGB8; break;
			default: tex->format = GL_RGB; break;
			}
			break;
		case 4:
		default:
			switch( bits )
			{
			case 16: tex->format = GL_RGBA4; break;
			case 32: tex->format = GL_RGBA8; break;
			default: tex->format = GL_RGBA; break;
			}
			break;
		}
	}
}

byte *GL_ResampleTexture( const byte *source, int inWidth, int inHeight, int outWidth, int outHeight, qboolean isNormalMap )
{
	uint		frac, fracStep;
	uint		*in = (uint *)source;
	uint		p1[0x1000], p2[0x1000];
	byte		*pix1, *pix2, *pix3, *pix4;
	uint		*out, *inRow1, *inRow2;
	static byte	*scaledImage = NULL;	// pointer to a scaled image
	vec3_t		normal;
	int		i, x, y;

	if( !source ) return NULL;

	scaledImage = Mem_Realloc( r_temppool, scaledImage, outWidth * outHeight * 4 );
	fracStep = inWidth * 0x10000 / outWidth;
	out = (uint *)scaledImage;

	frac = fracStep >> 2;
	for( i = 0; i < outWidth; i++ )
	{
		p1[i] = 4 * (frac >> 16);
		frac += fracStep;
	}

	frac = (fracStep >> 2) * 3;
	for( i = 0; i < outWidth; i++ )
	{
		p2[i] = 4 * (frac >> 16);
		frac += fracStep;
	}

	if( isNormalMap )
	{
		for( y = 0; y < outHeight; y++, out += outWidth )
		{
			inRow1 = in + inWidth * (int)(((float)y + 0.25f) * inHeight / outHeight);
			inRow2 = in + inWidth * (int)(((float)y + 0.75f) * inHeight / outHeight);

			for( x = 0; x < outWidth; x++ )
			{
				pix1 = (byte *)inRow1 + p1[x];
				pix2 = (byte *)inRow1 + p2[x];
				pix3 = (byte *)inRow2 + p1[x];
				pix4 = (byte *)inRow2 + p2[x];

				normal[0] = MAKE_SIGNED( pix1[0] ) + MAKE_SIGNED( pix2[0] ) + MAKE_SIGNED( pix3[0] ) + MAKE_SIGNED( pix4[0] );
				normal[1] = MAKE_SIGNED( pix1[1] ) + MAKE_SIGNED( pix2[1] ) + MAKE_SIGNED( pix3[1] ) + MAKE_SIGNED( pix4[1] );
				normal[2] = MAKE_SIGNED( pix1[2] ) + MAKE_SIGNED( pix2[2] ) + MAKE_SIGNED( pix3[2] ) + MAKE_SIGNED( pix4[2] );

				if( !VectorNormalizeLength( normal ))
					VectorSet( normal, 0.5f, 0.5f, 1.0f );

				((byte *)(out+x))[0] = 128 + (byte)(127.0f * normal[0]);
				((byte *)(out+x))[1] = 128 + (byte)(127.0f * normal[1]);
				((byte *)(out+x))[2] = 128 + (byte)(127.0f * normal[2]);
				((byte *)(out+x))[3] = 255;
			}
		}
	}
	else
	{
		for( y = 0; y < outHeight; y++, out += outWidth )
		{
			inRow1 = in + inWidth * (int)(((float)y + 0.25f) * inHeight / outHeight);
			inRow2 = in + inWidth * (int)(((float)y + 0.75f) * inHeight / outHeight);

			for( x = 0; x < outWidth; x++ )
			{
				pix1 = (byte *)inRow1 + p1[x];
				pix2 = (byte *)inRow1 + p2[x];
				pix3 = (byte *)inRow2 + p1[x];
				pix4 = (byte *)inRow2 + p2[x];

				((byte *)(out+x))[0] = (pix1[0] + pix2[0] + pix3[0] + pix4[0]) >> 2;
				((byte *)(out+x))[1] = (pix1[1] + pix2[1] + pix3[1] + pix4[1]) >> 2;
				((byte *)(out+x))[2] = (pix1[2] + pix2[2] + pix3[2] + pix4[2]) >> 2;
				((byte *)(out+x))[3] = (pix1[3] + pix2[3] + pix3[3] + pix4[3]) >> 2;
			}
		}
	}

	return scaledImage;
}

void GL_BoxFilter3x3( byte *out, const byte *in, int w, int h, int x, int y )
{
	int		r = 0, g = 0, b = 0, a = 0;
	int		count = 0, acount = 0;
	int		i, j, u, v;
	const byte	*pixel;

	for( i = 0; i < 3; i++ )
	{
		u = ( i - 1 ) + x;

		for( j = 0; j < 3; j++ )
		{
			v = ( j - 1 ) + y;

			if( u >= 0 && u < w && v >= 0 && v < h )
			{
				pixel = &in[( u + v * w ) * 4];

				if( pixel[3] != 0 )
				{
					r += pixel[0];
					g += pixel[1];
					b += pixel[2];
					a += pixel[3];
					acount++;
				}
			}
		}
	}

	if(  acount == 0 )
		acount = 1;

	out[0] = r / acount;
	out[1] = g / acount;
	out[2] = b / acount;
}

byte *GL_ApplyFilter( const byte *source, int width, int height )
{
	byte	*in = (byte *)source;
	byte	*out = (byte *)source;
	int	i;

	if( ENGINE_GET_PARM( PARM_QUAKE_COMPATIBLE ) || glConfig.max_multisamples > 1 )
		return in;

	for( i = 0; source && i < width * height; i++, in += 4 )
	{
		if( in[0] == 0 && in[1] == 0 && in[2] == 0 && in[3] == 0 )
			GL_BoxFilter3x3( in, source, width, height, i % width, i / width );
	}

	return out;
}

static void GL_BuildMipMap( byte *in, int srcWidth, int srcHeight, int srcDepth, int flags )
{
	byte	*out = in;
	int	instride = ALIGN( srcWidth * 4, 1 );
	int	mipWidth, mipHeight, outpadding;
	int	row, x, y, z;
	vec3_t	normal;

	if( !in ) return;

	mipWidth = Q_max( 1, ( srcWidth >> 1 ));
	mipHeight = Q_max( 1, ( srcHeight >> 1 ));
	outpadding = ALIGN( mipWidth * 4, 1 ) - mipWidth * 4;
	row = srcWidth << 2;

	if( FBitSet( flags, TF_ALPHACONTRAST ))
	{
		memset( in, mipWidth, mipWidth * mipHeight * 4 );
		return;
	}

	// move through all layers
	for( z = 0; z < srcDepth; z++ )
	{
		if( FBitSet( flags, TF_NORMALMAP ))
		{
			for( y = 0; y < mipHeight; y++, in += instride * 2, out += outpadding )
			{
				byte *next = ((( y << 1 ) + 1 ) < srcHeight ) ? ( in + instride ) : in;
				for( x = 0, row = 0; x < mipWidth; x++, row += 8, out += 4 )
				{
					if((( x << 1 ) + 1 ) < srcWidth )
					{
						normal[0] = MAKE_SIGNED( in[row+0] ) + MAKE_SIGNED( in[row+4] )
						+ MAKE_SIGNED( next[row+0] ) + MAKE_SIGNED( next[row+4] );
						normal[1] = MAKE_SIGNED( in[row+1] ) + MAKE_SIGNED( in[row+5] )
						+ MAKE_SIGNED( next[row+1] ) + MAKE_SIGNED( next[row+5] );
						normal[2] = MAKE_SIGNED( in[row+2] ) + MAKE_SIGNED( in[row+6] )
						+ MAKE_SIGNED( next[row+2] ) + MAKE_SIGNED( next[row+6] );
					}
					else
					{
						normal[0] = MAKE_SIGNED( in[row+0] ) + MAKE_SIGNED( next[row+0] );
						normal[1] = MAKE_SIGNED( in[row+1] ) + MAKE_SIGNED( next[row+1] );
						normal[2] = MAKE_SIGNED( in[row+2] ) + MAKE_SIGNED( next[row+2] );
					}

					if( !VectorNormalizeLength( normal ))
						VectorSet( normal, 0.5f, 0.5f, 1.0f );

					out[0] = 128 + (byte)(127.0f * normal[0]);
					out[1] = 128 + (byte)(127.0f * normal[1]);
					out[2] = 128 + (byte)(127.0f * normal[2]);
					out[3] = 255;
				}
			}
		}
		else
		{
			for( y = 0; y < mipHeight; y++, in += instride * 2, out += outpadding )
			{
				byte *next = ((( y << 1 ) + 1 ) < srcHeight ) ? ( in + instride ) : in;
				for( x = 0, row = 0; x < mipWidth; x++, row += 8, out += 4 )
				{
					if((( x << 1 ) + 1 ) < srcWidth )
					{
						out[0] = (in[row+0] + in[row+4] + next[row+0] + next[row+4]) >> 2;
						out[1] = (in[row+1] + in[row+5] + next[row+1] + next[row+5]) >> 2;
						out[2] = (in[row+2] + in[row+6] + next[row+2] + next[row+6]) >> 2;
						out[3] = (in[row+3] + in[row+7] + next[row+3] + next[row+7]) >> 2;
					}
					else
					{
						out[0] = (in[row+0] + next[row+0]) >> 1;
						out[1] = (in[row+1] + next[row+1]) >> 1;
						out[2] = (in[row+2] + next[row+2]) >> 1;
						out[3] = (in[row+3] + next[row+3]) >> 1;
					}
				}
			}
		}
	}
}

static void GL_TextureImageRAW( gl_texture_t *tex, GLint side, GLint level, GLint width, GLint height, GLint depth, GLint type, const void *data )
{
	GLuint	cubeTarget = GL_TEXTURE_CUBE_MAP_POSITIVE_X_ARB;
	qboolean	subImage = FBitSet( tex->flags, TF_IMG_UPLOADED );
	GLenum	inFormat = gEngfuncs.Image_GetPFDesc(type)->glFormat;
	GLint	dataType = GL_UNSIGNED_BYTE;
	GLsizei	samplesCount = 0;

	Assert( tex != NULL );

	if( FBitSet( tex->flags, TF_DEPTHMAP ))
		inFormat = GL_DEPTH_COMPONENT;

	if( FBitSet( tex->flags, TF_ARB_16BIT ))
		dataType = GL_HALF_FLOAT_ARB;
	else if( FBitSet( tex->flags, TF_ARB_FLOAT ))
		dataType = GL_FLOAT;

	if( tex->target == GL_TEXTURE_1D )
	{
		if( subImage ) pglTexSubImage1D( tex->target, level, 0, width, inFormat, dataType, data );
		else pglTexImage1D( tex->target, level, tex->format, width, 0, inFormat, dataType, data );
	}
	else if( tex->target == GL_TEXTURE_CUBE_MAP_ARB )
	{
		if( subImage ) pglTexSubImage2D( cubeTarget + side, level, 0, 0, width, height, inFormat, dataType, data );
		else pglTexImage2D( cubeTarget + side, level, tex->format, width, height, 0, inFormat, dataType, data );
	}
	else if( tex->target == GL_TEXTURE_3D || tex->target == GL_TEXTURE_2D_ARRAY_EXT )
	{
		if( subImage ) pglTexSubImage3D( tex->target, level, 0, 0, 0, width, height, depth, inFormat, dataType, data );
		else pglTexImage3D( tex->target, level, tex->format, width, height, depth, 0, inFormat, dataType, data );
	}
	else if( tex->target == GL_TEXTURE_2D_MULTISAMPLE )
	{
#if !defined( XASH_GLES ) && !defined( XASH_GL4ES )
		samplesCount = (GLsizei)gEngfuncs.pfnGetCvarFloat("gl_msaa_samples");
		switch (samplesCount)
		{
			case 2:
			case 4:
			case 8:
			case 16:
				break;
			default:
				samplesCount = 1;
		}
		pglTexImage2DMultisample( tex->target, samplesCount, tex->format, width, height, GL_TRUE );
#else /* !XASH_GLES && !XASH_GL4ES */
		gEngfuncs.Con_Printf( S_ERROR "GLES renderer don't support GL_TEXTURE_2D_MULTISAMPLE!\n" );
#endif /* !XASH_GLES && !XASH_GL4ES */
	}
	else // 2D or RECT
	{
		if( subImage ) pglTexSubImage2D( tex->target, level, 0, 0, width, height, inFormat, dataType, data );
		else pglTexImage2D( tex->target, level, tex->format, width, height, 0, inFormat, dataType, data );
	}
}

static void GL_TextureImageDXT( gl_texture_t *tex, GLint side, GLint level, GLint width, GLint height, GLint depth, size_t size, const void *data )
{
	GLuint	cubeTarget = GL_TEXTURE_CUBE_MAP_POSITIVE_X_ARB;
	qboolean	subImage = FBitSet( tex->flags, TF_IMG_UPLOADED );

	Assert( tex != NULL );

#ifndef XASH_GLES
	if( tex->target == GL_TEXTURE_1D )
	{
		if( subImage ) pglCompressedTexSubImage1DARB( tex->target, level, 0, width, tex->format, size, data );
		else pglCompressedTexImage1DARB( tex->target, level, tex->format, width, 0, size, data );
	}
	else if( tex->target == GL_TEXTURE_CUBE_MAP_ARB )
	{
		if( subImage ) pglCompressedTexSubImage2DARB( cubeTarget + side, level, 0, 0, width, height, tex->format, size, data );
		else pglCompressedTexImage2DARB( cubeTarget + side, level, tex->format, width, height, 0, size, data );
	}
	else if( tex->target == GL_TEXTURE_3D || tex->target == GL_TEXTURE_2D_ARRAY_EXT )
	{
		if( subImage ) pglCompressedTexSubImage3DARB( tex->target, level, 0, 0, 0, width, height, depth, tex->format, size, data );
		else pglCompressedTexImage3DARB( tex->target, level, tex->format, width, height, depth, 0, size, data );
	}
	else // 2D or RECT
	{
		if( subImage ) pglCompressedTexSubImage2DARB( tex->target, level, 0, 0, width, height, tex->format, size, data );
		else pglCompressedTexImage2DARB( tex->target, level, tex->format, width, height, 0, size, data );
	}
#endif
}

static void GL_CheckTexImageError( gl_texture_t *tex )
{
	int	err;

	Assert( tex != NULL );

	// catch possible errors
	if( CVAR_TO_BOOL( gl_check_errors ) && ( err = pglGetError()) != GL_NO_ERROR )
		gEngfuncs.Con_Printf( S_OPENGL_ERROR "%s while uploading %s [%s]\n", GL_ErrorString( err ), tex->name, GL_TargetToString( tex->target ));
}

static qboolean GL_UploadTexture( int texnum, rgbdata_t *pic )
{
	byte		*buf, *data;
	size_t		texsize, size;
	uint		width, height;
	uint		i, j, numSides;
	uint		offset = 0;
	qboolean		normalMap;
	const byte	*bufend;

	gl_texture_t* tex = &(gl_textures[texnum]);
	Assert( tex->used == TRUE );

	// dedicated server
	if( !glw_state.initialized )
		return true;

	Assert( pic != NULL );
	Assert( tex != NULL );

	GL_SetTextureTarget( tex, pic ); // must be first

	// make sure what target is correct
	if( tex->target == GL_NONE )
	{
		gEngfuncs.Con_DPrintf( S_ERROR "GL_UploadTexture: %s is not supported by your hardware\n", tex->name );
		return false;
	}

	if( pic->type == PF_BC6H_SIGNED || pic->type == PF_BC6H_UNSIGNED || pic->type == PF_BC7 )
	{
		if( !GL_Support( GL_ARB_TEXTURE_COMPRESSION_BPTC ))
		{
			gEngfuncs.Con_DPrintf( S_ERROR "GL_UploadTexture: BC6H/BC7 compression formats is not supported by your hardware\n" );
			return false;
		}
	}

	GL_SetTextureDimensions( tex, pic->width, pic->height, pic->depth );
	GL_SetTextureFormat( tex, pic->type, pic->flags );

	tex->fogParams[0] = pic->fogParams[0];
	tex->fogParams[1] = pic->fogParams[1];
	tex->fogParams[2] = pic->fogParams[2];
	tex->fogParams[3] = pic->fogParams[3];

	if(( pic->width * pic->height ) & 3 )
	{
		// will be resampled, just tell me for debug targets
		gEngfuncs.Con_Reportf( "GL_UploadTexture: %s s&3 [%d x %d]\n", tex->name, pic->width, pic->height );
	}

	buf = pic->buffer;
	bufend = pic->buffer + pic->size; // total image size include all the layers, cube sides, mipmaps
	offset = GL_CalcImageSize( pic->type, pic->width, pic->height, pic->depth );
	texsize = GL_CalcTextureSize( tex->format, tex->width, tex->height, tex->depth );
	normalMap = FBitSet( tex->flags, TF_NORMALMAP ) ? true : false;
	numSides = FBitSet( pic->flags, IMAGE_CUBEMAP ) ? 6 : 1;

	// uploading texture into video memory, change the binding
	glState.currentTextures[glState.activeTMU] = tex->texnum;
	pglBindTexture( tex->target, tex->texnum );

	for( i = 0; i < numSides; i++ )
	{
		// track the buffer bounds
		if( buf != NULL && buf >= bufend )
			gEngfuncs.Host_Error( "GL_UploadTexture: %s image buffer overflow\n", tex->name );

		if( ImageDXT( pic->type ))
		{
			for( j = 0; j < Q_max( 1, pic->numMips ); j++ )
			{
				width = Q_max( 1, ( tex->width >> j ));
				height = Q_max( 1, ( tex->height >> j ));
				texsize = GL_CalcTextureSize( tex->format, width, height, tex->depth );
				size = GL_CalcImageSize( pic->type, width, height, tex->depth );
				GL_TextureImageDXT( tex, i, j, width, height, tex->depth, size, buf );
				tex->size += texsize;
				buf += size; // move pointer
				tex->numMips++;

				GL_CheckTexImageError( tex );
			}
		}
		else if( Q_max( 1, pic->numMips ) > 1 )	// not-compressed DDS
		{
			for( j = 0; j < Q_max( 1, pic->numMips ); j++ )
			{
				width = Q_max( 1, ( tex->width >> j ));
				height = Q_max( 1, ( tex->height >> j ));
				texsize = GL_CalcTextureSize( tex->format, width, height, tex->depth );
				size = GL_CalcImageSize( pic->type, width, height, tex->depth );
				GL_TextureImageRAW( tex, i, j, width, height, tex->depth, pic->type, buf );
				tex->size += texsize;
				buf += size; // move pointer
				tex->numMips++;

				GL_CheckTexImageError( tex );

			}
		}
		else // RGBA32
		{
			int mipCount = GL_CalcMipmapCount( tex, ( buf != NULL ));

			// NOTE: only single uncompressed textures can be resamples, no mips, no layers, no sides
			if(( tex->depth == 1 ) && (( pic->width != tex->width ) || ( pic->height != tex->height )))
				data = GL_ResampleTexture( buf, pic->width, pic->height, tex->width, tex->height, normalMap );
			else data = buf;

			if( !ImageDXT( pic->type ) && !FBitSet( tex->flags, TF_NOMIPMAP ) && FBitSet( pic->flags, IMAGE_ONEBIT_ALPHA ))
				data = GL_ApplyFilter( data, tex->width, tex->height );

			// mips will be auto-generated if desired
			for( j = 0; j < mipCount; j++ )
			{
				width = Q_max( 1, ( tex->width >> j ));
				height = Q_max( 1, ( tex->height >> j ));
				texsize = GL_CalcTextureSize( tex->format, width, height, tex->depth );
				size = GL_CalcImageSize( pic->type, width, height, tex->depth );
				GL_TextureImageRAW( tex, i, j, width, height, tex->depth, pic->type, data );
				if( mipCount > 1 )
					GL_BuildMipMap( data, width, height, tex->depth, tex->flags );
				tex->size += texsize;
				tex->numMips++;

				GL_CheckTexImageError( tex );
			}

			// move to next side
			if( numSides > 1 && ( buf != NULL ))
				buf += GL_CalcImageSize( pic->type, pic->width, pic->height, 1 );
		}
	}

	SetBits( tex->flags, TF_IMG_UPLOADED ); // done
	tex->numMips /= numSides;

	return true;
}

static void GL_ProcessImage( int texnum, rgbdata_t *pic )
{
	uint img_flags = 0;
	
	gl_texture_t* tex = &(gl_textures[texnum]);

	// force upload texture as RGB or RGBA (detail textures requires this)
	if( tex->flags & TF_FORCE_COLOR ) pic->flags |= IMAGE_HAS_COLOR;
	if( pic->flags & IMAGE_HAS_ALPHA ) tex->flags |= TF_HAS_ALPHA;

	tex->encode = pic->encode; // share encode method

	if( ImageDXT( pic->type ))
	{
		if( !pic->numMips )
			tex->flags |= TF_NOMIPMAP; // disable mipmapping by user request

		// clear all the unsupported flags
		tex->flags &= ~TF_KEEP_SOURCE;
	}
	else
	{
		// copy flag about luma pixels
		if( pic->flags & IMAGE_HAS_LUMA )
			tex->flags |= TF_HAS_LUMA;

		if( pic->flags & IMAGE_QUAKEPAL )
			tex->flags |= TF_QUAKEPAL;

		// create luma texture from quake texture
		if( tex->flags & TF_MAKELUMA )
		{
			img_flags |= IMAGE_MAKE_LUMA;
			tex->flags &= ~TF_MAKELUMA;
		}

		if( !FBitSet( tex->flags, TF_IMG_UPLOADED ) && FBitSet( tex->flags, TF_KEEP_SOURCE ))
			tex->original = gEngfuncs.FS_CopyImage( pic ); // because current pic will be expanded to rgba

		// we need to expand image into RGBA buffer
		if( pic->type == PF_INDEXED_24 || pic->type == PF_INDEXED_32 )
			img_flags |= IMAGE_FORCE_RGBA;

		// processing image before uploading (force to rgba, make luma etc)
		if( pic->buffer ) gEngfuncs.Image_Process( &pic, 0, 0, img_flags, 0 );

		if( FBitSet( tex->flags, TF_LUMINANCE ))
			ClearBits( pic->flags, IMAGE_HAS_COLOR );
	}
}

void GL_DeleteTexture( int texnum )
{
	if ( gl_textures[texnum].used )
	{
		memset( &(gl_textures[texnum]), 0, sizeof(gl_texture_t) );

		pglDeleteTextures( 1, &texnum );
	}
}

void GL_UpdateTexSize( int texnum, int width, int height, int depth )
{
	int		i, j, texsize;
	int		numSides;
	gl_texture_t	*tex;

	if( texnum <= 0 || texnum >= MAX_TEXTURES )
		return;

	tex = &gl_textures[texnum];
	numSides = FBitSet( tex->flags, TF_CUBEMAP ) ? 6 : 1;
	GL_SetTextureDimensions( tex, width, height, depth );
	tex->size = 0; // recompute now

	for( i = 0; i < numSides; i++ )
	{
		for( j = 0; j < Q_max( 1, tex->numMips ); j++ )
		{
			width = Q_max( 1, ( tex->width >> j ));
			height = Q_max( 1, ( tex->height >> j ));
			texsize = GL_CalcTextureSize( tex->format, width, height, tex->depth );
			tex->size += texsize;
		}
	}
}

qboolean GL_LoadTextureFromBuffer( int texnum, rgbdata_t *pic, texFlags_t flags, qboolean update )
{
	gl_texture_t *tex;

	// See if already loaded
	if( gl_textures[texnum].used && !update )
		return true;

	// Invalid picture pointer
	if( !pic )
		return false;

	if( update )
	{
		if( gl_textures[texnum].used == false )
		{
			gEngfuncs.Host_Error( "GL_LoadTextureFromBuffer: couldn't find texture with num %d for update\n", texnum );
		}

		SetBits( gl_textures[texnum].flags, flags );
	}
	else
	{
		// Initialize the new one
		memset( &(gl_textures[texnum]), 0, sizeof(gl_texture_t) );

		gl_textures[texnum].used   = true;
		gl_textures[texnum].texnum = texnum;
		gl_textures[texnum].flags  = flags;
	}

	GL_ProcessImage( texnum, pic );
	GL_UploadTexture( texnum, pic );  // FIXME: How to handle error?
	GL_ApplyTextureParams( texnum );  // Update texture filter, wrap etc
	
	return true;
}

int GL_CreateTexture( int texnum, int width, int height, const void *buffer, texFlags_t flags )
{
	qboolean	update = FBitSet( flags, TF_UPDATE ) ? true : false;
	int	datasize = 1;
	rgbdata_t	r_empty;

	if( FBitSet( flags, TF_ARB_16BIT ))
		datasize = 2;
	else if( FBitSet( flags, TF_ARB_FLOAT ))
		datasize = 4;

	ClearBits( flags, TF_UPDATE );
	memset( &r_empty, 0, sizeof( r_empty ));
	r_empty.width = width;
	r_empty.height = height;
	r_empty.type = PF_RGBA_32;
	r_empty.size = r_empty.width * r_empty.height * datasize * 4;
	r_empty.buffer = (byte *)buffer;

	// clear invalid combinations
	ClearBits( flags, TF_TEXTURE_3D );

	// if image not luminance and not alphacontrast it will have color
	if( !FBitSet( flags, TF_LUMINANCE ) && !FBitSet( flags, TF_ALPHACONTRAST ))
		SetBits( r_empty.flags, IMAGE_HAS_COLOR );

	if( FBitSet( flags, TF_HAS_ALPHA ))
		SetBits( r_empty.flags, IMAGE_HAS_ALPHA );

	if( FBitSet( flags, TF_CUBEMAP ))
	{
		if( !GL_Support( GL_TEXTURE_CUBEMAP_EXT ))
			return 0;
		SetBits( r_empty.flags, IMAGE_CUBEMAP );
		r_empty.size *= 6;
	}

	return GL_LoadTextureFromBuffer( texnum, &r_empty, flags, update );
}

int GL_CreateTextureArray( int texnum, int width, int height, int depth, const void *buffer, texFlags_t flags )
{
	rgbdata_t	r_empty;

	memset( &r_empty, 0, sizeof( r_empty ));
	r_empty.width = Q_max( width, 1 );
	r_empty.height = Q_max( height, 1 );
	r_empty.depth = Q_max( depth, 1 );
	r_empty.type = PF_RGBA_32;
	r_empty.size = r_empty.width * r_empty.height * r_empty.depth * 4;
	r_empty.buffer = (byte *)buffer;

	// clear invalid combinations
	ClearBits( flags, TF_CUBEMAP|TF_SKYSIDE|TF_HAS_LUMA|TF_MAKELUMA|TF_ALPHACONTRAST );

	// if image not luminance it will have color
	if( !FBitSet( flags, TF_LUMINANCE ))
		SetBits( r_empty.flags, IMAGE_HAS_COLOR );

	if( FBitSet( flags, TF_HAS_ALPHA ))
		SetBits( r_empty.flags, IMAGE_HAS_ALPHA );

	if( FBitSet( flags, TF_TEXTURE_3D ))
	{
		if( !GL_Support( GL_TEXTURE_3D_EXT ))
			return 0;
	}
	else
	{
		if( !GL_Support( GL_TEXTURE_ARRAY_EXT ))
			return 0;
		SetBits( r_empty.flags, IMAGE_MULTILAYER );
	}

	return GL_LoadTextureInternal( &(gl_textures[texnum].name), &r_empty, flags );
}

void GL_ProcessTexture( int texnum, float gamma, int topColor, int bottomColor )
{
	gl_texture_t	*image;
	rgbdata_t		*pic;
	int		flags = 0;

	if( texnum <= 0 || texnum >= MAX_TEXTURES )
		return; // missed image

	image = &gl_textures[texnum];

	// select mode
	if( gamma != -1.0f )
	{
		flags = IMAGE_LIGHTGAMMA;
	}
	else if( topColor != -1 && bottomColor != -1 )
	{
		flags = IMAGE_REMAP;
	}
	else
	{
		gEngfuncs.Con_Printf( S_ERROR "GL_ProcessTexture: bad operation for %s\n", image->name );
		return;
	}

	if( !image->original )
	{
		gEngfuncs.Con_Printf( S_ERROR "GL_ProcessTexture: no input data for %s\n", image->name );
		return;
	}

	if( ImageDXT( image->original->type ))
	{
		gEngfuncs.Con_Printf( S_ERROR "GL_ProcessTexture: can't process compressed texture %s\n", image->name );
		return;
	}

	// all the operations makes over the image copy not an original
	pic = gEngfuncs.FS_CopyImage( image->original );

	// we need to expand image into RGBA buffer
	if( pic->type == PF_INDEXED_24 || pic->type == PF_INDEXED_32 )
		flags |= IMAGE_FORCE_RGBA;

	gEngfuncs.Image_Process( &pic, topColor, bottomColor, flags, 0.0f );

	GL_UploadTexture( texnum, pic );
	GL_ApplyTextureParams( texnum ); // update texture filter, wrap etc

	gEngfuncs.FS_FreeImage( pic );
}

int GL_TexMemory( void )
{
	int	i, total = 0;

	for( i = 0; i < gl_numTextures; i++ )
		total += gl_textures[i].size;

	return total;
}

/*
===============
R_TextureList_f
===============
*/
void R_TextureList_f( void )
{
	gl_texture_t	*image;
	int		i, texCount, bytes = 0;

	gEngfuncs.Con_Printf( "\n" );
	gEngfuncs.Con_Printf( " -id-   -w-  -h-     -size- -fmt- -type- -data-  -encode- -wrap- -depth- -name--------\n" );

	for( i = texCount = 0, image = gl_textures; i < gl_numTextures; i++, image++ )
	{
		if( !image->texnum ) continue;

		bytes += image->size;
		texCount++;

		gEngfuncs.Con_Printf( "%4i: ", i );
		gEngfuncs.Con_Printf( "%4i %4i ", image->width, image->height );
		gEngfuncs.Con_Printf( "%12s ", Q_memprint( image->size ));

		switch( image->format )
		{
		case GL_COMPRESSED_RGBA_ARB:
			gEngfuncs.Con_Printf( "CRGBA " );
			break;
		case GL_COMPRESSED_RGB_ARB:
			gEngfuncs.Con_Printf( "CRGB  " );
			break;
		case GL_COMPRESSED_LUMINANCE_ALPHA_ARB:
			gEngfuncs.Con_Printf( "CLA   " );
			break;
		case GL_COMPRESSED_LUMINANCE_ARB:
			gEngfuncs.Con_Printf( "CL    " );
			break;
		case GL_COMPRESSED_ALPHA_ARB:
			gEngfuncs.Con_Printf( "CA    " );
			break;
		case GL_COMPRESSED_INTENSITY_ARB:
			gEngfuncs.Con_Printf( "CI    " );
			break;
		case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
			gEngfuncs.Con_Printf( "DXT1c " );
			break;
		case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
			gEngfuncs.Con_Printf( "DXT1a " );
			break;
		case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
			gEngfuncs.Con_Printf( "DXT3  " );
			break;
		case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
			gEngfuncs.Con_Printf( "DXT5  " );
			break;
		case GL_COMPRESSED_RED_GREEN_RGTC2_EXT:
		case GL_COMPRESSED_LUMINANCE_ALPHA_3DC_ATI:
			gEngfuncs.Con_Printf( "ATI2  " );
			break;
		case GL_RGBA:
			gEngfuncs.Con_Printf( "RGBA  " );
			break;
		case GL_RGBA8:
			gEngfuncs.Con_Printf( "RGBA8 " );
			break;
		case GL_RGBA4:
			gEngfuncs.Con_Printf( "RGBA4 " );
			break;
		case GL_RGB:
			gEngfuncs.Con_Printf( "RGB   " );
			break;
		case GL_RGB8:
			gEngfuncs.Con_Printf( "RGB8  " );
			break;
		case GL_RGB5:
			gEngfuncs.Con_Printf( "RGB5  " );
			break;
		case GL_LUMINANCE4_ALPHA4:
			gEngfuncs.Con_Printf( "L4A4  " );
			break;
		case GL_LUMINANCE_ALPHA:
		case GL_LUMINANCE8_ALPHA8:
			gEngfuncs.Con_Printf( "L8A8  " );
			break;
		case GL_LUMINANCE4:
			gEngfuncs.Con_Printf( "L4    " );
			break;
		case GL_LUMINANCE:
		case GL_LUMINANCE8:
			gEngfuncs.Con_Printf( "L8    " );
			break;
		case GL_ALPHA8:
			gEngfuncs.Con_Printf( "A8    " );
			break;
		case GL_INTENSITY8:
			gEngfuncs.Con_Printf( "I8    " );
			break;
		case GL_DEPTH_COMPONENT:
		case GL_DEPTH_COMPONENT24:
			gEngfuncs.Con_Printf( "DPTH24" );
			break;
		case GL_DEPTH_COMPONENT32F:
			gEngfuncs.Con_Printf( "DPTH32" );
			break;
		case GL_LUMINANCE16F_ARB:
			gEngfuncs.Con_Printf( "L16F  " );
			break;
		case GL_LUMINANCE32F_ARB:
			gEngfuncs.Con_Printf( "L32F  " );
			break;
		case GL_LUMINANCE_ALPHA16F_ARB:
			gEngfuncs.Con_Printf( "LA16F " );
			break;
		case GL_LUMINANCE_ALPHA32F_ARB:
			gEngfuncs.Con_Printf( "LA32F " );
			break;
		case GL_RG16F:
			gEngfuncs.Con_Printf( "RG16F " );
			break;
		case GL_RG32F:
			gEngfuncs.Con_Printf( "RG32F " );
			break;
		case GL_RGB16F_ARB:
			gEngfuncs.Con_Printf( "RGB16F" );
			break;
		case GL_RGB32F_ARB:
			gEngfuncs.Con_Printf( "RGB32F" );
			break;
		case GL_RGBA16F_ARB:
			gEngfuncs.Con_Printf( "RGBA16F" );
			break;
		case GL_RGBA32F_ARB:
			gEngfuncs.Con_Printf( "RGBA32F" );
			break;
		default:
			gEngfuncs.Con_Printf( " ^1ERROR^7 " );
			break;
		}

		switch( image->target )
		{
		case GL_TEXTURE_1D:
			gEngfuncs.Con_Printf( " 1D   " );
			break;
		case GL_TEXTURE_2D:
			gEngfuncs.Con_Printf( " 2D   " );
			break;
		case GL_TEXTURE_3D:
			gEngfuncs.Con_Printf( " 3D   " );
			break;
		case GL_TEXTURE_CUBE_MAP_ARB:
			gEngfuncs.Con_Printf( "CUBE  " );
			break;
		case GL_TEXTURE_RECTANGLE_EXT:
			gEngfuncs.Con_Printf( "RECT  " );
			break;
		case GL_TEXTURE_2D_ARRAY_EXT:
			gEngfuncs.Con_Printf( "ARRAY " );
			break;
		case GL_TEXTURE_2D_MULTISAMPLE:
			gEngfuncs.Con_Printf( "MSAA  ");
			break;
		default:
			gEngfuncs.Con_Printf( "????  " );
			break;
		}

		if( image->flags & TF_NORMALMAP )
			gEngfuncs.Con_Printf( "normal  " );
		else gEngfuncs.Con_Printf( "diffuse " );

		switch( image->encode )
		{
		case DXT_ENCODE_COLOR_YCoCg:
			gEngfuncs.Con_Printf( "YCoCg     " );
			break;
		case DXT_ENCODE_NORMAL_AG_ORTHO:
			gEngfuncs.Con_Printf( "ortho     " );
			break;
		case DXT_ENCODE_NORMAL_AG_STEREO:
			gEngfuncs.Con_Printf( "stereo    " );
			break;
		case DXT_ENCODE_NORMAL_AG_PARABOLOID:
			gEngfuncs.Con_Printf( "parabolic " );
			break;
		case DXT_ENCODE_NORMAL_AG_QUARTIC:
			gEngfuncs.Con_Printf( "quartic   " );
			break;
		case DXT_ENCODE_NORMAL_AG_AZIMUTHAL:
			gEngfuncs.Con_Printf( "azimuthal " );
			break;
		default:
			gEngfuncs.Con_Printf( "default   " );
			break;
		}

		if( image->flags & TF_CLAMP )
			gEngfuncs.Con_Printf( "clamp  " );
		else if( image->flags & TF_BORDER )
			gEngfuncs.Con_Printf( "border " );
		else gEngfuncs.Con_Printf( "repeat " );
		gEngfuncs.Con_Printf( "   %d  ", image->depth );
		gEngfuncs.Con_Printf( "  %s\n", image->name );
	}

	gEngfuncs.Con_Printf( "---------------------------------------------------------\n" );
	gEngfuncs.Con_Printf( "%i total textures\n", texCount );
	gEngfuncs.Con_Printf( "%s total memory used\n", Q_memprint( bytes ));
	gEngfuncs.Con_Printf( "\n" );
}

/*
===============
R_InitImages
===============
*/
void R_InitImages( void )
{
	memset( gl_textures, 0, sizeof( gl_textures ));
	gl_numTextures = 0;

	R_SetTextureParameters();

        gEngfuncs.Cmd_AddCommand( "texturelist", R_TextureList_f, "display loaded textures list" );

	// We found this textures in engine,
	//   but they are not presented in render for this moment.
	// That's ok! Textures will be uploaded later from RM with index stable safety
	tr.defaultTexture  = gEngfuncs.RM_FindTexture( REF_DEFAULT_TEXTURE );
	tr.particleTexture = gEngfuncs.RM_FindTexture( REF_PARTICLE_TEXTURE );
	tr.whiteTexture    = gEngfuncs.RM_FindTexture( REF_WHITE_TEXTURE );
	tr.grayTexture     = gEngfuncs.RM_FindTexture( REF_GRAY_TEXTURE );
	tr.blackTexture    = gEngfuncs.RM_FindTexture( REF_BLACK_TEXTURE );
	tr.cinTexture      = gEngfuncs.RM_FindTexture( REF_CINEMA_TEXTURE );
	tr.solidskyTexture = gEngfuncs.RM_FindTexture( REF_SOLIDSKY_TEXTURE );
	tr.alphaskyTexture = gEngfuncs.RM_FindTexture( REF_ALPHASKY_TEXTURE );
	tr.dlightTexture   = gEngfuncs.RM_FindTexture( REF_DLIGHT_TEXTURE );

	gEngfuncs.Con_Printf( "Found standart textures.\n" );
	gEngfuncs.Con_Printf( "\tDefault %d\n",   tr.defaultTexture );
	gEngfuncs.Con_Printf( "\tParticle %d\n",  tr.particleTexture );
	gEngfuncs.Con_Printf( "\tWhite %d\n",     tr.whiteTexture );
	gEngfuncs.Con_Printf( "\tGray %d\n",      tr.grayTexture );
	gEngfuncs.Con_Printf( "\tBlack %d\n",     tr.blackTexture );
	gEngfuncs.Con_Printf( "\tCinematic %d\n", tr.cinTexture );
	gEngfuncs.Con_Printf( "\tSolidSky %d\n",  tr.solidskyTexture );
	gEngfuncs.Con_Printf( "\tAlphaSky %d\n",  tr.alphaskyTexture );
	gEngfuncs.Con_Printf( "\tDlight %d\n",    tr.dlightTexture );
}

void R_ShutdownImages( void )
{
	gEngfuncs.Cmd_RemoveCommand( "texturelist" );
}
