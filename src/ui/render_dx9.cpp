// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
#include "render_dx9.h"

#include <d3d9.h>
#include <stdint.h>
#include <vector>
#include <wincodec.h>

#include "imgui.h"

#include "../core/plugin.h"

struct Vertex {
	float x, y, z, rhw;
	D3DCOLOR color;
	float u, v;
};

static const DWORD kVertexFormat = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;

static IDirect3DDevice9 *g_device;
static IDirect3DTexture9 *g_fontTexture;
static IDirect3DStateBlock9 *g_savedState;
static IDirect3DStateBlock9 *g_imguiState;
static std::vector<Vertex> g_vertices;

static D3DCOLOR ToD3DColor(ImU32 color)
{
	return (color & 0xFF00FF00) | ((color & 0xFF0000) >> 16) | ((color & 0xFF) << 16);
}

static IDirect3DTexture9 *CreateTexture(const uint8_t *rgba, int width, int height, bool swapRB)
{
	if (!g_device || !rgba || width <= 0 || height <= 0) return nullptr;
	IDirect3DTexture9 *tex = nullptr;
	if (FAILED(g_device->CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr)) || !tex)
		return nullptr;
	D3DLOCKED_RECT locked;
	if (FAILED(tex->LockRect(0, &locked, nullptr, 0))) {
		tex->Release();
		return nullptr;
	}
	for (int y = 0; y < height; ++y) {
		auto src = reinterpret_cast<const uint32_t *>(rgba + y * width * 4);
		auto dst = reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(locked.pBits) + y * locked.Pitch);
		for (int x = 0; x < width; ++x) dst[x] = swapRB ? ToD3DColor(src[x]) : src[x];
	}
	tex->UnlockRect(0);
	return tex;
}

bool RenderInit(IDirect3DDevice9 *device)
{
	g_device = device;
	ImGuiIO &io = ImGui::GetIO();
	unsigned char *pixels;
	int width, height;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
	g_fontTexture = CreateTexture(pixels, width, height, true);
	if (!g_fontTexture) return false;
	io.Fonts->SetTexID((ImTextureID)(uintptr_t)g_fontTexture);
	return true;
}

void RenderOnLostDevice()
{
	if (g_savedState) { g_savedState->Release(); g_savedState = nullptr; }
	if (g_imguiState) { g_imguiState->Release(); g_imguiState = nullptr; }
}

void RenderResetDevice()
{
	RenderOnLostDevice();
	if (g_fontTexture) { g_fontTexture->Release(); g_fontTexture = nullptr; }
	g_device = nullptr;
}

IDirect3DTexture9 *LoadResourceTexture(int id, int &width, int &height)
{
	HRSRC r = FindResourceA(g_module, MAKEINTRESOURCEA(id), MAKEINTRESOURCEA(10) /* RT_RCDATA */);
	HGLOBAL h = r ? LoadResource(g_module, r) : nullptr;
	const BYTE *data = h ? (const BYTE *)LockResource(h) : nullptr;
	if (!data) return nullptr;
	DWORD size = SizeofResource(g_module, r);

	HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	IDirect3DTexture9 *tex = nullptr;
	IWICImagingFactory *factory = nullptr;
	IWICStream *stream = nullptr;
	IWICBitmapDecoder *decoder = nullptr;
	IWICBitmapFrameDecode *frame = nullptr;
	IWICFormatConverter *converter = nullptr;
	if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
	    SUCCEEDED(factory->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromMemory((BYTE *)data, size)) &&
	    SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) &&
	    SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
	    SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
		UINT w = 0, hgt = 0;
		if (SUCCEEDED(converter->GetSize(&w, &hgt)) && w > 0 && hgt > 0 && w <= 4096 && hgt <= 4096) {
			std::vector<uint8_t> pixels((size_t)w * hgt * 4);
			if (SUCCEEDED(converter->CopyPixels(nullptr, w * 4, (UINT)pixels.size(), pixels.data()))) {
				tex = CreateTexture(pixels.data(), (int)w, (int)hgt, false);
				width = (int)w;
				height = (int)hgt;
			}
		}
	}
	if (converter) converter->Release();
	if (frame) frame->Release();
	if (decoder) decoder->Release();
	if (stream) stream->Release();
	if (factory) factory->Release();
	if (SUCCEEDED(co)) CoUninitialize();
	return tex;
}

static void EmitImGuiStates()
{
	g_device->SetPixelShader(nullptr);
	g_device->SetVertexShader(nullptr);
	g_device->SetFVF(kVertexFormat);
	g_device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
	g_device->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
	g_device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	g_device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	g_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	g_device->SetRenderState(D3DRS_ZENABLE, FALSE);
	g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	g_device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
	g_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	g_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	g_device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, TRUE);
	g_device->SetRenderState(D3DRS_BLENDOPALPHA, D3DBLENDOP_ADD);
	g_device->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ONE);
	g_device->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_INVSRCALPHA);
	g_device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
	g_device->SetRenderState(D3DRS_FOGENABLE, FALSE);
	g_device->SetRenderState(D3DRS_RANGEFOGENABLE, FALSE);
	g_device->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
	g_device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
	g_device->SetRenderState(D3DRS_CLIPPING, TRUE);
	g_device->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
	g_device->SetRenderState(D3DRS_LIGHTING, FALSE);
	g_device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
	g_device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
	g_device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	g_device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	g_device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	g_device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	g_device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	g_device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
	g_device->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
	g_device->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	g_device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	g_device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	g_device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	g_device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	g_device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
	g_device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	g_device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	g_device->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
}

static void EmitSavedStateMask()
{
	// Record ONLY the exact ~28 D3D9 states ImGui modifies so Capture()/Apply()
	// avoids snapshotting & restoring all 300+ D3D9 states on every frame (5-8x faster in DXVK!).
	D3DVIEWPORT9 vp = { 0, 0, 1, 1, 0.0f, 1.0f };
	RECT sc = { 0, 0, 1, 1 };
	g_device->SetViewport(&vp);
	g_device->SetScissorRect(&sc);
	g_device->SetTexture(0, nullptr);
	g_device->SetStreamSource(0, nullptr, 0, 0);
	g_device->SetIndices(nullptr);
	EmitImGuiStates();
}

static bool EnsureStateBlocks()
{
	if (!g_savedState) {
		if (SUCCEEDED(g_device->BeginStateBlock())) {
			EmitSavedStateMask();
			g_device->EndStateBlock(&g_savedState);
		}
	}
	if (!g_imguiState) {
		if (SUCCEEDED(g_device->BeginStateBlock())) {
			EmitImGuiStates();
			g_device->EndStateBlock(&g_imguiState);
		}
	}
	return g_savedState != nullptr;
}

static void ApplyImGuiState(const ImDrawData *data)
{
	D3DVIEWPORT9 viewport = { 0, 0, (DWORD)data->DisplaySize.x, (DWORD)data->DisplaySize.y, 0.0f, 1.0f };
	g_device->SetViewport(&viewport);
	if (g_imguiState) g_imguiState->Apply();
	else EmitImGuiStates();
}

void RenderDrawData(ImDrawData *data)
{
	if (data->DisplaySize.x <= 0.0f || data->DisplaySize.y <= 0.0f || data->CmdListsCount == 0)
		return;
	if (!EnsureStateBlocks())
		return;

	g_savedState->Capture();
	ApplyImGuiState(data);

	const ImVec2 origin = data->DisplayPos;
	for (int n = 0; n < data->CmdListsCount; ++n) {
		const ImDrawList *list = data->CmdLists[n];
		if (!list || list->VtxBuffer.Size <= 0) continue;
		g_vertices.resize(list->VtxBuffer.Size);
		for (int i = 0; i < list->VtxBuffer.Size; ++i) {
			const ImDrawVert &src = list->VtxBuffer[i];
			Vertex &dst = g_vertices[i];
			dst.x = src.pos.x - origin.x - 0.5f;
			dst.y = src.pos.y - origin.y - 0.5f;
			dst.z = 0.0f;
			dst.rhw = 1.0f;
			dst.color = ToD3DColor(src.col);
			dst.u = src.uv.x;
			dst.v = src.uv.y;
		}
		for (const ImDrawCmd &cmd : list->CmdBuffer) {
			if (cmd.UserCallback) {
				if (cmd.UserCallback == ImDrawCallback_ResetRenderState)
					ApplyImGuiState(data);
				else
					cmd.UserCallback(list, &cmd);
				continue;
			}
			const RECT clip = {
				(LONG)(cmd.ClipRect.x - origin.x), (LONG)(cmd.ClipRect.y - origin.y),
				(LONG)(cmd.ClipRect.z - origin.x), (LONG)(cmd.ClipRect.w - origin.y),
			};
			if (clip.right <= clip.left || clip.bottom <= clip.top)
				continue;
			g_device->SetScissorRect(&clip);
			g_device->SetTexture(0, (IDirect3DTexture9 *)(uintptr_t)cmd.GetTexID());
			const UINT vertexCount = (UINT)list->VtxBuffer.Size - cmd.VtxOffset;
			g_device->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, vertexCount, cmd.ElemCount / 3,
			                                 list->IdxBuffer.Data + cmd.IdxOffset,
			                                 sizeof(ImDrawIdx) == 2 ? D3DFMT_INDEX16 : D3DFMT_INDEX32,
			                                 g_vertices.data() + cmd.VtxOffset, sizeof(Vertex));
		}
	}

	g_savedState->Apply();
}
