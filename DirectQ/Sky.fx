float4x4 WorldProjMatrix;

texture backTexture;

sampler backlayer = sampler_state
{
	texture = backTexture;
	addressu = WRAP;
	addressv = WRAP;
};

texture frontTexture;

sampler frontlayer = sampler_state
{
	texture = frontTexture;
	addressu = WRAP;
	addressv = WRAP;
};

float backscroll;
float frontscroll;


struct VS_0ST
{
	float4 Position : POSITION0;
};


struct VS_1ST
{
	float4 Position : POSITION0;
	float2 Texcoord0 : TEXCOORD0;
};


struct VS_2ST
{
	float4 Position : POSITION0;
	float2 Texcoord0 : TEXCOORD0;
	float2 Texcoord1 : TEXCOORD1;
};


VS_1ST VSDirectQWarp (VS_1ST Input)
{
	VS_1ST Output;
	Output.Position = mul (Input.Position, WorldProjMatrix);
	Output.Texcoord0 = Input.Texcoord0;
	return (Output);
}


float4 PSDirectQWarp (VS_1ST Input) : COLOR0
{
	return tex2D (backlayer, Input.Texcoord0);
}


VS_2ST VSClassicWarp (VS_1ST Input)
{
	VS_2ST Output;
	Output.Position = mul (Input.Position, WorldProjMatrix);
	Output.Texcoord0 = Input.Texcoord0;
	Output.Texcoord1 = Input.Texcoord0;

	Output.Texcoord0.x += backscroll;
	Output.Texcoord0.y += backscroll;

	Output.Texcoord1.x += frontscroll;
	Output.Texcoord1.y += frontscroll;

	return (Output);
}


float4 PSClassicWarp (VS_2ST Input) : COLOR0
{
	float4 BackColor = tex2D (backlayer, Input.Texcoord0);
	float4 FrontColor = tex2D (frontlayer, Input.Texcoord1);

	float4 color = (FrontColor * FrontColor.a) + (BackColor * (1.0 - FrontColor.a));

	color.a = 1.0;
	return color;
}


VS_0ST VSDepthClip (VS_0ST Input)
{
	VS_0ST Output;
	Output.Position = mul (Input.Position, WorldProjMatrix);
	return (Output);
}


technique SkyWarp
{
	pass DepthClipper
	{
		VertexShader = compile vs_2_0 VSDepthClip ();
	}

	pass ClassicWarp
	{
		VertexShader = compile vs_2_0 VSClassicWarp ();
		PixelShader = compile ps_2_0 PSClassicWarp ();
	}

	pass DirectQWarp
	{
		VertexShader = compile vs_2_0 VSDirectQWarp ();
		PixelShader = compile ps_2_0 PSDirectQWarp ();
	}

	pass SkyBoxWarp
	{
		// uses same shaders as directq warp but is coded as a separate
		// pass in case anyone wants to change either at any time...
		VertexShader = compile vs_2_0 VSDirectQWarp ();
		PixelShader = compile ps_2_0 PSDirectQWarp ();
	}
}
