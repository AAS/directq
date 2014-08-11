
float4x4 WorldMatrix;
float4x4 ProjMatrix;

texture ScreenTexture;
float warpscale;
float4 polyblend;

sampler SrcColor = sampler_state
{
	texture = <ScreenTexture>;
	addressu = CLAMP;
	addressv = CLAMP;
};


struct ScreenVert
{
	// 2 sets of texcoords needed for the warp; it's only 4 verts so no big deal on the overhead
	float4 Position : POSITION0;
	float2 Tex0 : TEXCOORD0;
	float2 Tex1 : TEXCOORD1;
};


float4 PSPassThru (ScreenVert Input) : COLOR0
{
	return tex2D (SrcColor, Input.Tex0);
}


float4 PSBlendLerp (ScreenVert Input) : COLOR0
{
	return lerp (tex2D (SrcColor, Input.Tex0), polyblend, polyblend.a);
}


float4 PSUnderwaterPostProcess (ScreenVert Input) : COLOR0
{
	// that's a little more like it!
	return lerp (tex2D (SrcColor, Input.Tex0 + sin (Input.Tex1) * warpscale), polyblend, polyblend.a);
}


ScreenVert VSCommon (ScreenVert Input)
{
	ScreenVert Output;

	// this is friendlier for preshaders
	Output.Position = mul (Input.Position, mul (WorldMatrix, ProjMatrix));

	Output.Tex0 = Input.Tex0;
	Output.Tex1 = Input.Tex1;

	return Output;
}


technique ScreenUpdate
{
	pass P0
	{
		VertexShader = compile vs_2_0 VSCommon ();
		PixelShader = compile ps_2_0 PSUnderwaterPostProcess ();
	}

	pass P1
	{
		VertexShader = compile vs_2_0 VSCommon ();
		PixelShader = compile ps_2_0 PSPassThru ();
	}

	pass P2
	{
		VertexShader = compile vs_2_0 VSCommon ();
		PixelShader = compile ps_2_0 PSBlendLerp ();
	}
}
