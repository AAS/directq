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

texture tmu0Texture;
texture tmu1Texture;
texture tmu2Texture;

float warptime;
float warpscale;
float warpfactor;
float Overbright;
float AlphaVal;
float SkyFog;

int texfilter0, texfilter1, texfilter2;
int mipfilter0, mipfilter1, mipfilter2;
int address0, address1, address2;

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

	MagFilter = <texfilter0>;
	MinFilter = <texfilter0>;
	MipFilter = <mipfilter0>;
};


sampler tmu0Sampler = sampler_state
{
	Texture = <tmu0Texture>;

	AddressU = <address0>;
	AddressV = <address0>;
	AddressW = <address0>;

	MagFilter = <texfilter0>;
	MinFilter = <texfilter0>;
	MipFilter = <mipfilter0>;
};

sampler tmu1Sampler = sampler_state
{
	Texture = <tmu1Texture>;

	AddressU = <address1>;
	AddressV = <address1>;
	AddressW = <address1>;

	MagFilter = <texfilter1>;
	MinFilter = <texfilter1>;
	MipFilter = <mipfilter1>;
};

sampler tmu2Sampler = sampler_state
{
	Texture = <tmu2Texture>;

	AddressU = <address2>;
	AddressV = <address2>;
	AddressW = <address2>;

	MagFilter = <texfilter2>;
	MinFilter = <texfilter2>;
	MipFilter = <mipfilter2>;
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
	float4 FogPosition : TEXCOORD2;
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
	float4 FogPosition : TEXCOORD2;
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
	float4 FogPosition : TEXCOORD1;
};

struct VertPositionOnly
{
	float4 Position : POSITION0;
};

struct VertColoured
{
	float4 Position : POSITION0;
	float4 Color : COLOR0;
};


float4 FogCalc (float4 color, float4 fogpos)
{
	float fogdist = length (fogpos);
	float fogfactor = clamp (exp2 (-FogDensity * FogDensity * fogdist * fogdist * 1.442695), 0.0, 1.0);
	return lerp (FogColor, color, fogfactor);
}


float4 PSAliasNoLuma (VertAlias Input) : COLOR0
{
	float4 color = (tex2D (tmu0Sampler, Input.Tex0) * Input.Color) * Overbright;
	color = FogCalc (color, Input.FogPosition);
	color.a = Input.Color.a;

	return color;
}


float4 PSTexturedColouredAlpha (VertTexturedColoured Input) : COLOR0
{
	return tex2D (tmu0Sampler, Input.Tex0) * Input.Color;
}


float4 PSParticles (VertAlias Input) : COLOR0
{
	float4 texcolor = tex2D (tmu0Sampler, Input.Tex0);
	float4 color = FogCalc (texcolor * Input.Color, Input.FogPosition);
	color.a = texcolor.a;
	return color;
}


float4 PSAliasLuma (VertAlias Input) : COLOR0
{
	float4 fullbright = tex2D (tmu1Sampler, Input.Tex0) * 0.5;
	float4 color = ((tex2D (tmu0Sampler, Input.Tex0) * Input.Color) * Overbright);
	color = FogCalc (color + fullbright, Input.FogPosition) + fullbright;
	color.a = Input.Color.a;

	return color;
}


float4 PSRTTBlend (VertTexturedColoured Input) : COLOR0
{
	// fixme - is this the wrong way around???
	return lerp (tex2D (tmu0Sampler, Input.Tex0), Input.Color, (1.0f - Input.Color.a));
}


VertTexturedColoured VSTexturedColouredCommon (VertTexturedColoured Input)
{
	VertTexturedColoured Output;

	// this is friendlier for preshaders
	Output.Position = mul (Input.Position, WorldMatrix);

	Output.Color = Input.Color;
	Output.Tex0 = Input.Tex0;

	return Output;
}


VertAlias VSAliasCommon (VertTexturedColoured Input)
{
	VertAlias Output;

	// this is friendlier for preshaders
	Output.Position = mul (Input.Position, WorldMatrix);
	Output.FogPosition = mul (Input.Position, ModelViewMatrix);

	Output.Color = Input.Color;
	Output.Tex0 = Input.Tex0;

	return Output;
}


VertTexturedColoured VSParticles (VertTexturedColoured Input)
{
	VertTexturedColoured Output;

	// this is friendlier for preshaders
	Output.Position = mul (Input.Position, WorldMatrix);

	Output.Color = Input.Color;
	Output.Tex0 = Input.Tex0;

	return Output;
}


float4 LiquidPS (VertLiquidOutput Input) : COLOR0
{
	// same warp calculation as is used for the fixed pipeline path
	// a lot of the heavier lifting here has been offloaded to the vs
	float4 color = tex2D (tmu0Sampler, mul (mul (sin (Input.Texcoord1), warpscale) + Input.Texcoord0, 0.015625f));
	color = FogCalc (color, Input.FogPosition);
	color.a = AlphaVal;

	return color;
}


VertLiquidOutput LiquidVS (VertLiquidInput Input)
{
	VertLiquidOutput Output;

	// this is friendlier for preshaders
	Output.Position = mul (Input.Position, WorldMatrix);
	Output.FogPosition = mul (Input.Position, ModelViewMatrix);
	Output.Texcoord0 = Input.Texcoord;

	// we need to switch x and y at some stage; logic says lets do it per vertex to get the ps instructions down
	Output.Texcoord1.x = mul (Input.Texcoord.y, warpfactor);
	Output.Texcoord1.y = mul (Input.Texcoord.x, warpfactor);

	// keeping vs instructions down is good as well, of course.
	Output.Texcoord1 = mul (Output.Texcoord1 + warptime, 0.024543f);

	return (Output);
}


VertColoured ColoredVS (VertColoured Input)
{
	VertColoured Output;
	
	Output.Position = mul (Input.Position, WorldMatrix);
	Output.Color = Input.Color;

	return (Output);
}


VertColoured ShadowVS (VertPositionOnly Input)
{
	VertColoured Output;
	
	Output.Position = mul (Input.Position, WorldMatrix);
	
	Output.Color.r = 0;
	Output.Color.g = 0;
	Output.Color.b = 0;
	Output.Color.a = AlphaVal;
	
	return (Output);
}


float4 ShadowPS (VertColoured Input) : COLOR0
{
	return Input.Color;
}


float4 PSWorldNoLuma (WorldVertPS Input) : COLOR0
{
	float4 color = tex2D (tmu1Sampler, Input.Tex0) * tex2D (tmu0Sampler, Input.Tex1) * Overbright;
	color = FogCalc (color, Input.FogPosition);
	color.a = AlphaVal;
	
	return color;
}


float4 PSWorldLuma (WorldVertPS Input) : COLOR0
{
	float4 fullbright = tex2D (tmu2Sampler, Input.Tex0) * 0.5;
	float4 color = (tex2D (tmu1Sampler, Input.Tex0) * tex2D (tmu0Sampler, Input.Tex1) * Overbright);
	color = FogCalc (color + fullbright, Input.FogPosition) + fullbright;
	color.a = AlphaVal;
	
	return color;
}


float4 PSWorldLumaNoLuma (WorldVertPS Input) : COLOR0
{
	float4 color = (tex2D (tmu1Sampler, Input.Tex0) + tex2D (tmu2Sampler, Input.Tex0)) * tex2D (tmu0Sampler, Input.Tex1) * Overbright;
	color.a = AlphaVal;
	return color;
}


WorldVertPS VSWorldCommon (WorldVert Input)
{
	WorldVertPS Output;

	// this is friendlier for preshaders
	Output.Position = mul (Input.Position, WorldMatrix);
	Output.FogPosition = mul (Input.Position, ModelViewMatrix);

	Output.Tex0 = Input.Tex0;
	Output.Tex1 = Input.Tex1;

	return Output;
}


float4 SkyWarpPS (VertSkyInput Input) : COLOR0
{
	// same as classic Q1 warp but done per-pixel on the GPU instead
	Input.Texcoord = mul (Input.Texcoord, 6 * 63 / length (Input.Texcoord));
		
	float4 solidcolor = tex2D (tmu0Sampler, mul (Input.Texcoord.xy + Scale.x, 0.0078125));
	float4 alphacolor = tex2D (tmu1Sampler, mul (Input.Texcoord.xy + Scale.y, 0.0078125));
	alphacolor.a *= AlphaVal;

	float4 color = (alphacolor * alphacolor.a) + (solidcolor * (1.0 - alphacolor.a));
	color.a = 1.0;
	
	return lerp (FogColor, color, SkyFog);
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
	return lerp (FogColor, color, SkyFog);
}


technique MasterRefresh
{
	pass PAliasNoLuma
	{
		// textured, coloured, no luma, usable for alias models only owing to overbrights
		VertexShader = compile vs_2_0 VSAliasCommon ();
		PixelShader = compile ps_2_0 PSAliasNoLuma ();
	}

	pass PAliasLuma
	{
		// textured, coloured, with luma, alias models only
		VertexShader = compile vs_2_0 VSAliasCommon ();
		PixelShader = compile ps_2_0 PSAliasLuma ();
	}

	pass PGeneric
	{
		// textured, coloured, no luma, no overbrights, blends texture alpha
		VertexShader = compile vs_2_0 VSTexturedColouredCommon ();
		PixelShader = compile ps_2_0 PSTexturedColouredAlpha ();
	}

	pass PLiquid
	{
		// liquid textures
		VertexShader = compile vs_2_0 LiquidVS ();
		PixelShader = compile ps_2_0 LiquidPS ();
	}

	pass PShadowed
	{
		// shadows and occlusions
		VertexShader = compile vs_2_0 ShadowVS ();
		PixelShader = compile ps_2_0 ShadowPS ();
	}

	pass PWorldNoLuma
	{
		// shadows and occlusions
		VertexShader = compile vs_2_0 VSWorldCommon ();
		PixelShader = compile ps_2_0 PSWorldNoLuma ();
	}

	pass PWorldLuma
	{
		VertexShader = compile vs_2_0 VSWorldCommon ();
		PixelShader = compile ps_2_0 PSWorldLuma ();
	}

	pass PRTT
	{
		// textured, coloured, no luma, no overbrights, blends texture alpha
		VertexShader = compile vs_2_0 VSTexturedColouredCommon ();
		PixelShader = compile ps_2_0 PSRTTBlend ();
	}

	pass PSkyWarp
	{
		VertexShader = compile vs_2_0 SkyWarpVS ();
		PixelShader = compile ps_2_0 SkyWarpPS ();
	}
	
	pass PDrawTextured
	{
		VertexShader = compile vs_2_0 VSTexturedColouredCommon ();
		PixelShader = compile ps_2_0 PSTexturedColouredAlpha ();
	}
	
	pass PDrawColored
	{
		VertexShader = compile vs_2_0 ColoredVS ();
		PixelShader = compile ps_2_0 ShadowPS ();
	}
	
	pass PSkybox
	{
		VertexShader = compile vs_2_0 SkyBoxVS ();
		PixelShader = compile ps_2_0 SkyBoxPS ();
	}
	
	pass PWorldLumaNoLuma
	{
		VertexShader = compile vs_2_0 VSWorldCommon ();
		PixelShader = compile ps_2_0 PSWorldLumaNoLuma ();
	}

	pass PAliasLumaNoLuma
	{
		// textured, coloured, with luma, alias models only
		VertexShader = compile vs_2_0 VSAliasCommon ();
		PixelShader = compile ps_2_0 PSAliasLuma ();
	}

	pass PParticles
	{
		// particles!
		//VertexShader = compile vs_2_0 VSParticles ();
		VertexShader = compile vs_2_0 VSAliasCommon ();
		PixelShader = compile ps_2_0 PSParticles ();
		//PixelShader = compile ps_2_0 PSAliasNoLuma ();
	}
}
