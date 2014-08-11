
float4x4 WorldMatrix;
float4x4 ProjMatrix;
float4x4 ViewMatrix;

texture baseTexture;
texture lumaTexture;
float colourscale;

sampler baseMap = sampler_state
{
	texture = baseTexture;
	addressu = WRAP;
	addressv = WRAP;
};

sampler lumaMap = sampler_state
{
	texture = lumaTexture;
	addressu = CLAMP;
	addressv = CLAMP;
};


// this needs to mirror the layout of glwarpvert_t in gl_warp.cpp
struct VS_INPUT 
{
	float4 Position : POSITION0;
	float4 Color : COLOR0;
	float2 Texcoord : TEXCOORD0;
};


struct VS_OUTPUT 
{
	float4 Position : POSITION0;
	float4 Color : COLOR0;
	float2 Texcoord : TEXCOORD0;
};


float4 PSWithoutLuma (VS_OUTPUT Input) : COLOR0
{
	return mul (tex2D (baseMap, Input.Texcoord), colourscale) * Input.Color;
}


float4 PSWithLuma (VS_OUTPUT Input) : COLOR0
{
	return mul (tex2D (baseMap, Input.Texcoord), colourscale) * (Input.Color + tex2D (lumaMap, Input.Texcoord));
}


VS_OUTPUT VSAlias (VS_INPUT Input)
{
	VS_OUTPUT Output;

	Output.Position = mul (Input.Position, WorldMatrix);
	Output.Position = mul (Output.Position, ViewMatrix);
	Output.Position = mul (Output.Position, ProjMatrix);
	Output.Color = Input.Color;
	Output.Texcoord = Input.Texcoord;

	return (Output);
}


technique Alias
{
	Pass NoLuma
	{
		VertexShader = compile vs_2_0 VSAlias ();
		PixelShader = compile ps_2_0 PSWithoutLuma ();
	}

	Pass Luma
	{
		VertexShader = compile vs_2_0 VSAlias ();
		PixelShader = compile ps_2_0 PSWithLuma ();
	}
}
