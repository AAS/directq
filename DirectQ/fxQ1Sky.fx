
float4x4 WorldMatrix;
float4x4 ProjMatrix;

texture solidLayer;
texture alphaLayer;


sampler solidMap = sampler_state
{
	texture = <solidLayer>;
	addressu = WRAP;
	addressv = WRAP;
};


sampler alphaMap = sampler_state
{
	texture = <alphaLayer>;
	addressu = WRAP;
	addressv = WRAP;
};


float Alpha;
float3 Scale;
float3 r_origin;


// this needs to mirror the layout of warppolyvert_t in gl_warp.cpp
struct VS_INPUT 
{
	float4 Position : POSITION0;
};


struct VS_OUTPUT 
{
	float4 Position : POSITION0;
	float3 Texcoord : TEXCOORD0;
};


float4 SkyPS (VS_OUTPUT Input) : COLOR0
{
	// same as classic Q1 warp but done per-pixel on the GPU instead
	Input.Texcoord = mul (Input.Texcoord, 6 * 63 / length (Input.Texcoord));
		
	float4 solidcolor = tex2D (solidMap, mul (Input.Texcoord + Scale.x, 0.0078125));
	float4 alphacolor = tex2D (alphaMap, mul (Input.Texcoord + Scale.y, 0.0078125));
	alphacolor.a *= Alpha;

	float4 color = (alphacolor * alphacolor.a) + (solidcolor * (1.0 - alphacolor.a));
	color.a = 1.0;
	
	return color;
}


VS_OUTPUT SkyVS (VS_INPUT Input)
{
	VS_OUTPUT Output;

	// this is friendlier for preshaders
	Output.Position = mul (Input.Position, mul (WorldMatrix, ProjMatrix));

	// use the untranslated input position as the output texcoord, shift by r_origin
	Output.Texcoord = Input.Position - r_origin;
	Output.Texcoord.z *= 3.0;
	return (Output);
}


technique SkyTech
{
	Pass P0
	{
		VertexShader = compile vs_2_0 SkyVS ();
		PixelShader = compile ps_2_0 SkyPS ();
	}
}

