float4x4 WorldProjMatrix;

texture particleTexture;

sampler baseMap = sampler_state
{
	texture = particleTexture;
	addressu = CLAMP;
	addressv = CLAMP;
};

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


float4 PSParticle (VS_OUTPUT Input) : COLOR0
{
	return tex2D (baseMap, Input.Texcoord) * Input.Color;
}


VS_OUTPUT VSParticle (VS_INPUT Input)
{
	VS_OUTPUT Output;

	Output.Position = mul (Input.Position, WorldProjMatrix);
	Output.Color = Input.Color;
	Output.Texcoord = Input.Texcoord;

	return (Output);
}


technique Particle
{
	pass ParticlePass
	{
		VertexShader = compile vs_2_0 VSParticle ();
		PixelShader = compile ps_2_0 PSParticle ();
	}
}



