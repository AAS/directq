float4x4 WorldProjMatrix;

texture baseTexture;

sampler baseMap = sampler_state
{
	texture = baseTexture;
	addressu = CLAMP;
	addressv = CLAMP;
};

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


float4 PSSprite (VS_OUTPUT Input) : COLOR0
{
	return tex2D (baseMap, Input.Texcoord);
}


VS_OUTPUT VSSprite (VS_INPUT Input)
{
	VS_OUTPUT Output;

	Output.Position = mul (Input.Position, WorldProjMatrix);
	Output.Texcoord = Input.Texcoord;

	return (Output);
}


technique Sprite
{
	pass SpritePass
	{
		VertexShader = compile vs_2_0 VSSprite ();
		PixelShader = compile ps_2_0 PSSprite ();
	}
}
