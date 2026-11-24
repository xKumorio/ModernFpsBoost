// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [MaterialFontCache] CObjectMaterialText::Create (R1 samp+0x6C2D0) renders the text of a
// SetObjectMaterialText object into a texture when the object streams in: it creates a D3DX font
// (samp+0x6C3DD, D3DXCreateFontA: GDI font creation + glyph rasterization, several to tens of ms - a
// visible hitch), draws once and releases the font. Fonts are kept here by their creation parameters
// and handed out again with AddRef, so a second sign with the same face/size/weight reuses the font
// and its glyphs. All kept fonts are released when any d3dx9_25 font is told the device is lost
// (SA-MP does that for its own fonts before Reset; hooked by TextCache), so nothing of ours holds
// device resources across a Reset.

#include "material_font_cache.h"

#include "../registry.h"

typedef HRESULT(__stdcall *CreateFontA_t)(void *, int, UINT, UINT, UINT, BOOL, DWORD, DWORD, DWORD, DWORD, const char *, void **);
static CreateFontA_t g_createFontA;
struct MatFont { void *dev; int h; UINT w, weight, mips; BOOL italic; DWORD cs, op, q, pf; char face[32]; void *font; };
static MatFont g_matFonts[64];
static int g_matFontN;

static void FontAddRef(void *font) { ((ULONG(__stdcall *)(void *))(*(void ***)font)[1])(font); }
static void FontRelease(void *font) { ((ULONG(__stdcall *)(void *))(*(void ***)font)[2])(font); }

void MaterialFontCachePurge()
{
	for (int i = 0; i < g_matFontN; i++)
		if (g_matFonts[i].font) FontRelease(g_matFonts[i].font);
	g_matFontN = 0;
}

static HRESULT __stdcall hkMatCreateFont(void *dev, int h, UINT w, UINT weight, UINT mips, BOOL italic, DWORD cs, DWORD op, DWORD q, DWORD pf, const char *face, void **out)
{
	if (!face || strlen(face) >= sizeof(MatFont::face) || GetCurrentThreadId() != g_mainThread)
		return g_createFontA(dev, h, w, weight, mips, italic, cs, op, q, pf, face, out);
	for (int i = 0; i < g_matFontN; i++) {
		MatFont &f = g_matFonts[i];
		if (f.dev == dev && f.h == h && f.w == w && f.weight == weight && f.mips == mips && f.italic == italic && f.cs == cs &&
		    f.op == op && f.q == q && f.pf == pf && !strcmp(f.face, face)) {
			FontAddRef(f.font); // the caller releases its reference
			*out = f.font;
			return S_OK;
		}
	}
	HRESULT hr = g_createFontA(dev, h, w, weight, mips, italic, cs, op, q, pf, face, out);
	if (SUCCEEDED(hr) && *out && g_matFontN < 64) {
		MatFont &f = g_matFonts[g_matFontN++];
		f = { dev, h, w, weight, mips, italic, cs, op, q, pf, {}, *out };
		strcpy_s(f.face, face);
		FontAddRef(f.font); // the cache's own reference
	}
	return hr;
}

void MaterialFontCacheApply(BYTE *)
{
	static bool done;
	if (done) return;
	done = true;
	BYTE *p = SampAt(S_MATFONT_CREATE_SITE);
	// the argument pushes before the call: the same bytes in every version
	static const BYTE ctx[] = { 0x8D, 0x54, 0x24, 0x0C, 0x52, 0x53, 0x6A, 0x00, 0x6A, 0x00, 0x6A, 0x00, 0x6A, 0x01, 0x6A, 0x00, 0x6A, 0x01, 0x51, 0x8B, 0x0E, 0x6A, 0x00, 0x50, 0x51 };
	if (!p || !IsReadableRange(p - sizeof(ctx), sizeof(ctx) + 5) || *p != 0xE8 || memcmp(p - sizeof(ctx), ctx, sizeof(ctx))) {
		Log("MaterialFontCache: the D3DXCreateFontA call is not original - skipped");
		return;
	}
	g_createFontA = (CreateFontA_t)((DWORD)(p + 5) + *(int32_t *)(p + 1));
	if ((g_fixMaterialFontCache.active = RedirectCall(p, hkMatCreateFont))) Log("MaterialFontCache: object material text fonts reused");
}

Fix g_fixMaterialFontCache = {
	.section = "MaterialFontCache",
	.title = "Надписи на объектах",
	.gain = "без фризов",
	.summary = "Шрифты для надписей на объектах создаются один раз: пропадают подлаги 10–60 мс при появлении вывесок.",
	.details = "SA-MP для каждой надписи на объекте заново создаёт шрифт через GDI Windows (медленно), рисует текст один раз и удаляет шрифт.\n\nШрифты запоминаются по параметрам и отдаются снова. Перед сбросом устройства (Alt+Tab) кеш очищается. Работает вместе с кешем текста.",
};
