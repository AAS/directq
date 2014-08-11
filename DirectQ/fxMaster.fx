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

texture tmu0Texture;
texture tmu1Texture;
texture tmu2Texture;

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

float4 FogColor;
float FogDensity;

samplerCUBE cubeSampler = sampler_state
{
	Texture = <tmu0Texture>;

	AddressU = <address0>;
	AddressV = <address0>;
	AddressW = <address0>;

	MagFilter = <magfilter0>;
	MinFilter = <minfilter0>;
	MipFilter = <mipfilter0>;

	MaxAnisotropy = <aniso0>;
};


sampler tmu0Sampler = sampler_state
{
	Texture = <tmu0Texture>;

	AddressU = <address0>;
	AddressV = <address0>;
	AddressW = <address0>;

	MagFilter = <magfilter0>;
	MinFilter = <minfilter0>;
	MipFilter = <mipfilter0>;

	MaxAnisotropy = <aniso0>;
};

sampler tmu1Sampler = sampler_state
{
	Texture = <tmu1Texture>;

	AddressU = <address1>;
	AddressV = <address1>;
	AddressW = <address1>;

	MagFilter = <magfilter1>;
	MinFilter = <minfilter1>;
	MipFilter = <mipfilter1>;

	MaxAnisotropy = <aniso1>;
};

sampler tmu2Sampler = sampler_state
{
	Texture = <tmu2Texture>;

	AddressU = <address2>;
	AddressV = <address2>;
	AddressW = <address2>;

	MagFilter = <magfilter2>;
	MinFilter = <minfilter2>;
	MipFilter = <mipfilter2>;

	MaxAnisotropy = <aniso2>;
};

struct WorldVert
{
	float4 Position : POSITION0;
	float2 Tex0 : TEXCOORD0;
	float2 Tex1 : TEXCOORD1;
};

struct WorldVertPS
{
	float4 Position : POSITION0;
	float2 Tex0 : TEXCOORD0;
	float2 Tex1 : TEXCOORD1;
#ifdef hlsl_fog
	float4 FogPosition : TEXCOORD2;
#endif
};

struct VertLiquidInput
{
	float4 Position : POSITION0;
	float2 Texcoord : TEXCOORD0;
};

struct VertSkyInput
{
	float4 Position : POSITION0;
	float3 Texcoord : TEXCOORD0;
};

struct VertLiquidOutput
{
	float4 Position : POSITION0;
	float2 Texcoord0 : TEXCOORD0;
	float2 Texcoord1 : TEXCOORD1;
#ifdef hlsl_fog
	float4 FogPosition : TEXCOORD2;
#endif
};

struct VertTexturedColoured
{
	float4 Position : POSITION0;
	float4 Color : COLOR0;
	float2 Tex0 : TEXCOORD0;
};

struct VertAlias
{
	float4 Position : POSITION0;
	float4 Color : COLOR0;
	float2 Tex0 : TEXCOORD0;
	
#ifdef hlsl_fog
	float4 FogPosition : TEXCOORD1;
#endif
};

struct VertPositionOnly
{
	float4 Position : POSITION0;
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
ALIAS MODELS
====================
*/
float2 currlerp;
float2 lastlerp;
float3 ShadeVector;
float3 ShadeLight;
float3 AmbientLight;
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


float4 EvalAliasLight (VertAliasPS Input)
{
	/*
	float4 ShadeDot = tex2D (tmu2Sampler, float2 (dot (Normal, ShadeVector) * -1.0f, 0.0f));
	return float4 (AmbientLight + ShadeDot.xyz * ShadeLight, 1.0f);
	*/

	return float4 (ShadeLight * dot (Input.Normal, ShadeVector) + AmbientLight, 1.0f);
}


float4 PSAliasLuma (VertAliasPS Input) : COLOR0
{
	float4 ShadeColor = EvalAliasLight (Input);
	// return ShadeColor;

#ifdef hlsl_fog
	float4 fullbright = tex2D (tmu1Sampler, Input.Tex0) * 0.5;
	float4 color = ((tex2D (tmu0Sampler, Input.Tex0) * ShadeColor) * Overbright);
	color = FogCalc (color + fullbright, Input.FogPosition) + fullbright;
#else
	float4 color = ((tex2D (tmu0Sampler, Input.Tex0) * ShadeColor) * Overbright) + tex2D (tmu1Sampler, Input.Tex0);
#endif

	color.a = AlphaVal;
	return color;
}


float4 PSAliasNoLuma (VertAliasPS Input) : COLOR0
{
	float4 ShadeColor = EvalAliasLight (Input);
	float4 color = (tex2D (tmu0Sampler, Input.Tex0) * ShadeColor) * Overbright;

#ifdef hlsl_fog
	color = FogCalc (color, Input.FogPosition);
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

	// switch lighting to per-pixel and interpolate the two sets of normals in the vertex shader
	// let HLSL optimize this as a mad instruction.
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

float4 PSParticles (VertAlias Input) : COLOR0
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


VertAlias VSParticles (VertTexturedColoured Input)
{
	VertAlias Output;

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

VertAlias VSParticlesInstanced (VertParticleInstanced Input)
{
	VertAlias Output;

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


float4 LiquidPS (VertLiquidOutput Input) : COLOR0
{
	// same warp calculation as is used for the fixed pipeline path
	// a lot of the heavier lifting here has been offloaded to the vs
	float4 color = tex2D (tmu0Sampler, mul (mul (sin (Input.Texcoord1), warpscale) + Input.Texcoord0, 0.015625f));
#ifdef hlsl_fog
	color = FogCalc (color, Input.FogPosition);
#endif
	color.a = AlphaVal;

	return color;
}


VertLiquidOutput LiquidVS (VertLiquidInput Input)
{
	VertLiquidOutput Output;

	// this is friendlier for preshaders
	Output.Position = mul (Input.Position, WorldMatrix);
#ifdef hlsl_fog
	Output.FogPosition = mul (Input.Position, ModelViewMatrix);
#endif
	Output.Texcoord0 = Input.Texcoord;

	// we need to switch x and y at some stage; logic says lets do it per vertex to get the ps instructions down
	Output.Texcoord1.x = mul (Input.Texcoord.y, warpfactor);
	Output.Texcoord1.y = mul (Input.Texcoord.x, warpfactor);

	// keeping vs instructions down is good as well, of course.
	Output.Texcoord1 = mul (Output.Texcoord1 + warptime, 0.024543f);

	return (Output);
}


/*
====================
WORLD MODEL
====================
*/

float4 PSWorldNoLuma (WorldVertPS Input) : COLOR0
{
	float4 texcolor = tex2D (tmu1Sampler, Input.Tex0);
	float4 color = texcolor * (tex2D (tmu0Sampler, Input.Tex1) * Overbright);
#ifdef hlsl_fog
	color = FogCalc (color, Input.FogPosition);
#endif
	
	return color;
}


float4 PSWorldNoLumaAlpha (WorldVertPS Input) : COLOR0
{
	float4 texcolor = tex2D (tmu1Sampler, Input.Tex0);
	float4 color = texcolor * (tex2D (tmu0Sampler, Input.Tex1) * Overbright);
#ifdef hlsl_fog
	color = FogCalc (color, Input.FogPosition);
#endif
	color.a = AlphaVal * texcolor.a;
	
	return color;
}


float4 PSWorldLuma (WorldVertPS Input) : COLOR0
{
	float4 texcolor = tex2D (tmu1Sampler, Input.Tex0);
#ifdef hlsl_fog
	float4 fullbright = tex2D (tmu2Sampler, Input.Tex0) * 0.5;
	float4 color = (texcolor * tex2D (tmu0Sampler, Input.Tex1) * Overbright);
	color = FogCalc (color + fullbright, Input.FogPosition) + fullbright;
#else
	float4 color = (texcolor * tex2D (tmu0Sampler, Input.Tex1) * Overbright) + tex2D (tmu2Sampler, Input.Tex0);
#endif
	
	return color;
}


float4 PSWorldLumaAlpha (WorldVertPS Input) : COLOR0
{
	float4 texcolor = tex2D (tmu1Sampler, Input.Tex0);
#ifdef hlsl_fog
	float4 fullbright = tex2D (tmu2Sampler, Input.Tex0) * 0.5;
	float4 color = (texcolor * tex2D (tmu0Sampler, Input.Tex1) * Overbright);
	color = FogCalc (color + fullbright, Input.FogPosition) + fullbright;
#else
	float4 color = (texcolor * tex2D (tmu0Sampler, Input.Tex1) * Overbright) + tex2D (tmu2Sampler, Input.Tex0);
#endif
	color.a = AlphaVal * texcolor.a;
	
	return color;
}


WorldVertPS VSWorldCommon (WorldVert Input)
{
	WorldVertPS Output;

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

float4 SkyWarpPS (VertSkyInput Input) : COLOR0
{
	// same as classic Q1 warp but done per-pixel on the GPU instead
	Input.Texcoord = mul (Input.Texcoord, 6 * 63 / length (Input.Texcoord));
		
	float4 solidcolor = tex2D (tmu0Sampler, mul (Input.Texcoord.xy + Scale.x, 0.0078125));
	float4 alphacolor = tex2D (tmu1Sampler, mul (Input.Texcoord.xy + Scale.y, 0.0078125));
	alphacolor.a *= AlphaVal;

	float4 color = (alphacolor * alphacolor.a) + (solidcolor * (1.0 - alphacolor.a));
	color.a = 1.0;
#ifdef hlsl_fog
	return lerp (FogColor, color, SkyFog);
#else
	return color;
#endif
}


VertSkyInput SkyWarpVS (VertPositionOnly Input)
{
	VertSkyInput Output;

	// this is friendlier for preshaders
	Output.Position = mul (Input.Position, WorldMatrix);

	// use the untranslated input position as the output texcoord, shift by r_origin
	Output.Texcoord = Input.Position.xyz - r_origin;
	Output.Texcoord.z *= 3.0;
	return (Output);
}


VertSkyInput SkyBoxVS (VertPositionOnly Input)
{
	VertSkyInput Output;

	// this is friendlier for preshaders
	Output.Position = mul (Input.Position, WorldMatrix);

	// use the untranslated input position as the output texcoord, shift by r_origin
	Output.Texcoord = Input.Position.xyz - r_origin;
	return (Output);
}


float4 SkyBoxPS (VertSkyInput Input) : COLOR0
{
	float4 color = texCUBE (cubeSampler, Input.Texcoord);
	color.a = 1.0;
#ifdef hlsl_fog
	return lerp (FogColor, color, SkyFog);
#else
	return color;
#endif
}


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
}



