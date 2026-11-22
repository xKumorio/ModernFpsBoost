// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [SampStateBlockFix] Replaces SA-MP 0.3.7-R1's full D3DSBT_ALL IDirect3DStateBlock9 allocations
// (~900 D3D9 states captured and re-applied every single frame on DXVK!) with a targeted ~54-state
// block covering every render state, TSS, sampler, shader, FVF, stream, texture, transform and
// viewport touched by CPlayerTags, CDXUTDialog (chat scrollbar & dialogs), and ID3DXSprite:
//   1. CPlayerTags::ResetDevice  (samp+0x68FC2)
//   2. CPlayerTags::BeginHealthBar (samp+0x68FF8)
//   3. CDXUTDialogResourceManager::OnResetDevice (samp+0x829A6)
// Also hot-swaps any already-created D3DSBT_ALL blocks on the main thread after attach.

#include <d3d9.h>
#include <stdint.h>

#include "../registry.h"

static BYTE **g_playerTagsSlot;
static BYTE **g_dxutMgrSlot;
static IDirect3DStateBlock9 *g_fastBlocks[16];
static int g_fastIdx;
static unsigned g_created, g_swapped;

static bool IsFastBlock(IDirect3DStateBlock9 *sb)
{
	if (!sb) return false;
	for (IDirect3DStateBlock9 *p : g_fastBlocks)
		if (p == sb) return true;
	return false;
}

static void RememberFastBlock(IDirect3DStateBlock9 *sb)
{
	if (!sb || IsFastBlock(sb)) return;
	g_fastBlocks[g_fastIdx++ & 15] = sb;
}

static HRESULT CreateTargetedUiStateBlock(IDirect3DDevice9 *dev, IDirect3DStateBlock9 **ppSB)
{
	if (!dev || !ppSB) return E_INVALIDARG;
	*ppSB = nullptr;

	if (SUCCEEDED(dev->BeginStateBlock())) {
		// All render states touched by CPlayerTags, CDXUTDialog, CDXUTControl, and ID3DXSprite
		dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
		dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
		dev->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
		dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
		dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
		dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
		dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
		dev->SetRenderState(D3DRS_ALPHAREF, 0);
		dev->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_ALWAYS);
		dev->SetRenderState(D3DRS_DITHERENABLE, FALSE);
		dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
		dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
		dev->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
		dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
		dev->SetRenderState(D3DRS_TEXTUREFACTOR, 0xFFFFFFFF);
		dev->SetRenderState(D3DRS_CLIPPING, TRUE);
		dev->SetRenderState(D3DRS_LIGHTING, FALSE);
		dev->SetRenderState(D3DRS_COLORVERTEX, TRUE);
		dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
		dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
		dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
		dev->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ONE);
		dev->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_ZERO);

		// Texture stage 0 & 1 states
		dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
		dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
		dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
		dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
		dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
		dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
		dev->SetTextureStageState(0, D3DTSS_RESULTARG, D3DTA_CURRENT);
		dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
		dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

		// Sampler 0 states
		dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
		dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
		dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
		dev->SetSamplerState(0, D3DSAMP_MIPMAPLODBIAS, 0);
		dev->SetSamplerState(0, D3DSAMP_MAXMIPLEVEL, 0);

		// Shaders, vertex input & texture 0
		dev->SetVertexShader(nullptr);
		dev->SetPixelShader(nullptr);
		dev->SetVertexDeclaration(nullptr);
		dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
		dev->SetStreamSource(0, nullptr, 0, 0);
		dev->SetIndices(nullptr);
		dev->SetTexture(0, nullptr);

		// Transforms & viewport touched by ID3DXSprite
		D3DMATRIX ident = {};
		ident._11 = ident._22 = ident._33 = ident._44 = 1.0f;
		dev->SetTransform(D3DTS_WORLD, &ident);
		dev->SetTransform(D3DTS_VIEW, &ident);
		dev->SetTransform(D3DTS_PROJECTION, &ident);
		dev->SetTransform(D3DTS_TEXTURE0, &ident);
		D3DVIEWPORT9 vp = { 0, 0, 1, 1, 0.0f, 1.0f };
		dev->SetViewport(&vp);

		if (SUCCEEDED(dev->EndStateBlock(ppSB)) && *ppSB) {
			RememberFastBlock(*ppSB);
			g_created++;
			return D3D_OK;
		}
	}
	return dev->CreateStateBlock(D3DSBT_ALL, ppSB);
}

static HRESULT __stdcall hkCreateStateBlock(IDirect3DDevice9 *dev, D3DSTATEBLOCKTYPE type, IDirect3DStateBlock9 **ppSB)
{
	if (type == D3DSBT_ALL)
		return CreateTargetedUiStateBlock(dev, ppSB);
	return dev->CreateStateBlock(type, ppSB);
}

static void TryHotSwap(IDirect3DDevice9 *dev, IDirect3DStateBlock9 **slot, const char *name)
{
	if (!dev || !slot || !*slot || IsFastBlock(*slot)) return;
	IDirect3DStateBlock9 *fast = nullptr;
	if (SUCCEEDED(CreateTargetedUiStateBlock(dev, &fast)) && fast) {
		IDirect3DStateBlock9 *old = *slot;
		*slot = fast;
		old->Release();
		g_swapped++;
		Log("SampStateBlockFix: hot-swapped %s D3DSBT_ALL -> targeted 54-state block", name);
	}
}

void SampStateBlockFixTick()
{
	if (!g_fixSampStateBlockFix.active) return;

	// CPlayerTags singleton: dev @ +0x00, m_pStates @ +0x04
	if (g_playerTagsSlot && *g_playerTagsSlot) {
		BYTE *tags = *g_playerTagsSlot;
		IDirect3DDevice9 *dev = *(IDirect3DDevice9 **)(tags + 0x00);
		IDirect3DStateBlock9 **slot = (IDirect3DStateBlock9 **)(tags + 0x04);
		TryHotSwap(dev, slot, "CPlayerTags");
	}

	// CDXUTDialogResourceManager singleton: m_pStateBlock @ +0x00, dev @ +0x20
	if (g_dxutMgrSlot && *g_dxutMgrSlot) {
		BYTE *mgr = *g_dxutMgrSlot;
		IDirect3DDevice9 *dev = *(IDirect3DDevice9 **)(mgr + 0x20);
		IDirect3DStateBlock9 **slot = (IDirect3DStateBlock9 **)(mgr + 0x00);
		TryHotSwap(dev, slot, "CDXUTDialogResourceManager");
	}
}

static bool Apply(BYTE *)
{
	static const BYTE exEdx[6] = { 0xFF, 0x92, 0xEC, 0x00, 0x00, 0x00 }; // call dword ptr [edx+0xEC]
	static const BYTE exEcx[6] = { 0xFF, 0x91, 0xEC, 0x00, 0x00, 0x00 }; // call dword ptr [ecx+0xEC]

	struct Site { SampId id; const BYTE *exp; };
	const Site sites[3] = {
		{ S_SB_TAGS_RESET_SITE, exEdx }, // CPlayerTags::ResetDevice
		{ S_SB_TAGS_BEGIN_SITE, exEdx }, // CPlayerTags::BeginHealthBar
		{ S_SB_DXUT_RESET_SITE, exEcx }, // CDXUTDialogResourceManager::OnResetDevice
	};

	BYTE *addrs[3];
	BYTE patches[3][6];
	const void *data[3];
	size_t sizes[3];
	int n = 0;

	for (const Site &s : sites) {
		BYTE *p = SampAt(s.id);
		if (!p || memcmp(p, s.exp, 6) != 0) continue;
		patches[n][0] = 0xE8;
		*(int32_t *)(patches[n] + 1) = Rel32(p, (const void *)hkCreateStateBlock);
		patches[n][5] = 0x90;
		addrs[n] = p;
		data[n] = patches[n];
		sizes[n] = 6;
		n++;
	}

	if (n > 0 && PatchMainThread(addrs, data, sizes, n)) {
		g_playerTagsSlot = (BYTE **)SampAt(S_PLAYERTAGS_PTR);
		g_dxutMgrSlot    = (BYTE **)SampAt(S_DXUT_RESMGR_PTR);
		g_fixSampStateBlockFix.active = true;
		Log("SampStateBlockFix: %d/3 D3DSBT_ALL sites replaced with targeted 54-state block (%s)", n, SampVersionName());
	} else {
		Log("SampStateBlockFix: signatures changed (%d/3) on %s - skipped", n, SampVersionName());
	}
	return true;
}

Fix g_fixSampStateBlockFix = {
	.section = "SampStateBlockFix",
	.title = "Лёгкий StateBlock для UI SA-MP",
	.gain = "-1–1.8% кадра",
	.summary = "Заменяет тяжёлый D3DSBT_ALL (~900 стейтов видеокарты каждый кадр) в CPlayerTags и CDXUTDialog на точечный блок.",
	.details = "На каждый кадр SA-MP сохраняет и восстанавливает ПОЛНЫЙ D3DSBT_ALL StateBlock (~900 состояний Direct3D: все 8 стадий "
	           "текстур, все 16 потоков вершин, все источники света и матрицы) для полосок HP и для скроллбара чата / диалогов "
	           "(CDXUTDialog::OnRender). На DXVK такой Capture + Apply бьёт по CPU на 1.5–2% кадра.\n\n"
	           "Здесь сохраняются и восстанавливаются ровно те ~54 состояния, которые эти отрисовки реально меняют. "
	           "Картинка на 100% идентична.",
	.atSamp = Apply,
};
