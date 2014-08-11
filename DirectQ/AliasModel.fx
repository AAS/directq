float4x4 WorldProjMatrix;

texture baseTexture;
texture lumaTexture;

sampler baseMap = sampler_state
{
	texture = baseTexture;
	addressu = CLAMP;
	addressv = CLAMP;
};

sampler lumaMap = sampler_state
{
	texture = lumaTexture;
	addressu = CLAMP;
	addressv = CLAMP;
};

struct VS_INPUT 
{
	float4 Verts : POSITION0;
	float4 Color : COLOR0;
	float2 Tex : TEXCOORD0;
};

struct VS_OUTPUT 
{
	float4 Position : POSITION0;
	float4 Color : COLOR0;
	float2 Tex : TEXCOORD0;
};


float4 PSWithoutLuma (VS_OUTPUT Input) : COLOR0
{
	float4 texColor = tex2D (baseMap, Input.Tex);

	//return Input.Color * tex2D (baseMap, Input.Tex);
	return Input.Color * (texColor + (texColor * texColor));
}


float4 PSWithLuma (VS_OUTPUT Input) : COLOR0
{
	float4 texColor = tex2D (baseMap, Input.Tex);
	//float4 color = (Input.Color + tex2D (lumaMap, Input.Tex)) * tex2D (baseMap, Input.Tex);
	float4 color = (Input.Color + tex2D (lumaMap, Input.Tex)) * (texColor + (texColor * texColor));

	// get correct alpha from Input as it will be set wrong by the addition above
	color.a = Input.Color.a;

	return color;
}


VS_OUTPUT VSAlias (VS_INPUT Input)
{
	VS_OUTPUT Output;

	Output.Position = mul (Input.Verts, WorldProjMatrix);
	Output.Color = Input.Color;
	Output.Tex = Input.Tex;

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
