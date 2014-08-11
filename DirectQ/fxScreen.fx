
texture ScreenTexture;
float warptime;
float warpscale;
float4 polyblend;

sampler SrcColor = sampler_state
{
	texture = <ScreenTexture>;
	addressu = CLAMP;
	addressv = CLAMP;
};


float4 PSPassThru (float2 Tex : TEXCOORD0) : COLOR0
{
	return tex2D (SrcColor, Tex.xy);
}


float4 PSBlendLerp (float2 Tex : TEXCOORD0) : COLOR0
{
	return lerp (tex2D (SrcColor, Tex.xy), polyblend, polyblend.a);
}


float4 PSUnderwaterPostProcess (float2 Tex : TEXCOORD0) : COLOR0
{
	float4 Color;

	float2 st;
	st.x = (Tex.x + sin (Tex.y + warptime) * warpscale) * 0.03125;
	st.y = (Tex.y + sin (Tex.x + warptime) * warpscale) * 0.03125;

	Color = lerp (tex2D (SrcColor, st.xy), polyblend, polyblend.a);
	return Color;
}


technique ScreenUpdate
{
	pass P0
	{
		VertexShader = null;
		PixelShader = compile ps_2_0 PSUnderwaterPostProcess ();
	}

	pass P1
	{
		VertexShader = null;
		PixelShader = compile ps_2_0 PSPassThru ();
	}

	pass P2
	{
		VertexShader = null;
		PixelShader = compile ps_2_0 PSBlendLerp ();
	}
}
