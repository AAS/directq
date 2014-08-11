float4x4 WorldProjMatrix;
float4x4 ModelTransform;
float warptime;

texture baseTexture;
texture lumaTexture;
float colourscale;

sampler baseMap = sampler_state
{
	texture = baseTexture;
	addressu = WRAP;
	addressv = WRAP;
};

sampler lumaMap = sampler_state
{
	texture = lumaTexture;
	addressu = WRAP;
	addressv = WRAP;
};

float3 Light;

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


float4 PSLiquid (VS_OUTPUT Input) : COLOR0
{
	float2 stwarp;
	float4 color;

	stwarp.x = Input.Texcoord.x + sin ((Input.Texcoord.y + warptime) / 4);
	stwarp.y = Input.Texcoord.y + sin ((Input.Texcoord.x + warptime) / 4);

	stwarp.xy = mul (stwarp.xy, 0.0625);
	color.rgb = tex2D (baseMap, stwarp);
	color.a = 1;

	return color;
}


float4 PSWithLuma (VS_OUTPUT Input) : COLOR0
{
	float4 color;
	color.rgb = Light.rgb;
	color.a = 1;

	float4 luma = tex2D (lumaMap, Input.Texcoord);

	// correct luma blend
	// note - color += luma compiles OK in RenderMonkey but not in the engine.
	// it works out as the same number of instructions anyway, so no big deal, just more code...
	color.r += luma.r;
	color.g += luma.g;
	color.b += luma.b;

	float4 texColor = tex2D (baseMap, Input.Texcoord);
	color += (texColor.r * texColor.r);
	color += (texColor.g * texColor.g);
	color += (texColor.b * texColor.b);
	color *= texColor;

	return mul (color, colourscale);
}


float4 PSWithoutLuma (VS_OUTPUT Input) : COLOR0
{
	float4 color;
	color.rgb = Light.rgb;
	color.a = 1;

	float4 texColor = tex2D (baseMap, Input.Texcoord);
	color += (texColor.r * texColor.r);
	color += (texColor.g * texColor.g);
	color += (texColor.b * texColor.b);
	color *= texColor;

	return mul (color, colourscale);
}


VS_OUTPUT VSCommon (VS_INPUT Input)
{
	VS_OUTPUT Output;

	Output.Position = mul (mul (Input.Position, ModelTransform), WorldProjMatrix);
	Output.Texcoord = Input.Texcoord;

	return (Output);
}


technique InstancedBrush
{
	pass WithLuma
	{
		VertexShader = compile vs_2_0 VSCommon ();
		PixelShader = compile ps_2_0 PSWithLuma ();
	}

	pass WithoutLuma
	{
		VertexShader = compile vs_2_0 VSCommon ();
		PixelShader = compile ps_2_0 PSWithoutLuma ();
	}

	pass Liquid
	{
		VertexShader = compile vs_2_0 VSCommon ();
		PixelShader = compile ps_2_0 PSLiquid ();
	}
}
