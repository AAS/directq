
float4x4 WorldMatrix;
float4x4 ProjMatrix;

texture baseTexture;


sampler baseMap = sampler_state
{
	texture = <baseTexture>;
	addressu = WRAP;
	addressv = WRAP;
};


float warptime;
float warpscale;
float warpfactor;
float Alpha;


struct VS_INPUT_OUTPUT 
{
	float4 Position : POSITION0;
	float2 Texcoord : TEXCOORD0;
};


float4 LiquidPS (VS_INPUT_OUTPUT Input) : COLOR0
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


VS_INPUT_OUTPUT LiquidVS (VS_INPUT_OUTPUT Input)
{
	VS_INPUT_OUTPUT Output;

	// this is friendlier for preshaders
	Output.Position = mul (Input.Position, mul (WorldMatrix, ProjMatrix));
	Output.Texcoord = Input.Texcoord;

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

