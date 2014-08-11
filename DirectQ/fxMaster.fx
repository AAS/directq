/*
Copyright (C) 1996-1997 Id Software, Inc.
Shader code (C) 2009-2010 MH

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 3
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

float4x4 WorldMatrix;
float4x4 ModelViewMatrix;
float4x4 EntMatrix;

Texture2D tmu0Texture;
Texture2D tmu1Texture;
Texture2D tmu2Texture;
TextureCube cubeTexture;

float warptime;
float warpscale;
float warpfactor;
float Overbright;
float AlphaVal;
float SkyFog;

int magfilter0, magfilter1, magfilter2;
int mipfilter0, mipfilter1, mipfilter2;
int minfilter0, minfilter1, minfilter2;
int address0, address1, address2;
int aniso0, aniso1, aniso2;

float3 Scale;
float3 r_origin;
float3 viewangles;

float4 FogColor;
float FogDensity;

sampler2D tmu0Sampler : register(s0) = sampler_state
{
	Texture = <tmu0Texture>;

	AddressU = <address0>;
	AddressV = <address0>;

	MagFilter = <magfilter0>;
	MinFilter = <minfilter0>;
	MipFilter = <mipfilter0>;

	MaxAnisotropy = <aniso0>;
};

sampler2D tmu1Sampler : register(s1) = sampler_state
{
	Texture = <tmu1Texture>;

	AddressU = <address1>;
	AddressV = <address1>;

	MagFilter = <magfilter1>;
	MinFilter = <minfilter1>;
	MipFilter = <mipfilter1>;

	MaxAnisotropy = <aniso1>;
};

sampler2D tmu2Sampler : register(s2) = sampler_state
{
	Texture = <tmu2Texture>;

	AddressU = <address2>;
	AddressV = <address2>;

	MagFilter = <magfilter2>;
	MinFilter = <minfilter2>;
	MipFilter = <mipfilter2>;

	MaxAnisotropy = <aniso2>;
};

samplerCUBE cubeSampler : register(s3) = sampler_state
{
	Texture = <cubeTexture>;

	AddressU = <address0>;
	AddressV = <address0>;
	AddressW = <address0>;

	MagFilter = <magfilter0>;
	MinFilter = <minfilter0>;
	MipFilter = <mipfilter0>;

	MaxAnisotropy = <aniso0>;
};


#ifdef hlsl_fog
float4 FogCalc (float4 color, float4 fogpos)
{
	float fogdist = length (fogpos);
	float fogfactor = clamp (exp2 (-FogDensity * FogDensity * fogdist * fogdist * 1.442695), 0.0, 1.0);
	return lerp (FogColor, color, fogfactor);
	return color;
}
#endif


#ifdef hlsl_fog
float4 GetLumaColor (float4 texcolor, float4 lightmap, float4 lumacolor, float4 FogPosition)
#else
float4 GetLumaColor (float4 texcolor, float4 lightmap, float4 lumacolor)
#endif
{
#ifdef hlsl_fog
	float4 lumaon = FogCalc (texcolor * lightmap * Overbright, FogPosition) + lumacolor;
	float4 lumaoff = FogCalc ((texcolor + lumacolor) * lightmap * Overbright, FogPosition);
#else
	float4 lumaon = (texcolor * lightmap * Overbright) + lumacolor;
	float4 lumaoff = (texcolor + lumacolor) * lightmap * Overbright;
#endif

	return max (lumaon, lumaoff);
}


/*
====================
2D GUI DRAWING
====================
*/

struct DrawVert
{
	float4 Position : POSITION0;
	float4 Color : COLOR0;
	float2 Tex0 : TEXCOORD0;
};


float4 PSDrawColored (DrawVert Input) : COLOR0
{
	return Input.Color;
}


float4 PSDrawTextured (DrawVert Input) : COLOR0
{
	return tex2D (tmu0Sampler, Input.Tex0) * Input.Color;
}


DrawVert VSDraw (DrawVert Input)
{
	DrawVert Output;
	
	Output.Position = mul (Input.Position, WorldMatrix);
	Output.Color = Input.Color;
	Output.Tex0 = Input.Tex0;

	return (Output);
}


/*
====================
UNDERWATER WARP
====================
*/

struct VSUnderwaterVert
{
	float4 Position : POSITION0;
	float4 Color : COLOR0;
	float2 Tex0 : TEXCOORD0;
	float2 Tex1 : TEXCOORD1;
};


struct PSUnderwaterVert
{
	float4 Position : POSITION0;
	float4 Color0 : COLOR0;
	float4 Color1 : COLOR1;
	float2 Tex0 : TEXCOORD0;
	float2 Tex1 : TEXCOORD1;
	float2 Tex2 : TEXCOORD2;
};


float4 PSDrawUnderwater (PSUnderwaterVert Input) : COLOR0
{
	float2 ofs = (((tex2D (tmu2Sampler, Input.Tex2)).rg) - 0.5f) * Scale.z * tex2D (tmu1Sampler, Input.Tex1).ba;
	return tex2D (tmu0Sampler, Input.Tex0 + ofs) * Input.Color0.a + Input.Color1;
}


PSUnderwaterVert VSDrawUnderwater (VSUnderwaterVert Input)
{
	PSUnderwaterVert Output;

	Output.Position = mul (Input.Position, WorldMatrix);

	Output.Color0 = 1.0f - Input.Color;
	Output.Color1 = float4 (Input.Color.rgb, 1.0f) * Input.Color.a;

	// we can't correct the sine warp for view angles as the gun model is drawn in a constant position irrespective of angles
	// so running the correction turns it into wobbly jelly.  lesser of two evils.
	Output.Tex0 = Input.Tex0;
	Output.Tex1 = Input.Tex1;
	Output.Tex2 = (Input.Tex1 + (warptime * 0.0625f)) * Scale.xy;

	return (Output);
}


/*
====================
ALIAS MODELS
====================
*/
float2 currlerp;
float2 lastlerp;
float3 ShadeVector;
float3 ShadeLight;
float DepthBias;

struct VertAliasVS
{
	float4 LastPosition : POSITION0;
	float4 CurrPosition : POSITION1;
	float4 LastNormal : TEXCOORD0;
	float4 CurrNormal : TEXCOORD1;
	float2 Tex0 : TEXCOORD2;
};

struct VertAliasPS
{
	float4 Position : POSITION0;
	float3 Normal : TEXCOORD0;
	float2 Tex0 : TEXCOORD1;

#ifdef hlsl_fog
	float4 FogPosition : TEXCOORD2;
#endif
};


float4 PSAliasLumaNoLuma (VertAliasPS Input) : COLOR0
{
	float4 Shade = float4 (ShadeLight * (dot (Input.Normal, ShadeVector) * -0.5f + 1.0f), 1.0f);

#ifdef hlsl_fog
	float4 color = FogCalc ((tex2D (tmu0Sampler, Input.Tex0) + tex2D (tmu1Sampler, Input.Tex0)) * (Shade * Overbright), Input.FogPosition);
#else
	float4 color = (tex2D (tmu0Sampler, Input.Tex0) + tex2D (tmu1Sampler, Input.Tex0)) * (Shade * Overbright);
#endif

	color.a = AlphaVal;
	return color;
}


float4 PSAliasLuma (VertAliasPS Input) : COLOR0
{
	float4 Shade = float4 (ShadeLight * (dot (Input.Normal, ShadeVector) * -0.5f + 1.0f), 1.0f);

#ifdef hlsl_fog
	float4 color = GetLumaColor (tex2D (tmu0Sampler, Input.Tex0), Shade, tex2D (tmu1Sampler, Input.Tex0), Input.FogPosition);
#else
	float4 color = GetLumaColor (tex2D (tmu0Sampler, Input.Tex0), Shade, tex2D (tmu1Sampler, Input.Tex0));
#endif

	color.a = AlphaVal;
	return color;
}


float4 PSAliasNoLuma (VertAliasPS Input) : COLOR0
{
	float4 Shade = float4 (ShadeLight * (dot (Input.Normal, ShadeVector) * -0.5f + 1.0f), 1.0f);

#ifdef hlsl_fog
	float4 color = FogCalc (tex2D (tmu0Sampler, Input.Tex0) * (Shade * Overbright), Input.FogPosition);
#else
	float4 color = tex2D (tmu0Sampler, Input.Tex0) * (Shade * Overbright);
#endif

	color.a = AlphaVal;
	return color;
}


VertAliasPS VSAliasVS (VertAliasVS Input)
{
	VertAliasPS Output;

	float4 BasePosition = mul (Input.LastPosition * lastlerp.x + Input.CurrPosition * currlerp.x, EntMatrix);

	// this is friendlier for preshaders
	Output.Position = mul (BasePosition, WorldMatrix);

	// the view model needs a depth range hack and this is the easiest way of doing it
	Output.Position.z *= DepthBias;

#ifdef hlsl_fog
	Output.FogPosition = mul (BasePosition, ModelViewMatrix);
#endif

	// scale, bias and interpolate the normals in the vertex shader for speed
	Output.Normal = ((Input.CurrNormal.xyz * currlerp.y) - currlerp.x) + ((Input.LastNormal.xyz * lastlerp.y) - lastlerp.x);
	Output.Tex0 = Input.Tex0;

	return Output;
}


struct VertShadowVS
{
	float4 LastPosition : POSITION0;
	float4 CurrPosition : POSITION1;
};


struct VertShadowPS
{
	float4 Position : POSITION0;
	float4 Color : COLOR0;

#ifdef hlsl_fog
	float4 FogPosition : TEXCOORD1;
#endif
};


VertShadowPS ShadowVS (VertShadowVS Input)
{
	VertShadowPS Output;

	float4 BasePosition = mul (Input.LastPosition * lastlerp.x + Input.CurrPosition * currlerp.x, EntMatrix);

	// the lightspot comes after the baseline matrix multiplication and is just stored in ShadeVector for convenience
	BasePosition.z = ShadeVector.z + 0.1f;

	Output.Position = mul (BasePosition, WorldMatrix);

#ifdef hlsl_fog
	Output.FogPosition = mul (BasePosition, ModelViewMatrix);
#endif

	Output.Color.r = 0;
	Output.Color.g = 0;
	Output.Color.b = 0;
	Output.Color.a = AlphaVal;

	return (Output);
}


float4 ShadowPS (VertShadowPS Input) : COLOR0
{
#ifdef hlsl_fog
	float4 color = FogCalc (Input.Color, Input.FogPosition);
	color.a = Input.Color.a;
	return color;
#endif
	return Input.Color;
}


/*
====================
PARTICLES (AND SPRITES)
====================
*/

struct VertParticleNonInstanced
{
	float4 Position : POSITION0;
	float4 Color : COLOR0;
	float2 Tex0 : TEXCOORD0;
};

struct PSParticleVert
{
	float4 Position : POSITION0;
	float4 Color : COLOR0;
	float2 Tex0 : TEXCOORD0;
	
#ifdef hlsl_fog
	float4 FogPosition : TEXCOORD1;
#endif
};


float4 PSParticles (PSParticleVert Input) : COLOR0
{
#ifdef hlsl_fog
	float4 texcolor = tex2D (tmu0Sampler, Input.Tex0);
	float4 color = FogCalc (texcolor * Input.Color, Input.FogPosition);
#else
	float4 texcolor = tex2D (tmu0Sampler, Input.Tex0);
	float4 color = texcolor * Input.Color;
#endif

	color.a = texcolor.a;
	return color;
}


PSParticleVert VSParticles (VertParticleNonInstanced Input)
{
	PSParticleVert Output;

	// this is friendlier for preshaders
	Output.Position = mul (Input.Position, WorldMatrix);
	
#ifdef hlsl_fog
	Output.FogPosition = mul (Input.Position, ModelViewMatrix);
#endif

	Output.Color = Input.Color;
	Output.Tex0 = Input.Tex0;

	return Output;
}


struct VertParticleInstanced
{
	float4 BasePosition : POSITION0;
	float2 Tex0 : TEXCOORD0;
	float3 Position : TEXCOORD1;
	float Scale : BLENDWEIGHT0;
	float4 Color : COLOR0;
};

float3 upvec;
float3 rightvec;

PSParticleVert VSParticlesInstanced (VertParticleInstanced Input)
{
	PSParticleVert Output;

	float4 NewPosition = float4
	(
		(Input.Position + 
		rightvec * Input.Scale * Input.BasePosition.x + 
		upvec * Input.Scale * Input.BasePosition.y),
		Input.BasePosition.w
	);
	
	// this is friendlier for preshaders
	Output.Position = mul (NewPosition, WorldMatrix);

#ifdef hlsl_fog
	Output.FogPosition = mul (NewPosition, ModelViewMatrix);
#endif

	Output.Color = Input.Color;
	Output.Tex0 = Input.Tex0;

	return Output;
}


/*
====================
LIQUID TEXTURES
====================
*/

struct VSLiquidVert
{
	float4 Position : POSITION0;
	float2 Texcoord : TEXCOORD0;
};

struct PSLiquidVert
{
	float4 Position : POSITION0;
	float2 Texcoord0 : TEXCOORD0;
	float2 Texcoord1 : TEXCOORD1;
#ifdef hlsl_fog
	float4 FogPosition : TEXCOORD2;
#endif
};

float4 LiquidPS (PSLiquidVert Input) : COLOR0
{
	// same warp calculation as is used for the fixed pipeline path
	// a lot of the heavier lifting here has been offloaded to the vs
	// fixme - should use the uwblur texture lookup
	float4 color = tex2D (tmu0Sampler, (Input.Texcoord0 + sin (Input.Texcoord1.yx) * warpscale) * 0.015625f);

#ifdef hlsl_fog
	color = FogCalc (color, Input.FogPosition);
#endif
	color.a = AlphaVal;

	return color;
}


PSLiquidVert LiquidVS (VSLiquidVert Input)
{
	PSLiquidVert Output;

	// this is friendlier for preshaders
	Output.Position = mul (Input.Position, WorldMatrix);
#ifdef hlsl_fog
	Output.FogPosition = mul (Input.Position, ModelViewMatrix);
#endif
	Output.Texcoord0 = Input.Texcoord;
	Output.Texcoord1 = (Input.Texcoord * warpfactor + warptime) * 0.024543f;

	return (Output);
}


/*
====================
WORLD MODEL
====================
*/

struct VSWorldVert
{
	float4 Position : POSITION0;
	float2 Tex0 : TEXCOORD0;
	float2 Tex1 : TEXCOORD1;
};

struct PSWorldVert
{
	float4 Position : POSITION0;
	float2 Tex0 : TEXCOORD0;
	float2 Tex1 : TEXCOORD1;
#ifdef hlsl_fog
	float4 FogPosition : TEXCOORD2;
#endif
};


float4 PSWorldNoLuma (PSWorldVert Input) : COLOR0
{
#ifdef hlsl_fog
	return FogCalc (tex2D (tmu1Sampler, Input.Tex0) * tex2D (tmu0Sampler, Input.Tex1) * Overbright, Input.FogPosition);
#else
	return tex2D (tmu1Sampler, Input.Tex0) * tex2D (tmu0Sampler, Input.Tex1) * Overbright;
#endif
}


float4 PSWorldNoLumaAlpha (PSWorldVert Input) : COLOR0
{
	float4 texcolor = tex2D (tmu1Sampler, Input.Tex0);

#ifdef hlsl_fog
	float4 color = FogCalc (texcolor * tex2D (tmu0Sampler, Input.Tex1) * Overbright, Input.FogPosition);
#else
	float4 color = texcolor * tex2D (tmu0Sampler, Input.Tex1) * Overbright;
#endif

	color.a = AlphaVal * texcolor.a;
	return color;
}


float4 PSWorldLumaNoLuma (PSWorldVert Input) : COLOR0
{
#ifdef hlsl_fog
	return FogCalc ((tex2D (tmu1Sampler, Input.Tex0) + tex2D (tmu2Sampler, Input.Tex0)) * (tex2D (tmu0Sampler, Input.Tex1) * Overbright), Input.FogPosition);
#else
	return (tex2D (tmu1Sampler, Input.Tex0) + tex2D (tmu2Sampler, Input.Tex0)) * (tex2D (tmu0Sampler, Input.Tex1) * Overbright);
#endif
}


float4 PSWorldLuma (PSWorldVert Input) : COLOR0
{
#ifdef hlsl_fog
	return GetLumaColor (tex2D (tmu1Sampler, Input.Tex0), tex2D (tmu0Sampler, Input.Tex1), tex2D (tmu2Sampler, Input.Tex0), Input.FogPosition);
#else
	return GetLumaColor (tex2D (tmu1Sampler, Input.Tex0), tex2D (tmu0Sampler, Input.Tex1), tex2D (tmu2Sampler, Input.Tex0));
#endif
}


float4 PSWorldLumaAlpha (PSWorldVert Input) : COLOR0
{
	float4 texcolor = tex2D (tmu1Sampler, Input.Tex0);

#ifdef hlsl_fog
	float4 color = GetLumaColor (texcolor, tex2D (tmu0Sampler, Input.Tex1), tex2D (tmu2Sampler, Input.Tex0), Input.FogPosition);
#else
	float4 color = GetLumaColor (texcolor, tex2D (tmu0Sampler, Input.Tex1), tex2D (tmu2Sampler, Input.Tex0));
#endif

	color.a = AlphaVal * texcolor.a;
	return color;
}


float4 PSWorldLumaNoLumaAlpha (PSWorldVert Input) : COLOR0
{
	float4 texcolor = tex2D (tmu1Sampler, Input.Tex0);

#ifdef hlsl_fog
	float4 color = FogCalc ((texcolor + tex2D (tmu2Sampler, Input.Tex0)) * (tex2D (tmu0Sampler, Input.Tex1) * Overbright), Input.FogPosition);
#else
	float4 color = (texcolor + tex2D (tmu2Sampler, Input.Tex0)) * (tex2D (tmu0Sampler, Input.Tex1) * Overbright);
#endif

	color.a = AlphaVal * texcolor.a;
	return color;
}


PSWorldVert VSWorldCommon (VSWorldVert Input)
{
	PSWorldVert Output;

	// this is friendlier for preshaders
	Output.Position = mul (Input.Position, WorldMatrix);
	
#ifdef hlsl_fog
	Output.FogPosition = mul (Input.Position, ModelViewMatrix);
#endif
	Output.Tex0 = Input.Tex0;
	Output.Tex1 = Input.Tex1;

	return Output;
}


/*
====================
SKY
====================
*/

struct PSSkyVert
{
	float4 Position : POSITION0;
	float3 Texcoord : TEXCOORD0;
};

struct VSSkyVert
{
	float4 Position : POSITION0;
};


float4 SkyWarpPS (PSSkyVert Input) : COLOR0
{
	// same as classic Q1 warp but done per-pixel on the GPU instead
	Input.Texcoord = mul (Input.Texcoord, 6 * 63 / length (Input.Texcoord));
		
	float4 solidcolor = tex2D (tmu0Sampler, mul (Input.Texcoord.xy + Scale.x, 0.0078125));
	float4 alphacolor = tex2D (tmu1Sampler, mul (Input.Texcoord.xy + Scale.y, 0.0078125));
	alphacolor.a *= AlphaVal;

	float4 color = (alphacolor * alphacolor.a) + (solidcolor * (1.0 - alphacolor.a));
	color.a = 1.0;

#ifdef hlsl_fog
	// to do - use the same fog density but fade it off a little?
	// something else???
	return lerp (FogColor, color, SkyFog);
#else
	return color;
#endif
}


PSSkyVert SkyWarpVS (VSSkyVert Input)
{
	PSSkyVert Output;

	// this is friendlier for preshaders
	Output.Position = mul (Input.Position, WorldMatrix);

	// use the untranslated input position as the output texcoord, shift by r_origin
	Output.Texcoord = Input.Position.xyz - r_origin;
	Output.Texcoord.z *= 3.0;
	return (Output);
}


float4 SkyBoxPS (PSSkyVert Input) : COLOR0
{
	float4 color = texCUBE (cubeSampler, Input.Texcoord);
	color.a = 1.0;
#ifdef hlsl_fog
	return lerp (FogColor, color, SkyFog);
#else
	return color;
#endif
}


PSSkyVert SkyBoxVS (VSSkyVert Input)
{
	PSSkyVert Output;

	// this is friendlier for preshaders
	Output.Position = mul (Input.Position, WorldMatrix);

	// use the untranslated input position as the output texcoord, shift by r_origin
	Output.Texcoord = Input.Position.xyz - r_origin;
	return (Output);
}


/*
====================
FX CRAP
====================
*/

technique MasterRefresh
{
	pass FX_PASS_ALIAS_NOLUMA
	{
		VertexShader = compile vs_2_0 VSAliasVS ();
		PixelShader = compile ps_2_0 PSAliasNoLuma ();
	}

	pass FX_PASS_ALIAS_LUMA
	{
		VertexShader = compile vs_2_0 VSAliasVS ();
		PixelShader = compile ps_2_0 PSAliasLuma ();
	}

	pass FX_PASS_LIQUID
	{
		VertexShader = compile vs_2_0 LiquidVS ();
		PixelShader = compile ps_2_0 LiquidPS ();
	}

	pass FX_PASS_SHADOW
	{
		VertexShader = compile vs_2_0 ShadowVS ();
		PixelShader = compile ps_2_0 ShadowPS ();
	}

	pass FX_PASS_WORLD_NOLUMA
	{
		VertexShader = compile vs_2_0 VSWorldCommon ();
		PixelShader = compile ps_2_0 PSWorldNoLuma ();
	}

	pass FX_PASS_WORLD_LUMA
	{
		VertexShader = compile vs_2_0 VSWorldCommon ();
		PixelShader = compile ps_2_0 PSWorldLuma ();
	}

	pass FX_PASS_SKYWARP
	{
		VertexShader = compile vs_2_0 SkyWarpVS ();
		PixelShader = compile ps_2_0 SkyWarpPS ();
	}
	
	pass FX_PASS_DRAWTEXTURED
	{
		VertexShader = compile vs_2_0 VSDraw ();
		PixelShader = compile ps_2_0 PSDrawTextured ();
	}
	
	pass FX_PASS_DRAWCOLORED
	{
		VertexShader = compile vs_2_0 VSDraw ();
		PixelShader = compile ps_2_0 PSDrawColored ();
	}
	
	pass FX_PASS_SKYBOX
	{
		VertexShader = compile vs_2_0 SkyBoxVS ();
		PixelShader = compile ps_2_0 SkyBoxPS ();
	}

	pass FX_PASS_PARTICLES
	{
		VertexShader = compile vs_2_0 VSParticles ();
		PixelShader = compile ps_2_0 PSParticles ();
	}

	pass FX_PASS_WORLD_NOLUMA_ALPHA
	{
		VertexShader = compile vs_2_0 VSWorldCommon ();
		PixelShader = compile ps_2_0 PSWorldNoLumaAlpha ();
	}

	pass FX_PASS_WORLD_LUMA_ALPHA
	{
		VertexShader = compile vs_2_0 VSWorldCommon ();
		PixelShader = compile ps_2_0 PSWorldLumaAlpha ();
	}

	pass FX_PASS_PARTICLES_INSTANCED
	{
		VertexShader = compile vs_2_0 VSParticlesInstanced ();
		PixelShader = compile ps_2_0 PSParticles ();
	}
	
	pass FX_PASS_UNDERWATER
	{
		VertexShader = compile vs_2_0 VSDrawUnderwater ();
		PixelShader = compile ps_2_0 PSDrawUnderwater ();
	}

	pass FX_PASS_ALIAS_LUMA_NO_LUMA
	{
		VertexShader = compile vs_2_0 VSAliasVS ();
		PixelShader = compile ps_2_0 PSAliasLumaNoLuma ();
	}

	pass FX_PASS_WORLD_LUMA_NO_LUMA
	{
		VertexShader = compile vs_2_0 VSWorldCommon ();
		PixelShader = compile ps_2_0 PSWorldLumaNoLuma ();
	}

	pass FX_PASS_WORLD_LUMA_NO_LUMA_ALPHA
	{
		VertexShader = compile vs_2_0 VSWorldCommon ();
		PixelShader = compile ps_2_0 PSWorldLumaNoLumaAlpha ();
	}
}



