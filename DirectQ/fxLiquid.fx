
float4x4 WorldMatrix;
float4x4 ProjMatrix;
float4x4 ViewMatrix;

texture baseTexture;


sampler baseMap = sampler_state
{
	texture = baseTexture;
	addressu = WRAP;
	addressv = WRAP;
};


float warptime;
float warpscale;
float warpfactor;
float Alpha;


// this needs to mirror the layout of glwarpvert_t in gl_warp.cpp
struct VS_INPUT 
{
	float4 Position1 : POSITION0;
	float2 Texcoord1 : TEXCOORD0;
	float4 Position2 : POSITION1;
	float2 Texcoord2 : TEXCOORD1;
};


struct VS_OUTPUT 
{
	float4 Position : POSITION0;
	float2 Texcoord : TEXCOORD0;
};


float4 LiquidPS (VS_OUTPUT Input) : COLOR0
{
	float2 stwarp = Input.Texcoord;
	float4 color;

	// same warp calculation as is used for the fixed pipeline path
	stwarp.x += sin (((Input.Texcoord.y * warpfactor) + warptime) * 0.024543f) * warpscale;
	stwarp.y += sin (((Input.Texcoord.x * warpfactor) + warptime) * 0.024543f) * warpscale;

	stwarp.xy = mul (stwarp.xy, 0.015625);

	color.rgb = tex2D (baseMap, stwarp);
	color.a = Alpha;

	return color;
}


VS_OUTPUT LiquidVS (VS_INPUT Input)
{
	VS_OUTPUT Output;

	Output.Position = mul (Input.Position1, WorldMatrix);
	Output.Position = mul (Output.Position, ViewMatrix);
	Output.Position = mul (Output.Position, ProjMatrix);
	Output.Texcoord = Input.Texcoord1;

	return (Output);
}


technique Liquid
{
	Pass P0
	{
		VertexShader = compile vs_2_0 LiquidVS ();
		PixelShader = compile ps_2_0 LiquidPS ();
	}
}

