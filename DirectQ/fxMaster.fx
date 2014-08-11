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

// ps2.0 guarantees 8 samplers min and we bind to explicit registers so that D3D Sets will work as expected
Texture tmu0Texture : register(t0);
Texture tmu1Texture : register(t1);
Texture tmu2Texture : register(t2);
Texture tmu3Texture : register(t3);
Texture tmu4Texture : register(t4);
Texture tmu5Texture : register(t5);
Texture tmu6Texture : register(t6);
Texture tmu7Texture : register(t7);

float warptime;
float warpscale;
float warpfactor;
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

// ps2.0 guarantees 8 samplers min and we bind to explicit registers so that D3D Sets will work as expected
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
	return lerp (FogColor, color, clamp (exp2 (FogDensity * fogdist * fogdist), 0.0, 1.0));
}
#endif


#ifdef hlsl_fog
float4 GetLumaColor (float4 texcolor, float4 lightmap, float4 lumacolor, float4 FogPosition)
#else
float4 GetLumaColor (float4 texcolor, float4 lightmap, float4 lumacolor)
#endif
{
	// take the highest because sometimes the lit pixels can be brighter than if the fullbright is used
	// this is valid for both internal native and external textures
#ifdef hlsl_fog
	return max (lumacolor, FogCalc (texcolor * lightmap, FogPosition));
#else
	return max (lumacolor, (texcolor * lightmap));
#endif
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


float4 drawmult;
float4 drawadd;
float4 halfpixoffset;

DrawVert VSDrawTextured (DrawVert Input)
{
	DrawVert Output;

	// correct the half-pixel offset
	Output.Position = (Input.Position + halfpixoffset) * drawmult + drawadd;
	Output.Color = Input.Color;
	Output.Tex0 = Input.Tex0;

	return (Output);
}


DrawVert VSDrawColored (DrawVert Input)
{
	DrawVert Output;

	Output.Position = Input.Position * drawmult + drawadd;
	Output.Color = Input.Color;
	Output.Tex0 = Input.Tex0;	// hack for hlsl compiler...

	return (Output);
}


float4 bbcolor;

struct BBVert
{
	float4 Position : POSITION0;
};

float4 PSDrawBBoxes (BBVert Input) : COLOR0
{
	return bbcolor;
}


BBVert VSDrawBBoxes (BBVert Input)
{
	BBVert Output;

	Output.Position = mul (mul (Input.Position, EntMatrix), WorldMatrix);

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


float2 UnderwaterTexMult;

PSUnderwaterVert VSDrawUnderwater (DrawVert Input)
{
	PSUnderwaterVert Output;

	// untransformed
	Output.Position = float4 (Input.Position.x, Input.Position.y, 0, 1);

	Output.Color0 = 1.0f - Input.Color;
	Output.Color1 = float4 (Input.Color.rgb, 1.0f) * Input.Color.a;

	float2 TexCoord = (Input.Position.xy * 0.5f) + 0.5f;

	TexCoord.y = 1.0f - TexCoord.y;

	// we can't correct the sine warp for view angles as the gun model is drawn in a constant position irrespective of angles
	// so running the correction turns it into wobbly jelly.  lesser of two evils.
	Output.Tex0 = TexCoord;
	Output.Tex1 = TexCoord * UnderwaterTexMult;
	Output.Tex2 = (TexCoord * UnderwaterTexMult + (warptime * 0.0625f)) * Scale.xy;

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
	float4 FogPosition : TEXCOORD7;
#endif
};


float4 GetColormapColor (VertAliasPS Input)
{
	// and this, ladies and gentlemen, is one reason why Quake sucks but Quake II doesn't...
	float4 colormap = tex2D (tmu2Sampler, Input.Tex0);

	return (tex2D (tmu0Sampler, Input.Tex0) * (1.0f - (colormap.r + colormap.a))) +
		   (tex2D (tmu3Sampler, float2 (colormap.g, 0)) * colormap.r) +
		   (tex2D (tmu4Sampler, float2 (colormap.b, 0)) * colormap.a);
}


float4 PSAliasGetShade (float3 Color, float3 Normal, float3 Vector)
{
	float shadedot = dot (Normal, Vector);

	// wtf - this reproduces anorm_dots within as reasonable a degree of tolerance as the >= 0 case 
	if (shadedot < 0)
		return float4 (Color * (1.0f + shadedot * (13.0f / 44.0f)), 1.0f) * 2.0f;
	return float4 (Color * (1.0f + shadedot), 1.0f) * 2.0f;
}


float4 PSAliasPlayerLuma (VertAliasPS Input) : COLOR0
{
	float4 Shade = PSAliasGetShade (ShadeLight, Input.Normal, ShadeVector);

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
	float4 Shade = PSAliasGetShade (ShadeLight, Input.Normal, ShadeVector);

#ifdef hlsl_fog
	float4 color = FogCalc (GetColormapColor (Input) * Shade, Input.FogPosition);
#else
	float4 color = GetColormapColor (Input) * Shade;
#endif

	color.a = AlphaVal;
	return color;
}


float4 PSAliasLuma (VertAliasPS Input) : COLOR0
{
	float4 Shade = PSAliasGetShade (ShadeLight, Input.Normal, ShadeVector);

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
	float4 Shade = PSAliasGetShade (ShadeLight, Input.Normal, ShadeVector);

#ifdef hlsl_fog
	float4 color = FogCalc (tex2D (tmu0Sampler, Input.Tex0) * Shade, Input.FogPosition);
#else
	float4 color = tex2D (tmu0Sampler, Input.Tex0) * Shade;
#endif

	color.a = AlphaVal;
	return color;
}


// lerp factors for viewmodel
// [0] is lerped and [1] is delerped
float4 vmblends[2];


VertAliasPS VSAliasVSCommon (VertAliasVS Input, float2 blend)
{
	VertAliasPS Output;

#ifdef hlsl_fog
	float4 BasePosition = mul (lerp (Input.LastPosition, Input.CurrPosition, blend.x), EntMatrix);
	Output.Position = BasePosition;
	Output.FogPosition = BasePosition;
#else
	Output.Position = mul (lerp (Input.LastPosition, Input.CurrPosition, blend.x), EntMatrix);
#endif

	// scale, bias and interpolate the normals in the vertex shader for speed
	// full range normals overbright/overdark too much so we scale it down by half
	// this means that the normals will no longer be normalized, but in practice it doesn't matter - at least for Quake
	Output.Normal = lerp (Input.LastNormal, Input.CurrNormal, blend.x);
	Output.Tex0 = Input.Tex0.xy;

	return Output;
}


// the view model needs a depth range hack and this is the easiest way of doing it
// (must find out how software quake did this)
VertAliasPS VSAliasVSViewModel (VertAliasVS Input)
{
	float blend = vmblends[Input.Tex0.z].x;
	VertAliasPS Output;

#ifdef hlsl_fog
	float4 BasePosition = mul (lerp (Input.LastPosition, Input.CurrPosition, blend.x), EntMatrix);
	Output.Position = BasePosition;
	Output.FogPosition = BasePosition;
#else
	Output.Position = mul (lerp (Input.LastPosition, Input.CurrPosition, blend.x), EntMatrix);
#endif

	Output.Position.z *= 0.15f;

	// scale, bias and interpolate the normals in the vertex shader for speed
	// full range normals overbright/overdark too much so we scale it down by half
	// this means that the normals will no longer be normalized, but in practice it doesn't matter - at least for Quake
	Output.Normal = lerp (Input.LastNormal, Input.CurrNormal, blend.x);
	Output.Tex0 = Input.Tex0.xy;

	return Output;
}


VertAliasPS VSAliasVS (VertAliasVS Input)
{
	VertAliasPS Output;

#ifdef hlsl_fog
	float4 BasePosition = mul (lerp (Input.LastPosition, Input.CurrPosition, aliaslerp.x), EntMatrix);
	Output.Position = BasePosition;
	Output.FogPosition = BasePosition;
#else
	Output.Position = mul (lerp (Input.LastPosition, Input.CurrPosition, aliaslerp.x), EntMatrix);
#endif

	Output.Normal = lerp (Input.LastNormal, Input.CurrNormal, aliaslerp.x);
	Output.Tex0 = Input.Tex0.xy;

	return Output;
}


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
	float4 FogPosition : TEXCOORD7;
#endif
};


float4 PSAliasInstancedNoLuma (VertInstancedPS Input) : COLOR0
{
	float4 Shade = PSAliasGetShade (Input.ColorAlpha.rgb, Input.Normal, Input.SVector);

#ifdef hlsl_fog
	float4 color = FogCalc (tex2D (tmu0Sampler, Input.Tex0) * Shade, Input.FogPosition);
#else
	float4 color = tex2D (tmu0Sampler, Input.Tex0) * Shade;
#endif

	color.a = Input.ColorAlpha.a;
	return color;
}


float4 PSAliasInstancedLuma (VertInstancedPS Input) : COLOR0
{
	float4 Shade = PSAliasGetShade (Input.ColorAlpha.rgb, Input.Normal, Input.SVector);

#ifdef hlsl_fog
	float4 color = GetLumaColor (tex2D (tmu0Sampler, Input.Tex0), Shade, tex2D (tmu1Sampler, Input.Tex0), Input.FogPosition);
#else
	float4 color = GetLumaColor (tex2D (tmu0Sampler, Input.Tex0), Shade, tex2D (tmu1Sampler, Input.Tex0));
#endif

	color.a = Input.ColorAlpha.a;
	return color;
}


VertInstancedPS VSAliasVSInstanced (VertInstancedVS Input)
{
	VertInstancedPS Output;

	float4x4 EntMatrixInstanced = float4x4 (Input.MRow1, Input.MRow2, Input.MRow3, Input.MRow4);

#ifdef hlsl_fog
	float4 BasePosition = mul (lerp (Input.LastPosition, Input.CurrPosition, Input.Lerps.x), EntMatrixInstanced);
	Output.Position = BasePosition;
	Output.FogPosition = BasePosition;
#else
	Output.Position = mul (lerp (Input.LastPosition, Input.CurrPosition, Input.Lerps.x), EntMatrixInstanced);
#endif

	// scale, bias and interpolate the normals in the vertex shader for speed
	Output.Normal = lerp (Input.LastNormal, Input.CurrNormal, Input.Lerps.x);
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

#ifdef hlsl_fog
	float4 BasePosition = mul (lerp (Input.LastPosition, Input.CurrPosition, aliaslerp.x), EntMatrix);
	Output.FogPosition = BasePosition;
	Output.Position = BasePosition;
#else
	Output.Position = mul (lerp (Input.LastPosition, Input.CurrPosition, aliaslerp.x), EntMatrix);
#endif

	Output.Color = float4 (0, 0, 0, AlphaVal);

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
	color.a = (1.0f - dot (Input.Tex0.xy, Input.Tex0.xy)) * 1.5f;
	return color;
}


PSParticleVert VSSprite (VertSprite Input)
{
	PSParticleVert Output;

	Output.Position = mul (Input.Position, WorldMatrix);
	
#ifdef hlsl_fog
	Output.FogPosition = mul (Input.Position, WorldMatrix);
#endif

	Output.Color = Input.Color;
	Output.Tex0 = Input.Tex0;

	return Output;
}


// only used for vs_2_0 Shader instancing
//#define NumBatchInstances 120

float4 PartInstancePosition[NumBatchInstances] : PARTINSTANCEPOSITION;
float4 PartInstanceColor[NumBatchInstances] : PARTINSTANCECOLOR;

float4 BillboardParticle (float2 Coord, float3 Position, float Scale)
{
	// hack a scale up to keep particles from disappearing
	float2 Base = Coord * (1.0f + dot (Position - r_origin, viewforward) * genericscale) * Scale;

	return float4 (float3 (Position + viewup * Base.y + viewright * Base.x), 1);
}


PSParticleVert VSParticles (VertParticle Input)
{
	PSParticleVert Output;

	float4 NewPosition = BillboardParticle (Input.Data.xy, PartInstancePosition[Input.Data.z].xyz, PartInstancePosition[Input.Data.z].w);

	Output.Position = mul (NewPosition, WorldMatrix);
	Output.Color = PartInstanceColor[Input.Data.z];
	Output.Tex0 = Input.Data.xy;

#ifdef hlsl_fog
	Output.FogPosition = mul (NewPosition, WorldMatrix);
#endif

	return Output;
}


float3 entorigin;
float cltime;
float brightscale;
float4 brightcolor;
float partscale;
float partgrav;

struct VertBrightField
{
	float4 Normal : POSITION0;
	float2 AVelocity : TEXCOORD0;
	float2 Coord : TEXCOORD1;
};

PSParticleVert VSBrightField (VertBrightField Input)
{
	PSParticleVert Output;
	float2 syp, cyp;

	sincos (Input.AVelocity * cltime, syp, cyp);

	float3 Position = entorigin + Input.Normal + float3 (cyp.y * cyp.x, cyp.y * syp.x, -syp.y) * 16;
	float4 NewPosition = BillboardParticle (Input.Coord, Position, brightscale);

	Output.Position = mul (NewPosition, WorldMatrix);
	Output.Color = brightcolor;
	Output.Tex0 = Input.Coord;

#ifdef hlsl_fog
	Output.FogPosition = mul (NewPosition, WorldMatrix);
#endif

	return Output;
}


struct VertPartEffect
{
	float4 Position : POSITION0;
	float3 Vel : TEXCOORD0;
	float3 DVel : TEXCOORD1;
	float Grav : TEXCOORD2;
	float RampTime : TEXCOORD3;
	float Ramp : TEXCOORD4;
	float2 Coord : TEXCOORD5;
	float Scale : TEXCOORD6;
};


PSParticleVert VSParticleEffect (VertPartEffect Input)
{
	PSParticleVert Output;

	// needs ramps (will these need to wait until the geometry shader???)
	float3 grav = float3 (0, 0, (partgrav * Input.Grav));
	float3 Position = entorigin + Input.Position.xyz + (Input.Vel + (Input.DVel * cltime) + grav) * cltime;
	float4 NewPosition = BillboardParticle (Input.Coord, Position, Input.Scale * partscale);
	
	Output.Position = mul (NewPosition, WorldMatrix);
	Output.Color = float4 (1, 1, 1, 1);
	Output.Tex0 = Input.Coord;

#ifdef hlsl_fog
	Output.FogPosition = mul (NewPosition, WorldMatrix);
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
	color.a = 1.0f - dot (Input.Tex0.xy, Input.Tex0.xy);
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
float warptexturescale;

float4 LiquidPS (PSLiquidVert Input) : COLOR0
{
	// same warp calculation as is used for the fixed pipeline path
	float4 color = tex2D (tmu0Sampler, (Input.Texcoord0 + sin (Input.Texcoord1) * warpscale) * warptexturescale);

#ifdef hlsl_fog
	color = FogCalc (color, Input.FogPosition);
#endif
	color.a = AlphaVal;

	return color;
}


PSLiquidVert LiquidVS (VSLiquidVert Input)
{
	PSLiquidVert Output;

	Output.Position = mul (Input.Position, WorldMatrix);
	
#ifdef hlsl_fog
	Output.FogPosition = mul (Input.Position, WorldMatrix);
#endif

	Output.Texcoord0 = Input.Texcoord;
	Output.Texcoord1 = ((Input.Texcoord.yx * warpfactor) + warptime) * (3.141592654f / 128.0f);

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

	Output.Position = mul (nv, WorldMatrix);
#ifdef hlsl_fog
	Output.FogPosition = mul (nv, WorldMatrix);
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
	float4 FogPosition : TEXCOORD7;
#endif
};


float4 WorldGetLightmap (PSWorldVert Input)
{
	// swap to BGR and decode from HDR
	float4 light = tex2D (tmu1Sampler, Input.Tex1);
	return (light.zyxw / light.w);
}


float4 PSWorldNoLuma (PSWorldVert Input) : COLOR0
{
#ifdef hlsl_fog
	return FogCalc (tex2D (tmu0Sampler, Input.Tex0) * WorldGetLightmap (Input), Input.FogPosition);
#else
	return tex2D (tmu0Sampler, Input.Tex0) * WorldGetLightmap (Input);
#endif
}


float4 PSWorldNoLumaAlpha (PSWorldVert Input) : COLOR0
{
	float4 texcolor = tex2D (tmu0Sampler, Input.Tex0);

#ifdef hlsl_fog
	float4 color = FogCalc (texcolor * WorldGetLightmap (Input), Input.FogPosition);
#else
	float4 color = texcolor * WorldGetLightmap (Input);
#endif

	color.a = AlphaVal * texcolor.a;
	return color;
}


float4 PSWorldLuma (PSWorldVert Input) : COLOR0
{
#ifdef hlsl_fog
	return GetLumaColor (tex2D (tmu0Sampler, Input.Tex0), WorldGetLightmap (Input), tex2D (tmu2Sampler, Input.Tex0), Input.FogPosition);
#else
	return GetLumaColor (tex2D (tmu0Sampler, Input.Tex0), WorldGetLightmap (Input), tex2D (tmu2Sampler, Input.Tex0));
#endif
}


float4 PSWorldLumaAlpha (PSWorldVert Input) : COLOR0
{
	float4 texcolor = tex2D (tmu0Sampler, Input.Tex0);

#ifdef hlsl_fog
	float4 color = GetLumaColor (texcolor, WorldGetLightmap (Input), tex2D (tmu2Sampler, Input.Tex0), Input.FogPosition);
#else
	float4 color = GetLumaColor (texcolor, WorldGetLightmap (Input), tex2D (tmu2Sampler, Input.Tex0));
#endif

	color.a = AlphaVal * texcolor.a;
	return color;
}


PSWorldVert VSWorldCommon (VSWorldVert Input)
{
	PSWorldVert Output;

#ifdef hlsl_fog
	float4 outpos = mul (Input.Position, WorldMatrix);

	Output.Position = outpos;
	Output.FogPosition = outpos;
#else
	Output.Position = mul (Input.Position, WorldMatrix);
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


float4 SkySetFogging (float4 basecolor)
{
	basecolor.a = 1.0f;

#ifdef hlsl_fog
	// to do - use the same fog density but fade it off a little?
	// something else???
	return lerp (FogColor, basecolor, SkyFog);
#else
	return basecolor;
#endif
}


float4 SkyWarpPS (PSSkyVert Input) : COLOR0
{
	// same as classic Q1 warp but done per-pixel on the GPU instead
	float3 Texcoord = normalize (Input.Texcoord) * Scale.z;

	float4 solidcolor = tex2D (tmu0Sampler, Texcoord.xy + Scale.x);
	float4 alphacolor = tex2D (tmu1Sampler, Texcoord.xy + Scale.y);
	alphacolor.a *= AlphaVal;

	return SkySetFogging (lerp (solidcolor, alphacolor, alphacolor.a));
}


float skyspherescaley;

float4 SkySpherePS (PSSkyVert Input) : COLOR0
{
	float3 direction = normalize (Input.Texcoord);

	// fixme - texture lookup here...
	float theta = acos (direction.z); // make sure the direction is normalized!
	float phi = atan2 (direction.y, direction.x);
	// float phi = (tex2D (tmu1Sampler, (direction.yx * 0.5f) + 0.5f) * 6.2831853f - 3.1415926f).r;
	float2 texcoord = float2 (phi / 6.2831853f, ((theta + 3.1415926f) / 3.1415926f - 1.0f) * skyspherescaley);

	return SkySetFogging (tex2D (tmu0Sampler, texcoord));
}


float4 SkyBoxPS (PSSkyVert Input) : COLOR0
{
	return SkySetFogging (texCUBE (tmu0Sampler, Input.Texcoord));
}


PSSkyVert SkyCommonVS (VSSkyVert Input)
{
	// common to all types now
	PSSkyVert Output;

	Output.Position = mul (Input.Position, WorldMatrix);
	Output.Texcoord = mul (Input.Position, EntMatrix);

	return (Output);
}


/*
====================
POLYBLEND
====================
*/

float4 PSPolyBlend (DrawVert Input) : COLOR0
{
	return Input.Color;
}


DrawVert VSPolyBlend (DrawVert Input)
{
	DrawVert Output;

	Output.Position = float4 (Input.Position.x, Input.Position.y, 0, 1);
	Output.Color = Input.Color;
	Output.Tex0 = Input.Tex0;	// hack for hlsl compiler...

	return (Output);
}


/*
====================
IQM
====================
*/

struct VSIQMVert
{
	float4 Position : POSITION0;
	float3 Normal : TEXCOORD0;
	float2 Texcoord : TEXCOORD1;
	float4 BlendIndexes : TEXCOORD2;
	float4 BlendWeights : TEXCOORD3;
};


struct PSIQMVert
{
	float4 Position : POSITION0;
	float3 Normal : TEXCOORD0;
	float2 Texcoord : TEXCOORD1;
	
#ifdef hlsl_fog
	float4 FogPosition : TEXCOORD2;
#endif
};


float4 PSIQM_Shadow (PSIQMVert Input) : COLOR0
{
#ifdef hlsl_fog
	float4 color = FogCalc (float4 (0, 0, 0, AlphaVal), Input.FogPosition);
	color.a = AlphaVal;
	return color;
#endif

	return float4 (0, 0, 0, AlphaVal);
}


float4 PSIQM_NoLuma (PSIQMVert Input) : COLOR0
{
	float4 Shade = PSAliasGetShade (ShadeLight, Input.Normal, ShadeVector);

#ifdef hlsl_fog
	float4 color = FogCalc (tex2D (tmu0Sampler, Input.Texcoord) * Shade, Input.FogPosition);
#else
	float4 color = tex2D (tmu0Sampler, Input.Texcoord) * Shade;
#endif

	color.a = AlphaVal;
	return color;
}


float4 PSIQM_Luma (PSIQMVert Input) : COLOR0
{
	float4 Shade = PSAliasGetShade (ShadeLight, Input.Normal, ShadeVector);

#ifdef hlsl_fog
	float4 color = GetLumaColor (tex2D (tmu0Sampler, Input.Texcoord), Shade, tex2D (tmu1Sampler, Input.Texcoord), Input.FogPosition);
#else
	float4 color = GetLumaColor (tex2D (tmu0Sampler, Input.Texcoord), Shade, tex2D (tmu1Sampler, Input.Texcoord));
#endif

	color.a = AlphaVal;
	return color;
}


float4 IQMJoints1[NumIQMJoints] : IQMJOINTS1;
float4 IQMJoints2[NumIQMJoints] : IQMJOINTS2;
float4 IQMJoints3[NumIQMJoints] : IQMJOINTS3;

PSIQMVert VSIQM (VSIQMVert Input)
{
	PSIQMVert Output;

	// there's always one
	float3x4 Joint = float3x4 (IQMJoints1[Input.BlendIndexes.x], IQMJoints2[Input.BlendIndexes.x], IQMJoints3[Input.BlendIndexes.x]) * Input.BlendWeights.x;

	// and conditionally add the rest
	if (Input.BlendWeights.y > 0) Joint += float3x4 (IQMJoints1[Input.BlendIndexes.y], IQMJoints2[Input.BlendIndexes.y], IQMJoints3[Input.BlendIndexes.y]) * Input.BlendWeights.y;
	if (Input.BlendWeights.z > 0) Joint += float3x4 (IQMJoints1[Input.BlendIndexes.z], IQMJoints2[Input.BlendIndexes.z], IQMJoints3[Input.BlendIndexes.z]) * Input.BlendWeights.z;
	if (Input.BlendWeights.w > 0) Joint += float3x4 (IQMJoints1[Input.BlendIndexes.w], IQMJoints2[Input.BlendIndexes.w], IQMJoints3[Input.BlendIndexes.w]) * Input.BlendWeights.w;

#ifdef hlsl_fog
	float4 BasePosition = mul (float4 (mul (Joint, Input.Position), Input.Position.w), EntMatrix);

	Output.Position = BasePosition;
	Output.FogPosition = BasePosition;
#else
	Output.Position = mul (float4 (mul (Joint, Input.Position), Input.Position.w), EntMatrix);
#endif

	Output.Normal = Input.Normal;
	Output.Texcoord = Input.Texcoord;

	return Output;
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
		VertexShader = compile vs_2_0 SkyCommonVS ();
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
		VertexShader = compile vs_2_0 SkyCommonVS ();
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

	pass FX_PASS_CORONA
	{
		VertexShader = compile vs_2_0 VSDrawCorona ();
		PixelShader = compile ps_2_0 PSDrawCorona ();
	}

	pass FX_PASS_BBOXES
	{
		VertexShader = compile vs_2_0 VSDrawBBoxes ();
		PixelShader = compile ps_2_0 PSDrawBBoxes ();
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

	pass FX_PASS_POLYBLEND
	{
		VertexShader = compile vs_2_0 VSPolyBlend ();
		PixelShader = compile ps_2_0 PSPolyBlend ();
	}

	pass FX_PASS_SKYSPHERE
	{
		VertexShader = compile vs_2_0 SkyCommonVS ();
		PixelShader = compile ps_2_0 SkySpherePS ();
	}

	pass FX_PASS_IQM_NOLUMA
	{
		VertexShader = compile vs_2_0 VSIQM ();
		PixelShader = compile ps_2_0 PSIQM_NoLuma ();
	}

	pass FX_PASS_IQM_LUMA
	{
		VertexShader = compile vs_2_0 VSIQM ();
		PixelShader = compile ps_2_0 PSIQM_Luma ();
	}

	pass FX_PASS_IQM_SHADOW
	{
		VertexShader = compile vs_2_0 VSIQM ();
		PixelShader = compile ps_2_0 PSIQM_Shadow ();
	}
	
	pass FX_PASS_BRIGHTFIELD
	{
		VertexShader = compile vs_2_0 VSBrightField ();
		PixelShader = compile ps_2_0 PSParticles ();
	}
	
	pass FX_PASS_BRIGHTFIELD_SQUARE
	{
		VertexShader = compile vs_2_0 VSBrightField ();
		PixelShader = compile ps_2_0 PSParticlesSquare ();
	}
	
	pass FX_PASS_PARTEFFECT
	{
		VertexShader = compile vs_2_0 VSParticleEffect ();
		PixelShader = compile ps_2_0 PSParticles ();
	}
	
	pass FX_PASS_PARTEFFECT_SQUARE
	{
		VertexShader = compile vs_2_0 VSParticleEffect ();
		PixelShader = compile ps_2_0 PSParticlesSquare ();
	}
}



