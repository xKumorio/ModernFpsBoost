// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [RadarBatch] The radar is drawn as hundreds of tiny draws: every blip's border and fill
// (CSprite2d::DrawRect / Draw2DPolygon from CRadar::ShowRadarTraceWithHeight), every icon
// (CRadar::DrawRadarSprite -> CSprite2d::Draw) and every SA-MP gang zone (CRadar::DrawAreaOnRadar)
// is its own RwIm2DRenderPrimitive, and each of those goes through RenderWare, the SAMPFUNCS device
// proxy and DXVK as a separate draw call (~0.5 us each, 150-200 per frame on a server with many
// blips and zones).
//
// While CRadar::DrawMap (the call at 0x58A38A in CHud::DrawRadar: map sections + gang zones) and
// CRadar::DrawBlips (entry 0x588050, hooked with a trampoline: called from SA-MP and the menu map)
// run, RenderWare's device functions in RwEngineInstance are swapped for collectors:
//   - render state sets are only remembered; the device gets them right before something is drawn.
//     So CSprite2d's "set texture, draw, set texture 0" and DrawRect's "alpha on, draw, alpha off"
//     around every blip no longer split the batch when the next blip sets the same again;
//   - a triangle fan / list of up to 64 vertices is appended to one buffer as triangles while the
//     states it needs are the ones the buffer is drawn with;
//   - an opaque untextured shape (every vertex alpha 255) looks the same with vertex alpha on, as long
//     as blending is the standard src alpha / inv src alpha (1 * src + 0 * dst), so the black blip
//     borders join the translucent fills;
//   - anything else (indexed, line, triangle, another primitive) first draws the buffer and applies
//     the remembered states.
// So the order and the state of every triangle is what it was; only the number of draw calls drops.
// The buffer is drawn and every state applied when the function returns, before anything else draws.
// Exe code reads RwEngineInstance through obfuscated stubs (0x409737 ...), but still from 0xC97B24.

#include "../registry.h"

static void **const kRwEngineInstance = (void **)0xC97B24;
static const DWORD kDrawMap = 0x586B00, kDrawMapCall = 0x58A38A, kDrawBlips = 0x588050;

// RwDevice in RwGlobals: fpRenderStateSet +0x20, fpRenderStateGet +0x24, fpIm2DRenderLine +0x28,
// fpIm2DRenderTriangle +0x2C, fpIm2DRenderPrimitive +0x30, fpIm2DRenderIndexedPrimitive +0x34
enum { kStateSet = 0x20 / 4, kStateGet = 0x24 / 4, kLine = 0x28 / 4, kTriangle = 0x2C / 4, kPrim = 0x30 / 4, kIndexed = 0x34 / 4 };

typedef int(__cdecl *StateSet_t)(int state, void *value);
typedef int(__cdecl *StateGet_t)(int state, void *value);
typedef int(__cdecl *Prim_t)(int type, void *verts, int n);
typedef int(__cdecl *Indexed_t)(int type, void *verts, int n, void *idx, int ni);
typedef int(__cdecl *Line_t)(void *verts, int n, int a, int b);
typedef int(__cdecl *Tri_t)(void *verts, int n, int a, int b, int c);

struct Vtx { float x, y, z, rhw; DWORD color; float u, v; }; // RwIm2DVertex (D3D9)

// rwRENDERSTATE*, rwBLEND*
enum { kRaster = 1, kSrcBlend = 10, kDestBlend = 11, kVertexAlpha = 12, kStates = 32 };
enum { kBlendSrcAlpha = 5, kBlendInvSrcAlpha = 6 };

static void **g_dev;
static void *g_orig[16];
static int g_depth;
static Vtx g_buf[3 * 1024];
static int g_n;
static void *g_want[kStates], *g_real[kStates]; // what the game set / what the device has
static DWORD g_touched;   // bit per state set inside the scope: g_want is valid
static DWORD g_realKnown; // bit per state: g_real is valid
static void *g_batchAlpha; // vertex alpha the buffer is drawn with
static bool g_batchOpaque; // every shape in the buffer is opaque and untextured: either vertex alpha fits
static DWORD g_draws, g_batches; // per second, for the stats

static void SetReal(int s, void *v)
{
	if ((g_realKnown >> s & 1) && g_real[s] == v) return;
	((StateSet_t)g_orig[kStateSet])(s, v);
	g_real[s] = v;
	g_realKnown |= 1u << s;
}

static int LowBit(DWORD m)
{
	unsigned long s;
	_BitScanForward(&s, m);
	return (int)s;
}

// the device gets every remembered state (vertex alpha too unless skipAlpha)
static void Sync(bool skipAlpha)
{
	DWORD m = g_touched;
	if (skipAlpha) m &= ~(1u << kVertexAlpha);
	for (; m; m &= m - 1) SetReal(LowBit(m), g_want[LowBit(m)]);
}

static void Flush()
{
	if (!g_n) return;
	SetReal(kVertexAlpha, g_batchAlpha);
	((Prim_t)g_orig[kPrim])(3 /* rwPRIMTYPETRILIST */, g_buf, g_n);
	g_n = 0;
	g_batches++;
}

static void *Current(int s)
{
	if (g_touched >> s & 1) return g_want[s];
	if (!(g_realKnown >> s & 1)) {
		void *v = nullptr;
		((StateGet_t)g_orig[kStateGet])(s, &v);
		g_real[s] = v;
		g_realKnown |= 1u << s;
	}
	return g_real[s];
}

// a state other than vertex alpha is not what the buffer is drawn with
static bool Dirty()
{
	for (DWORD m = g_touched & ~(1u << kVertexAlpha); m; m &= m - 1) {
		int s = LowBit(m);
		if (!(g_realKnown >> s & 1) || g_real[s] != g_want[s]) return true;
	}
	return false;
}

static void Passthrough()
{
	Flush();
	Sync(false);
}

static int __cdecl hkStateSet(int state, void *value)
{
	if (state <= 0 || state >= kStates) {
		Passthrough();
		return ((StateSet_t)g_orig[kStateSet])(state, value);
	}
	g_want[state] = value;
	g_touched |= 1u << state;
	return 1;
}

static int __cdecl hkStateGet(int state, void *value)
{
	if (state > 0 && state < kStates && (g_touched >> state & 1)) {
		*(void **)value = g_want[state];
		return 1;
	}
	return ((StateGet_t)g_orig[kStateGet])(state, value);
}

static int __cdecl hkPrim(int type, void *verts, int n)
{
	if (!verts || n < 3 || n > 64 || (type != 5 /* trifan */ && type != 3 /* trilist */)) {
		Passthrough();
		return ((Prim_t)g_orig[kPrim])(type, verts, n);
	}
	const Vtx *v = (const Vtx *)verts;
	void *alpha = Current(kVertexAlpha);
	bool opaque = !Current(kRaster) && Current(kSrcBlend) == (void *)kBlendSrcAlpha && Current(kDestBlend) == (void *)kBlendInvSrcAlpha;
	for (int k = 0; k < n && opaque; k++) opaque = (v[k].color >> 24) == 0xFF;
	int tris = type == 5 ? n - 2 : n / 3;
	bool fits = g_n && !Dirty() && g_n + tris * 3 <= (int)(sizeof(g_buf) / sizeof(g_buf[0]));
	if (fits && !alpha != !g_batchAlpha) {
		if (opaque) alpha = g_batchAlpha;                // this shape looks the same either way
		else if (g_batchOpaque) g_batchAlpha = alpha;    // so does everything already in the buffer
		else fits = false;
	}
	if (!fits) {
		Flush();
		Sync(true);
		g_batchAlpha = alpha;
		g_batchOpaque = true;
	}
	g_batchOpaque &= opaque;
	if (type == 5)
		for (int k = 1; k + 1 < n; k++) {
			g_buf[g_n++] = v[0];
			g_buf[g_n++] = v[k];
			g_buf[g_n++] = v[k + 1];
		}
	else
		for (int k = 0; k < tris * 3; k++) g_buf[g_n++] = v[k];
	g_draws++;
	return 1;
}

static int __cdecl hkIndexed(int type, void *verts, int n, void *idx, int ni)
{
	Passthrough();
	return ((Indexed_t)g_orig[kIndexed])(type, verts, n, idx, ni);
}

static int __cdecl hkLine(void *verts, int n, int a, int b)
{
	Passthrough();
	return ((Line_t)g_orig[kLine])(verts, n, a, b);
}

static int __cdecl hkTriangle(void *verts, int n, int a, int b, int c)
{
	Passthrough();
	return ((Tri_t)g_orig[kTriangle])(verts, n, a, b, c);
}

static const int kHooked[] = { kStateSet, kStateGet, kLine, kTriangle, kPrim, kIndexed };

static bool Begin()
{
	if (GetCurrentThreadId() != g_mainThread) return false;
	if (g_depth++) return true;
	BYTE *rw = (BYTE *)*kRwEngineInstance;
	if (!rw) {
		g_depth = 0;
		return false;
	}
	g_dev = (void **)rw;
	for (int k : kHooked) g_orig[k] = g_dev[k];
	g_touched = g_realKnown = 0;
	g_n = 0;
	g_dev[kStateSet] = (void *)hkStateSet;
	g_dev[kStateGet] = (void *)hkStateGet;
	g_dev[kLine] = (void *)hkLine;
	g_dev[kTriangle] = (void *)hkTriangle;
	g_dev[kPrim] = (void *)hkPrim;
	g_dev[kIndexed] = (void *)hkIndexed;
	return true;
}

static void End()
{
	if (--g_depth) return;
	Passthrough();
	for (int k : kHooked) g_dev[k] = g_orig[k];
}

static DWORD g_drawMapNext = kDrawMap; // the profiler can time DrawMap through it (call chain)

static void __cdecl hkDrawMap()
{
	bool on = Begin();
	((void(__cdecl *)())g_drawMapNext)();
	if (on) End();
}

static BYTE *g_blipsTramp; // the 6 bytes of DrawBlips' prologue + jmp back

static void __cdecl hkDrawBlips()
{
	bool on = Begin();
	((void(__cdecl *)())g_blipsTramp)();
	if (on) End();
}

static void Start()
{
	int n = 0;
	BYTE *call = (BYTE *)kDrawMapCall;
	if (IsCallTo(call, kDrawMap) && RedirectCall(call, hkDrawMap)) {
		AddCallChain(call, &g_drawMapNext);
		n++;
	}
	else Log("RadarBatch: the DrawMap call is not original - map and gang zones not batched");

	static const BYTE kPrologue[] = { 0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8 }; // push ebp; mov ebp, esp; and esp, -8
	if (!memcmp((void *)kDrawBlips, kPrologue, 6)) {
		g_blipsTramp = (BYTE *)VirtualAlloc(nullptr, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		memcpy(g_blipsTramp, kPrologue, 6);
		g_blipsTramp[6] = 0xE9;
		*(int32_t *)(g_blipsTramp + 7) = Rel32(g_blipsTramp + 6, (void *)(kDrawBlips + 6));
		BYTE jmp[6] = { 0xE9, 0, 0, 0, 0, 0x90 };
		*(int32_t *)(jmp + 1) = Rel32((BYTE *)kDrawBlips, hkDrawBlips);
		if (PatchBytes(kDrawBlips, kPrologue, jmp, 6)) n++;
	} else {
		Log("RadarBatch: CRadar::DrawBlips is hooked by something else - blips not batched");
	}
	if ((g_fixRadarBatch.active = n > 0)) Log("RadarBatch: %d/2 radar parts draw their small shapes in one call", n);
}

static void Report(std::string &out, double secs)
{
	if (!g_draws) return;
	char line[128];
	sprintf_s(line, "RadarBatch: %.0f radar draws/s sent as %.0f\n", g_draws / secs, g_batches / secs);
	out += line;
	g_draws = g_batches = 0;
}

Fix g_fixRadarBatch = {
	.section = "RadarBatch",
	.title = "Радар одним вызовом",
	.gain = "-1% кадра",
	.summary = "Метки игроков, иконки, точки и ганг-зоны на радаре рисуются пачкой, а не сотнями отдельных вызовов.",
	.details = "Каждая точка на радаре (игрок, метка, чекпоинт — с чёрной рамкой), каждая иконка и каждая ганг-зона рисуются отдельным вызовом "
	           "видеокарты, и каждый такой вызов проходит через RenderWare, SAMPFUNCS и DXVK. На сервере с кучей меток и зон это 150–200 вызовов "
	           "за кадр только на радар.\n\nТеперь фигуры подряд собираются в одну пачку и рисуются одним вызовом: рамки и полупрозрачные заливки "
	           "меток, одинаковые иконки, ганг-зоны. Порядок и цвета те же: если следующей фигуре нужна другая текстура или смешивание, "
	           "пачка сначала дорисовывается.",
	.gta = true,
	.atStart = Start,
	.report = Report,
};
