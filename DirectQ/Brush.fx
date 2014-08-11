float4x4 WorldProjMatrix;
float4x4 ModelTransform;
float LightScale;

texture baseTexture;
texture lightTexture;
texture lumaTexture;

sampler baseMap = sampler_state
{
	texture = baseTexture;
	addressu = WRAP;
	addressv = WRAP;
};

sampler lightMap = sampler_state
{
	texture = lightTexture;
	addressu = CLAMP;
	addressv = CLAMP;
};

sampler lumaMap = sampler_state
{
	texture = lumaTexture;
	addressu = WRAP;
	addressv = WRAP;
};


struct VS_INPUT
{
	float4 Position : POSITION0;
	float2 Texcoord0 : TEXCOORD0;
	float2 Texcoord1 : TEXCOORD1;
};

struct VS_OUTPUT
{
	float4 Position : POSITION0;
	float2 Texcoord0 : TEXCOORD0;
	float2 Texcoord1 : TEXCOORD1;
};


float4 PSWithLuma (VS_OUTPUT Input) : COLOR0
{
	float4 color = tex2D (lightMap, Input.Texcoord1);
	float4 luma = tex2D (lumaMap, Input.Texcoord0);

	// correct luma blend
	// note - color += luma compiles OK in RenderMonkey but not in the engine.
	// it works out as the same number of instructions anyway, so no big deal, just more code...
	color.r += luma.r;
	color.g += luma.g;
	color.b += luma.b;
	color *= tex2D (baseMap, Input.Texcoord0) * LightScale;

	return color;
}


float4 PSWithoutLuma (VS_OUTPUT Input) : COLOR0
{
	float4 color = tex2D (lightMap, Input.Texcoord1);

	color *= tex2D (baseMap, Input.Texcoord0) * LightScale;

	return color;
}


VS_OUTPUT VSBrush (VS_INPUT Input)
{
	VS_OUTPUT Output;

	Output.Position = mul (mul (Input.Position, ModelTransform), WorldProjMatrix);
	Output.Texcoord0 = Input.Texcoord0;
	Output.Texcoord1 = Input.Texcoord1;

	return (Output);
}


technique Brush
{
	pass NoLuma
	{
		VertexShader = compile vs_2_0 VSBrush ();
		PixelShader = compile ps_2_0 PSWithoutLuma ();
	}

	pass Luma
	{
		VertexShader = compile vs_2_0 VSBrush ();
		PixelShader = compile ps_2_0 PSWithLuma ();
	}
}
