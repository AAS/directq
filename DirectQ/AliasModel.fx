float4x4 WorldProjMatrix;

texture baseTexture;
texture lumaTexture;
float colourscale;

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
	float4 Verts1 : POSITION0;
	float4 Verts2 : POSITION1;
	float4 Color : COLOR0;
	float2 Tex : TEXCOORD0;
	float2 Lerp : TEXCOORD1;
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
	return mul (Input.Color * (texColor + (texColor * texColor)), colourscale);
}


float4 PSWithLuma (VS_OUTPUT Input) : COLOR0
{
	float4 texColor = tex2D (baseMap, Input.Tex);
	//float4 color = (Input.Color + tex2D (lumaMap, Input.Tex)) * tex2D (baseMap, Input.Tex);
	float4 color = (Input.Color + tex2D (lumaMap, Input.Tex)) * (texColor + (texColor * texColor));

	// get correct alpha from Input as it will be set wrong by the addition above
	color.a = Input.Color.a;

	return mul (color, colourscale);
}


VS_OUTPUT VSAlias (VS_INPUT Input)
{
	VS_OUTPUT Output;
	float4 LerpPosition;

	LerpPosition.x = Input.Verts1.x * Input.Lerp.x + Input.Verts2.x * Input.Lerp.y;
	LerpPosition.y = Input.Verts1.y * Input.Lerp.x + Input.Verts2.y * Input.Lerp.y;
	LerpPosition.z = Input.Verts1.z * Input.Lerp.x + Input.Verts2.z * Input.Lerp.y;
	LerpPosition.w = 1;

	Output.Position = mul (LerpPosition, WorldProjMatrix);

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
