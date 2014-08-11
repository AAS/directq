float4x4 WorldProjMatrix;

texture baseTexture;

sampler baseMap = sampler_state
{
	texture = baseTexture;
	addressu = CLAMP;
	addressv = CLAMP;
};

float4 BaseColor;

struct VS_INPUTOUTPUTTEXTURED
{
	float4 Position : POSITION0;
	float2 Texcoord : TEXCOORD0;
};

struct VS_INPUTOUTPUTUNTEXTURED
{
	float4 Position : POSITION0;
};


float4 PSFlat2DTextured (VS_INPUTOUTPUTTEXTURED Input) : COLOR0
{
	return tex2D (baseMap, Input.Texcoord);
}


float4 PSFlat2DUntextured (VS_INPUTOUTPUTUNTEXTURED Input) : COLOR0
{
	return BaseColor;
}


float4 PSFlat2DTexturedWithColor (VS_INPUTOUTPUTTEXTURED Input) : COLOR0
{
	return tex2D (baseMap, Input.Texcoord) * BaseColor;
}


VS_INPUTOUTPUTUNTEXTURED VSFlat2DUntextured (VS_INPUTOUTPUTUNTEXTURED Input)
{
	VS_INPUTOUTPUTUNTEXTURED Output;

	Output.Position = mul (Input.Position, WorldProjMatrix);

	return (Output);
}


VS_INPUTOUTPUTTEXTURED VSFlat2DTextured (VS_INPUTOUTPUTTEXTURED Input)
{
	VS_INPUTOUTPUTTEXTURED Output;

	Output.Position = mul (Input.Position, WorldProjMatrix);
	Output.Texcoord = Input.Texcoord;

	return (Output);
}


technique Flat2D
{
	pass Textured
	{
		VertexShader = compile vs_2_0 VSFlat2DTextured ();
		PixelShader = compile ps_2_0 PSFlat2DTextured ();
	}
	
	pass Untextured
	{
		VertexShader = compile vs_2_0 VSFlat2DUntextured ();
		PixelShader = compile ps_2_0 PSFlat2DUntextured ();
	}
	
	pass TexturedWithColor
	{
		VertexShader = compile vs_2_0 VSFlat2DTextured ();
		PixelShader = compile ps_2_0 PSFlat2DTexturedWithColor ();
	}
}
