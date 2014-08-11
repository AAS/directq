float4x4 WorldProjMatrix;

texture baseTexture;

sampler baseMap = sampler_state
{
	texture = baseTexture;
	addressu = WRAP;
	addressv = WRAP;
};

float warptime;
float Alpha;

struct VS_INPUT 
{
	float4 Position : POSITION0;
	float2 Texcoord : TEXCOORD0;
	float2 Warpcoord : TEXCOORD1;
};

struct VS_OUTPUT 
{
	float4 Position : POSITION0;
	float2 Texcoord : TEXCOORD0;
	float2 Warpcoord : TEXCOORD1;
};


float4 LiquidPS (VS_OUTPUT Input) : COLOR0
{
	float2 stwarp = Input.Texcoord;
	float4 color;

	stwarp.x += sin (Input.Warpcoord.y + warptime);
	stwarp.y += sin (Input.Warpcoord.x + warptime);

	stwarp.xy = mul (stwarp.xy, 0.0625);
	color.rgb = tex2D (baseMap, stwarp);
	color.a = Alpha;

	return color;
}


VS_OUTPUT LiquidVS (VS_INPUT Input)
{
	VS_OUTPUT Output;

	Output.Position = mul (Input.Position, WorldProjMatrix);
	Output.Texcoord = Input.Texcoord;
	Output.Warpcoord = Input.Warpcoord;

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

