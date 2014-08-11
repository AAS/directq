

// state block management
#include "quakedef.h"
#include "d3d_quake.h"


extern D3DDISPLAYMODE d3d_CurrentMode;

IDirect3DStateBlock9 *d3d_SetAliasState = NULL;
IDirect3DStateBlock9 *d3d_RevertAliasState = NULL;
IDirect3DStateBlock9 *d3d_DefaultViewport = NULL;
IDirect3DStateBlock9 *d3d_GunViewport = NULL;
IDirect3DStateBlock9 *d3d_2DViewport = NULL;
IDirect3DStateBlock9 *d3d_EnableAlphaTest = NULL;
IDirect3DStateBlock9 *d3d_DisableAlphaTest = NULL;
IDirect3DStateBlock9 *d3d_EnableAlphaBlend = NULL;
IDirect3DStateBlock9 *d3d_DisableAlphaBlend = NULL;


void D3D_ReleaseStateBlocks (void)
{
	SAFE_RELEASE (d3d_EnableAlphaBlend);
	SAFE_RELEASE (d3d_DisableAlphaBlend);
	SAFE_RELEASE (d3d_EnableAlphaTest);
	SAFE_RELEASE (d3d_DisableAlphaTest);
	SAFE_RELEASE (d3d_SetAliasState);
	SAFE_RELEASE (d3d_RevertAliasState);
	SAFE_RELEASE (d3d_DefaultViewport);
	SAFE_RELEASE (d3d_GunViewport);
	SAFE_RELEASE (d3d_2DViewport);
}


void D3D_CreateStateBlocks (void)
{
	D3DVIEWPORT9 vp;

	// enable alpha blend
	d3d_Device->BeginStateBlock ();
	d3d_Device->SetRenderState (D3DRS_ALPHABLENDENABLE, TRUE);
	d3d_Device->SetRenderState (D3DRS_BLENDOP, D3DBLENDOP_ADD);
	d3d_Device->SetTextureStageState (0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	d3d_Device->SetRenderState (D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	d3d_Device->SetRenderState (D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	d3d_Device->EndStateBlock (&d3d_EnableAlphaBlend);

	// disable alpha blend
	d3d_Device->BeginStateBlock ();
	d3d_Device->SetTextureStageState (0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	d3d_Device->SetRenderState (D3DRS_ALPHABLENDENABLE, FALSE);
	d3d_Device->EndStateBlock (&d3d_DisableAlphaBlend);

	// enable alpha test
	d3d_Device->BeginStateBlock ();
	d3d_Device->SetTextureStageState (0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	d3d_Device->SetRenderState (D3DRS_ALPHATESTENABLE, TRUE);
	d3d_Device->SetRenderState (D3DRS_ALPHAREF, 128);
	d3d_Device->SetRenderState (D3DRS_ALPHAFUNC, D3DCMP_GREATER);
	d3d_Device->EndStateBlock (&d3d_EnableAlphaTest);

	// disable alpha test
	d3d_Device->BeginStateBlock ();
	d3d_Device->SetTextureStageState (0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	d3d_Device->SetRenderState (D3DRS_ALPHATESTENABLE, FALSE);
	d3d_Device->EndStateBlock (&d3d_DisableAlphaTest);

	// default viewport
	d3d_Device->BeginStateBlock ();
	vp.X      = 0;
	vp.Y      = 0;
	vp.Width  = d3d_CurrentMode.Width;
	vp.Height = d3d_CurrentMode.Height;
	vp.MinZ   = 0.0f;
	vp.MaxZ   = 1.0f;
	d3d_Device->SetViewport (&vp);
	d3d_Device->EndStateBlock (&d3d_DefaultViewport);

	// gun viewport
	d3d_Device->BeginStateBlock ();
	vp.X      = 0;
	vp.Y      = 0;
	vp.Width  = d3d_CurrentMode.Width;
	vp.Height = d3d_CurrentMode.Height;
	vp.MinZ   = 0.0f;
	vp.MaxZ   = 0.3f;
	d3d_Device->SetViewport (&vp);
	d3d_Device->EndStateBlock (&d3d_GunViewport);

	// 2D viewport
	d3d_Device->BeginStateBlock ();
	vp.X      = 0;
	vp.Y      = 0;
	vp.Width  = d3d_CurrentMode.Width;
	vp.Height = d3d_CurrentMode.Height;
	vp.MinZ   = 0.0f;
	vp.MaxZ   = 1.0f;
	d3d_Device->SetViewport (&vp);
	d3d_Device->EndStateBlock (&d3d_2DViewport);
}


// other states
void D3D_SetDefaultStates (void)
{
	// create a matrix stack for the world
	SAFE_RELEASE (d3d_WorldMatrixStack);
	D3DXCreateMatrixStack (0, &d3d_WorldMatrixStack);

	D3DSAMPLERSTATETYPE TestSamplerState[] = {D3DSAMP_MIPFILTER, D3DSAMP_MAGFILTER, D3DSAMP_MINFILTER};

	for (int i = 0; i < d3d_DeviceCaps.MaxTextureBlendStages; i++)
	{
		HRESULT hr;

		for (int j = 0; j < 3; j++)
		{
			// attempt linear sampling
			hr = d3d_Device->SetSamplerState (i, TestSamplerState[j], D3DTEXF_LINEAR);

			// did we get it
			if (SUCCEEDED (hr)) continue;

			// fall back on point
			hr = d3d_Device->SetSamplerState (i, TestSamplerState[j], D3DTEXF_POINT);

			// did we get it?
			if (FAILED (hr))
			{
				// failed - the card is probably not able to support basic texturing!
				Sys_Error ("D3D_SetDefaultStates: Unable to set sampler state");
				return;
			}
		}
	}

	// disable lighting
	d3d_Device->SetRenderState (D3DRS_LIGHTING, FALSE);
}


