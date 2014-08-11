float4x4 WorldProjMatrix;
float WarpTime;

texture baseTexture;

sampler baseMap = sampler_state
{
	texture = baseTexture;
	addressu = CLAMP;
	addressv = CLAMP;
};

float WarpScale;


struct VS_INPUT
{
	float4 Position : POSITION0;
	float2 Texcoord : TEXCOORD0;
};

struct VS_OUTPUT
{
	float4 Position : POSITION0;
	float2 Texcoord : TEXCOORD0;
};


float4 PSUnderwater (VS_OUTPUT Input) : COLOR0
{
	// basic texture lookup here
	return tex2D (baseMap, Input.Texcoord);
}


VS_OUTPUT VSUnderwater (VS_INPUT Input)
{
	VS_OUTPUT Output;

	Output.Position = mul (Input.Position, WorldProjMatrix);
	Output.Texcoord = Input.Texcoord;

	Output.Texcoord.x += sin (Input.Texcoord.y + WarpTime) * WarpScale;
	Output.Texcoord.y += sin (Input.Texcoord.x + WarpTime) * WarpScale;
	Output.Texcoord.xy = mul (Output.Texcoord.xy, 0.03125);

	return (Output);
}


technique Underwater
{
	pass P0
	{
		VertexShader = compile vs_2_0 VSUnderwater ();
		PixelShader = compile ps_2_0 PSUnderwater ();
	}
}
