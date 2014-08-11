
float4x4 WorldMatrix;
float4x4 ProjMatrix;

texture baseTexture;


sampler baseMap = sampler_state
{
	texture = <baseTexture>;
	addressu = WRAP;
	addressv = WRAP;
};


float warptime;
float warpscale;
float warpfactor;
float Alpha;


struct VS_INPUT
{
	float4 Position : POSITION0;
	float2 Texcoord : TEXCOORD0;
};


struct VS_OUTPUT
{
	float4 Position : POSITION0;
	float2 Texcoord0 : TEXCOORD0;
	float2 Texcoord1 : TEXCOORD1;
};


float4 LiquidPS (VS_OUTPUT Input) : COLOR0
{
	// same warp calculation as is used for the fixed pipeline path
	// a lot of the heavier lifting here has been offloaded to the vs
	float4 color = tex2D (baseMap, mul (mul (sin (Input.Texcoord1), warpscale) + Input.Texcoord0, 0.015625f));
	color.a = Alpha;

	return color;
}


VS_OUTPUT LiquidVS (VS_INPUT Input)
{
	VS_OUTPUT Output;

	// this is friendlier for preshaders
	Output.Position = mul (Input.Position, mul (WorldMatrix, ProjMatrix));
	Output.Texcoord0 = Input.Texcoord;

	// we need to switch x and y at some stage; logic says lets do it per vertex to get the ps instructions down
	Output.Texcoord1.x = mul (Input.Texcoord.y, warpfactor);
	Output.Texcoord1.y = mul (Input.Texcoord.x, warpfactor);

	// keeping vs instructions down is good as well, of course.
	Output.Texcoord1 = mul (Output.Texcoord1 + warptime, 0.024543f);

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

