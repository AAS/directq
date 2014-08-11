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

// ps2.0 guarantees 8 samplers min
Texture tmu0Texture;
Texture tmu1Texture;
Texture tmu2Texture;
Texture tmu3Texture;
Texture tmu4Texture;
Texture tmu5Texture;
Texture tmu6Texture;
Texture tmu7Texture;

float warptime;
float warpscale;
float warpfactor;
float Overbright;
float AlphaVal;
float SkyFog;

float3 Scale;
float3 r_origin;
float3 viewangles;
float3 viewforward;
float3 viewright;
float3 viewup;

float4 FogColor;
float FogDensity;

float genericscale;

// ps2.0 guarantees 8 samplers min
sampler tmu0Sampler : register(s0) = sampler_state {Texture = <tmu0Texture>;};
sampler tmu1Sampler : register(s1) = sampler_state {Texture = <tmu1Texture>;};
sampler tmu2Sampler : register(s2) = sampler_state {Texture = <tmu2Texture>;};
sampler tmu3Sampler : register(s3) = sampler_state {Texture = <tmu3Texture>;};
sampler tmu4Sampler : register(s4) = sampler_state {Texture = <tmu4Texture>;};
sampler tmu5Sampler : register(s5) = sampler_state {Texture = <tmu5Texture>;};
sampler tmu6Sampler : register(s6) = sampler_state {Texture = <tmu6Texture>;};
sampler tmu7Sampler : register(s7) = sampler_state {Texture = <tmu7Texture>;};


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

if these are changed we also need to look out for corona drawing as it reuses them!!!
====================
*/

struct DrawVert
{
	float4 Position : POSITION0;
	float4 Color : COLOR0;
	float2 Tex0 : TEXCOORD0;
};


float4 PSDrawTextured (DrawVert Input) : COLOR0
{
	return tex2D (tmu0Sampler, Input.Tex0) * Input.Color;
}


float4 PSDrawColored (DrawVert Input) : COLOR0
{
	return Input.Color;
}


DrawVert VSDrawTextured (DrawVert Input)
{
	DrawVert Output;

	// correct the half-pixel offset
	Output.Position = mul (Input.Position - float4 (0.5f, 0.5f, 0.0f, 0.0f), WorldMatrix);
	Output.Color = Input.Color;
	Output.Tex0 = Input.Tex0;

	return (Output);
}


DrawVert VSDrawColored (DrawVert Input)
{
	DrawVert Output;

	Output.Position = mul (Input.Position, WorldMatrix);
	Output.Color = Input.Color;
	Output.Tex0 = Input.Tex0;	// hack for hlsl compiler...

	return (Output);
}


DrawVert VSDrawBBoxes (DrawVert Input)
{
	DrawVert Output;

	Output.Position = mul (mul (Input.Position, EntMatrix), WorldMatrix);
	Output.Color = Input.Color;
	Output.Tex0 = Input.Tex0;	// hack for hlsl compiler...

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

	// correct the half-pixel offset
	Output.Position = mul (Input.Position - float4 (0.5f, 0.5f, 0.0f, 0.0f), WorldMatrix);

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
float2 aliaslerp;
float3 ShadeVector;
float3 ShadeLight;

struct VertAliasVS
{
	float4 CurrPosition : POSITION0;
	float3 CurrNormal : TEXCOORD0;
	float4 LastPosition : POSITION1;
	float3 LastNormal : TEXCOORD1;
	float3 Tex0 : TEXCOORD2;
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


float4 GetColormapColor (VertAliasPS Input)
{
	// and this, ladies and gentlemen, is one reason why Quake sucks but Quake II doesn't...
	float4 colormap = tex2D (tmu2Sampler, Input.Tex0);

	return (tex2D (tmu0Sampler, Input.Tex0) * (1.0f - (colormap.r + colormap.a))) +
		(tex2D (tmu3Sampler, float2 (colormap.g, 0)) * colormap.r) +
		(tex2D (tmu4Sampler, float2 (colormap.b, 0)) * colormap.a);
}


float4 PSAliasPlayerLumaNoLuma (VertAliasPS Input) : COLOR0
{
	float4 Shade = float4 (ShadeLight * (dot (Input.Normal, ShadeVector) * -0.5f + 1.0f), 1.0f);

#ifdef hlsl_fog
	float4 color = FogCalc ((GetColormapColor (Input) + tex2D (tmu1Sampler, Input.Tex0)) * (Shade * Overbright), Input.FogPosition);
#else
	float4 color = (GetColormapColor (Input) + tex2D (tmu1Sampler, Input.Tex0)) * (Shade * Overbright);
#endif

	color.a = AlphaVal;
	return color;
}


float4 PSAliasPlayerLuma (VertAliasPS Input) : COLOR0
{
	float4 Shade = float4 (ShadeLight * (dot (Input.Normal, ShadeVector) * -0.5f + 1.0f), 1.0f);

#ifdef hlsl_fog
	float4 color = GetLumaColor (GetColormapColor (Input), Shade, tex2D (tmu1Sampler, Input.Tex0), Input.FogPosition);
#else
	float4 color = GetLumaColor (GetColormapColor (Input), Shade, tex2D (tmu1Sampler, Input.Tex0));
#endif

	color.a = AlphaVal;
	return color;
}


float4 PSAliasPlayerNoLuma (VertAliasPS Input) : COLOR0
{
	float4 Shade = float4 (ShadeLight * (dot (Input.Normal, ShadeVector) * -0.5f + 1.0f), 1.0f);

#ifdef hlsl_fog
	float4 color = FogCalc (GetColormapColor (Input) * (Shade * Overbright), Input.FogPosition);
#else
	float4 color = GetColormapColor (Input) * (Shade * Overbright);
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


float4 PSAliasKurok (VertAliasPS Input) : COLOR0
{
	float4 texcolor = tex2D (tmu0Sampler, Input.Tex0);
	float4 Shade = float4 (ShadeLight * (dot (Input.Normal, ShadeVector) * -0.5f + 1.0f), 1.0f);

#ifdef hlsl_fog
	float4 color = FogCalc (texcolor * (Shade * Overbright), Input.FogPosition);
#else
	float4 color = texcolor * (Shade * Overbright);
#endif

	color.a = texcolor.a;
	return color;
}


// lerp factors for viewmodel
// [0] is lerped and [1] is delerped
float4 vmblends[2];

VertAliasPS VSAliasVSCommon (VertAliasVS Input, float2 blend, float zhack)
{
	VertAliasPS Output;

	// retrieve the correct blend lerps to use for this vertex
	float4 BasePosition = mul (Input.LastPosition * blend.y + Input.CurrPosition * blend.x, EntMatrix);

	// this is friendlier for preshaders
	Output.Position = mul (BasePosition, WorldMatrix);
	Output.Position.z *= zhack;

#ifdef hlsl_fog
	Output.FogPosition = mul (BasePosition, ModelViewMatrix);
#endif

	// scale, bias and interpolate the normals in the vertex shader for speed
	// full range normals overbright/overdark too much so we scale it down by half
	// this means that the normals will no longer be normalized, but in practice it doesn't matter - at least for Quake
	Output.Normal = Input.CurrNormal * blend.x + Input.LastNormal * blend.y;
	Output.Tex0 = Input.Tex0.xy;

	return Output;
}


// the view model needs a depth range hack and this is the easiest way of doing it
// (must find out how software quake did this)
VertAliasPS VSAliasVSViewModel (VertAliasVS Input) {return VSAliasVSCommon (Input, vmblends[Input.Tex0.z].xy, 0.15f);}
VertAliasPS VSAliasVS (VertAliasVS Input) {return VSAliasVSCommon (Input, aliaslerp, 1.0f);}


struct VertInstancedVS
{
	// ps2.0 guarantees up to 16 texcoord sets
	float4 CurrPosition : POSITION0;
	float3 CurrNormal : TEXCOORD0;
	float4 LastPosition : POSITION1;
	float3 LastNormal : TEXCOORD1;
	float3 Tex0 : TEXCOORD2;
	float4 MRow1 : TEXCOORD3;
	float4 MRow2 : TEXCOORD4;
	float4 MRow3 : TEXCOORD5;
	float4 MRow4 : TEXCOORD6;
	float2 Lerps : TEXCOORD7;
	float3 SVector : TEXCOORD8;
	float4 ColorAlpha : TEXCOORD9;
};


struct VertInstancedPS
{
	float4 Position : POSITION0;
	float3 Normal : TEXCOORD0;
	float2 Tex0 : TEXCOORD1;
	float3 SVector : TEXCOORD2;
	float4 ColorAlpha : TEXCOORD3;

#ifdef hlsl_fog
	float4 FogPosition : TEXCOORD4;
#endif
};


float4 PSAliasInstancedNoLuma (VertInstancedPS Input) : COLOR0
{
	float4 Shade = float4 (Input.ColorAlpha.rgb * (dot (Input.Normal, Input.SVector) * -0.5f + 1.0f), 1.0f);

#ifdef hlsl_fog
	float4 color = FogCalc (tex2D (tmu0Sampler, Input.Tex0) * (Shade * Overbright), Input.FogPosition);
#else
	float4 color = tex2D (tmu0Sampler, Input.Tex0) * (Shade * Overbright);
#endif

	color.a = Input.ColorAlpha.a;
	return color;
}


float4 PSAliasInstancedLuma (VertInstancedPS Input) : COLOR0
{
	float4 Shade = float4 (Input.ColorAlpha.rgb * (dot (Input.Normal, Input.SVector) * -0.5f + 1.0f), 1.0f);

#ifdef hlsl_fog
	float4 color = GetLumaColor (tex2D (tmu0Sampler, Input.Tex0), Shade, tex2D (tmu1Sampler, Input.Tex0), Input.FogPosition);
#else
	float4 color = GetLumaColor (tex2D (tmu0Sampler, Input.Tex0), Shade, tex2D (tmu1Sampler, Input.Tex0));
#endif

	color.a = Input.ColorAlpha.a;
	return color;
}


float4 PSAliasInstancedLumaNoLuma (VertInstancedPS Input) : COLOR0
{
	float4 Shade = float4 (Input.ColorAlpha.rgb * (dot (Input.Normal, Input.SVector) * -0.5f + 1.0f), 1.0f);

#ifdef hlsl_fog
	float4 color = FogCalc ((tex2D (tmu0Sampler, Input.Tex0) + tex2D (tmu1Sampler, Input.Tex0)) * (Shade * Overbright), Input.FogPosition);
#else
	float4 color = (tex2D (tmu0Sampler, Input.Tex0) + tex2D (tmu1Sampler, Input.Tex0)) * (Shade * Overbright);
#endif

	color.a = Input.ColorAlpha.a;
	return color;
}


VertInstancedPS VSAliasVSInstanced (VertInstancedVS Input)
{
	VertInstancedPS Output;
	float4x4 EntMatrixInstanced = float4x4 (Input.MRow1, Input.MRow2, Input.MRow3, Input.MRow4);

	float4 BasePosition = mul (Input.LastPosition * Input.Lerps.y + Input.CurrPosition * Input.Lerps.x, EntMatrixInstanced);

	// this is friendlier for preshaders
	Output.Position = mul (BasePosition, WorldMatrix);

#ifdef hlsl_fog
	Output.FogPosition = mul (BasePosition, ModelViewMatrix);
#endif

	// scale, bias and interpolate the normals in the vertex shader for speed
	// full range normals overbright/overdark too much so we scale it down by half
	// this means that the normals will no longer be normalized, but in practice it doesn't matter - at least for Quake
	Output.Normal = Input.CurrNormal * Input.Lerps.x + Input.LastNormal * Input.Lerps.y;
	Output.Tex0 = Input.Tex0.xy;
	Output.SVector = Input.SVector;
	Output.ColorAlpha = Input.ColorAlpha;

	return Output;
}


struct VertShadowVS
{
	float4 CurrPosition : POSITION0;
	float3 CurrNormal : TEXCOORD0;
	float4 LastPosition : POSITION1;
	float3 LastNormal : TEXCOORD1;
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

	float4 BasePosition = mul (Input.LastPosition * aliaslerp.y + Input.CurrPosition * aliaslerp.x, EntMatrix);

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

struct VertParticle
{
	float4 Data : POSITION0;
};

struct VertSprite
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


float4 PSSprite (PSParticleVert Input) : COLOR0
{
#ifdef hlsl_fog
	float4 texcolor = tex2D (tmu0Sampler, Input.Tex0);
	float4 color = FogCalc (texcolor * Input.Color, Input.FogPosition);
#else
	float4 texcolor = tex2D (tmu0Sampler, Input.Tex0);
	float4 color = texcolor * Input.Color;
#endif

	// color.a = texcolor.a;
	return color;
}


float4 PSParticlesSquare (PSParticleVert Input) : COLOR0
{
#ifdef hlsl_fog
	float4 color = FogCalc (Input.Color, Input.FogPosition);
#else
	float4 color = Input.Color;
#endif

	// procedurally generate the particle square for good speed and per-pixel accuracy at any scale
	color.a = 1;
	return color;
}


float4 PSParticles (PSParticleVert Input) : COLOR0
{
#ifdef hlsl_fog
	float4 color = FogCalc (Input.Color, Input.FogPosition);
#else
	float4 color = Input.Color;
#endif

	// procedurally generate the particle dot for good speed and per-pixel accuracy at any scale
	color.a = (1.0f - ((Input.Tex0.x * Input.Tex0.x) + (Input.Tex0.y * Input.Tex0.y))) * 1.5f;
	return color;
}


PSParticleVert VSSprite (VertSprite Input)
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


// only used for vs_2_0 Shader instancing
#define NumBatchInstances 120

float4 PartInstancePosition[NumBatchInstances] : PARTINSTANCEPOSITION;
float4 PartInstanceColor[NumBatchInstances] : PARTINSTANCECOLOR;

PSParticleVert VSParticles (VertParticle Input)
{
	PSParticleVert Output;

	// hack a scale up to keep particles from disappearing
	float2 Base = Input.Data.xy *
		(1 + dot (PartInstancePosition[Input.Data.z].xyz - r_origin, viewforward) *
		genericscale) * PartInstancePosition[Input.Data.z].w;

	float4 NewPosition = float4 (float3 (PartInstancePosition[Input.Data.z].xyz + viewup * Base.y + viewright * Base.x), 1);

	Output.Position = mul (NewPosition, WorldMatrix);
	Output.Color = PartInstanceColor[Input.Data.z];
	Output.Tex0 = Input.Data.xy;

#ifdef hlsl_fog
	Output.FogPosition = mul (NewPosition, ModelViewMatrix);
#endif

	return Output;
}


float4 coronacolour;
float3 dlorg;

struct PSCoronaVert
{
	float4 Position : POSITION0;
	float4 Color : COLOR0;
	float2 Tex0 : TEXCOORD0;
};


struct VSCoronaVert
{
	float4 Data : POSITION0;
};

float4 PSDrawCorona (PSCoronaVert Input) : COLOR0
{
	float4 color = Input.Color;
	color.a = 1.0f - ((Input.Tex0.x * Input.Tex0.x) + (Input.Tex0.y * Input.Tex0.y));
	// clip (color.a);
	color.a = pow (color.a, 3.0f);
	return color;
}


PSCoronaVert VSDrawCorona (VSCoronaVert Input)
{
	PSCoronaVert Output;

	// hack a scale up to keep particles from disappearing
	float2 Base = Input.Data.xy * PartInstancePosition[Input.Data.z].w;
	float4 NewPosition = float4 (float3 (PartInstancePosition[Input.Data.z].xyz + viewup * Base.y + viewright * Base.x), 1);

	Output.Position = mul (NewPosition, WorldMatrix);
	Output.Color = PartInstanceColor[Input.Data.z];
	Output.Tex0 = Input.Data.xy;

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

float2 ripple;

float4 LiquidPS (PSLiquidVert Input) : COLOR0
{
	// same warp calculation as is used for the fixed pipeline path
	// a lot of the heavier lifting here has been offloaded to the vs
	// tmu1Sampler contains a 2D sin lookup so we can get two sin calcs with one texture lookup
	float4 color = tex2D (tmu0Sampler, Input.Texcoord0 + (tex2D (tmu1Sampler, Input.Texcoord1).gr - 0.5f) * warpscale);

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

	// fixme - add an OnChange callback to r_warpfactor and premultiply this in the vertexes (we have a second texcoord so we can)
	// probably not that big a deal though, but nonetheless.
	Output.Texcoord1 = Input.Texcoord.yx * warpfactor + warptime;

	return (Output);
}


PSLiquidVert LiquidVSRipple (VSLiquidVert Input)
{
	PSLiquidVert Output;

	float4 nv = float4
	(
		Input.Position.x,
		Input.Position.y,
		Input.Position.z + ripple.x * sin (Input.Position.x * 0.05f + ripple.y) * sin (Input.Position.z * 0.05f + ripple.y),
		Input.Position.w
	);

	// this is friendlier for preshaders
	Output.Position = mul (nv, WorldMatrix);
#ifdef hlsl_fog
	Output.FogPosition = mul (nv, ModelViewMatrix);
#endif
	Output.Texcoord0 = Input.Texcoord;

	// fixme - add an OnChange callback to r_warpfactor and premultiply this in the vertexes (we have a second texcoord so we can)
	// probably not that big a deal though, but nonetheless.
	Output.Texcoord1 = Input.Texcoord.yx * warpfactor + warptime;

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
	float4 color = texCUBE (tmu0Sampler, Input.Texcoord);
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
		VertexShader = compile vs_2_0 VSDrawTextured ();
		PixelShader = compile ps_2_0 PSDrawTextured ();
	}
	
	pass FX_PASS_DRAWCOLORED
	{
		// if these are changed we also need to look out for corona drawing as it reuses them!!!
		VertexShader = compile vs_2_0 VSDrawColored ();
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

	pass FX_PASS_SPRITE
	{
		VertexShader = compile vs_2_0 VSSprite ();
		PixelShader = compile ps_2_0 PSSprite ();
	}
	
	pass FX_PASS_UNDERWATER
	{
		VertexShader = compile vs_2_0 VSDrawUnderwater ();
		PixelShader = compile ps_2_0 PSDrawUnderwater ();
	}

	pass FX_PASS_ALIAS_LUMA_NOLUMA
	{
		VertexShader = compile vs_2_0 VSAliasVS ();
		PixelShader = compile ps_2_0 PSAliasLumaNoLuma ();
	}

	pass FX_PASS_WORLD_LUMA_NOLUMA
	{
		VertexShader = compile vs_2_0 VSWorldCommon ();
		PixelShader = compile ps_2_0 PSWorldLumaNoLuma ();
	}

	pass FX_PASS_WORLD_LUMA_NOLUMA_ALPHA
	{
		VertexShader = compile vs_2_0 VSWorldCommon ();
		PixelShader = compile ps_2_0 PSWorldLumaNoLumaAlpha ();
	}

	pass FX_PASS_ALIAS_INSTANCED_NOLUMA
	{
		VertexShader = compile vs_2_0 VSAliasVSInstanced ();
		PixelShader = compile ps_2_0 PSAliasInstancedNoLuma ();
	}

	pass FX_PASS_ALIAS_INSTANCED_LUMA
	{
		VertexShader = compile vs_2_0 VSAliasVSInstanced ();
		PixelShader = compile ps_2_0 PSAliasInstancedLuma ();
	}

	pass FX_PASS_ALIAS_INSTANCED_LUMA_NOLUMA
	{
		VertexShader = compile vs_2_0 VSAliasVSInstanced ();
		PixelShader = compile ps_2_0 PSAliasInstancedLumaNoLuma ();
	}

	pass FX_PASS_ALIAS_VIEWMODEL_NOLUMA
	{
		VertexShader = compile vs_2_0 VSAliasVSViewModel ();
		PixelShader = compile ps_2_0 PSAliasNoLuma ();
	}

	pass FX_PASS_ALIAS_VIEWMODEL_LUMA
	{
		VertexShader = compile vs_2_0 VSAliasVSViewModel ();
		PixelShader = compile ps_2_0 PSAliasLuma ();
	}

	pass FX_PASS_ALIAS_VIEWMODEL_LUMA_NOLUMA
	{
		VertexShader = compile vs_2_0 VSAliasVSViewModel ();
		PixelShader = compile ps_2_0 PSAliasLumaNoLuma ();
	}

	pass FX_PASS_CORONA
	{
		VertexShader = compile vs_2_0 VSDrawCorona ();
		PixelShader = compile ps_2_0 PSDrawCorona ();
	}

	pass FX_PASS_BBOXES
	{
		VertexShader = compile vs_2_0 VSDrawBBoxes ();
		PixelShader = compile ps_2_0 PSDrawColored ();
	}

	pass FX_PASS_LIQUID_RIPPLE
	{
		VertexShader = compile vs_2_0 LiquidVSRipple ();
		PixelShader = compile ps_2_0 LiquidPS ();
	}

	pass FX_PASS_PARTICLE_SQUARE
	{
		VertexShader = compile vs_2_0 VSParticles ();
		PixelShader = compile ps_2_0 PSParticlesSquare ();
	}

	pass FX_PASS_ALIAS_KUROK
	{
		VertexShader = compile vs_2_0 VSAliasVS ();
		PixelShader = compile ps_2_0 PSAliasKurok ();
	}

	pass FX_PASS_ALIAS_PLAYER_NOLUMA
	{
		VertexShader = compile vs_2_0 VSAliasVS ();
		PixelShader = compile ps_2_0 PSAliasPlayerNoLuma ();
	}

	pass FX_PASS_ALIAS_PLAYER_LUMA
	{
		VertexShader = compile vs_2_0 VSAliasVS ();
		PixelShader = compile ps_2_0 PSAliasPlayerLuma ();
	}

	pass FX_PASS_ALIAS_PLAYER_LUMA_NOLUMA
	{
		VertexShader = compile vs_2_0 VSAliasVS ();
		PixelShader = compile ps_2_0 PSAliasPlayerLumaNoLuma ();
	}
}



