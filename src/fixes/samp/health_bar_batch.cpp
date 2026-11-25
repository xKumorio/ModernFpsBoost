// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [HealthBarBatch] Batches SA-MP 0.3.7-R1's CPlayerTags::DrawHealthBar (samp+0x689C0):
// Vanilla calls IDirect3DDevice9::GetViewport + D3DXVec3Project + 1-2x SetFVF(0x44) + 3-6x
// DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, ...) for EVERY visible player (up to 300-600 separate
// DrawPrimitiveUP calls per frame in a 50-100 player crowd!).
// Here Viewport and View*Proj are computed once per frame, all players' health & armor quads are
// projected in SSE2 and appended to one triangle-list buffer, and the entire crowd is drawn in
// ONE single SetFVF + ONE single DrawPrimitiveUP right before EndHealthBar.

#include <d3d9.h>
#include <immintrin.h>
#include <stdint.h>
#include <vector>

#include "../registry.h"

struct Vec3 { float x, y, z; };

struct BarVertex {
	float x, y, z, rhw;
	D3DCOLOR color;
}; // 20 bytes, FVF = 0x44 (D3DFVF_XYZRHW | D3DFVF_DIFFUSE)

struct CPlayerTags {
	IDirect3DDevice9 *device;      // 0x00
	IDirect3DStateBlock9 *states;  // 0x04
	void *sprite;                  // 0x08
};

static const float *g_matView;
static const float *g_matProj;
static void(__thiscall *oBeginHealthBar)(CPlayerTags *);
static void(__thiscall *oEndHealthBar)(CPlayerTags *);

static bool g_inBatch;
static bool g_projReady;
static D3DVIEWPORT9 g_vp;
static __m128 g_vpRow0, g_vpRow1, g_vpRow2, g_vpRow3;
static std::vector<BarVertex> g_verts;

static unsigned g_barsDrawn, g_flushes;

static inline void MulMat4x4(const float *a, const float *b, __m128 &o0, __m128 &o1, __m128 &o2, __m128 &o3)
{
	const __m128 b0 = _mm_loadu_ps(b + 0);
	const __m128 b1 = _mm_loadu_ps(b + 4);
	const __m128 b2 = _mm_loadu_ps(b + 8);
	const __m128 b3 = _mm_loadu_ps(b + 12);

	auto mulRow = [&](const float *r) {
		__m128 res = _mm_mul_ps(_mm_set1_ps(r[0]), b0);
		res = _mm_add_ps(res, _mm_mul_ps(_mm_set1_ps(r[1]), b1));
		res = _mm_add_ps(res, _mm_mul_ps(_mm_set1_ps(r[2]), b2));
		res = _mm_add_ps(res, _mm_mul_ps(_mm_set1_ps(r[3]), b3));
		return res;
	};
	o0 = mulRow(a + 0);
	o1 = mulRow(a + 4);
	o2 = mulRow(a + 8);
	o3 = mulRow(a + 12);
}

static inline void PushQuad(float l, float t, float r, float b, D3DCOLOR col)
{
	const BarVertex v0 = { l, t, 0.0f, 1.0f, col };
	const BarVertex v1 = { l, b, 0.0f, 1.0f, col };
	const BarVertex v2 = { r, b, 0.0f, 1.0f, col };
	const BarVertex v3 = { r, t, 0.0f, 1.0f, col };
	g_verts.push_back(v0);
	g_verts.push_back(v1);
	g_verts.push_back(v2);
	g_verts.push_back(v0);
	g_verts.push_back(v2);
	g_verts.push_back(v3);
}

static void FlushBars(IDirect3DDevice9 *dev)
{
	if (g_verts.empty() || !dev) return;
	dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
	dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, (UINT)(g_verts.size() / 3), g_verts.data(), sizeof(BarVertex));
	g_verts.clear();
	g_flushes++;
}

static void __fastcall hkBeginHealthBar(CPlayerTags *self)
{
	g_verts.clear();
	g_inBatch = true;
	g_projReady = false;
	oBeginHealthBar(self);
}

static void __fastcall hkEndHealthBar(CPlayerTags *self)
{
	if (!g_verts.empty() && self && self->device)
		FlushBars(self->device);
	g_inBatch = false;
	g_projReady = false;
	oEndHealthBar(self);
}

static void __fastcall hkDrawHealthBar(CPlayerTags *self, void *, const Vec3 *pos, float health, float armor, float dist)
{
	if (!self || !self->device || !pos) return;

	if (!g_projReady) {
		self->device->GetViewport(&g_vp);
		MulMat4x4(g_matView, g_matProj, g_vpRow0, g_vpRow1, g_vpRow2, g_vpRow3);
		g_projReady = true;
	}

	// Exact SA-MP height offset: z + dist * 0.0475f + 0.2f
	const float wx = pos->x;
	const float wy = pos->y;
	const float wz = pos->z + dist * 0.04749999940395355f + 0.20000000298023224f;

	__m128 clip = _mm_mul_ps(_mm_set1_ps(wx), g_vpRow0);
	clip = _mm_add_ps(clip, _mm_mul_ps(_mm_set1_ps(wy), g_vpRow1));
	clip = _mm_add_ps(clip, _mm_mul_ps(_mm_set1_ps(wz), g_vpRow2));
	clip = _mm_add_ps(clip, g_vpRow3);

	alignas(16) float c[4];
	_mm_store_ps(c, clip);
	if (c[3] == 0.0f) return;
	const float invW = 1.0f / c[3];
	const float sz = g_vp.MinZ + (c[2] * invW) * (g_vp.MaxZ - g_vp.MinZ);
	if (!(sz <= 1.0f)) return;
	if (health < 0.0f) return;

	const float sx = (float)(int)(g_vp.X + (1.0f + c[0] * invW) * ((float)g_vp.Width * 0.5f));
	const float sy = (float)(int)(g_vp.Y + (1.0f - c[1] * invW) * ((float)g_vp.Height * 0.5f));

	const bool hasArmor = (armor > 0.0f);
	const float hy = hasArmor ? (sy + 8.0f) : sy;
	const float hp = (health > 100.0f) ? 100.0f : health;
	const float hpRight = sx + hp * 0.38461539149284363f - 19.0f;

	// Health bar: black border, dark-red bg, bright-red fill (exact SA-MP coords & ARGB colors)
	PushQuad(sx - 20.0f, hy + 18.0f, sx + 21.0f, hy + 24.0f, 0xFF000000);
	PushQuad(sx - 19.0f, hy + 19.0f, sx + 19.0f, hy + 23.0f, 0xFF4B0B14);
	PushQuad(sx - 19.0f, hy + 19.0f, hpRight,     hy + 23.0f, 0xFFB92228);
	g_barsDrawn += 3;

	if (hasArmor) {
		const float ap = (armor > 100.0f) ? 100.0f : armor;
		const float apRight = sx + ap * 0.38461539149284363f - 19.0f;
		// Armor bar above health bar at original sy
		PushQuad(sx - 20.0f, sy + 18.0f, sx + 21.0f, sy + 24.0f, 0xFF000000);
		PushQuad(sx - 19.0f, sy + 19.0f, sx + 19.0f, sy + 23.0f, 0xFF282828);
		PushQuad(sx - 19.0f, sy + 19.0f, apRight,     sy + 23.0f, 0xFFC8C8C8);
		g_barsDrawn += 3;
	}

	if (!g_inBatch) {
		FlushBars(self->device);
		g_projReady = false;
	}
}

static bool Apply(BYTE *)
{
	static const BYTE exBegin[6] = { 0x56, 0x8B, 0xF1, 0x8B, 0x46, 0x04 };
	static const BYTE exEnd[6]   = { 0x56, 0x8B, 0xF1, 0x8B, 0x46, 0x08 };
	static const BYTE exDraw[7]  = { 0x83, 0xEC, 0x74, 0x8B, 0x44, 0x24, 0x78 };

	BYTE *pBegin = SampAt(S_TAG_BEGIN_HB);
	BYTE *pEnd   = SampAt(S_TAG_END_HB);
	BYTE *pDraw  = SampAt(S_TAG_DRAW_HB);
	const float *mv = (const float *)SampAt(S_MAT_VIEW);
	const float *mp = (const float *)SampAt(S_MAT_PROJ);

	if (!pBegin || !pEnd || !pDraw || !mv || !mp ||
	    !SafeMemMatch(pBegin, exBegin, 6) || !SafeMemMatch(pEnd, exEnd, 6) || !SafeMemMatch(pDraw, exDraw, 7)) {
		Log("HealthBarBatch: CPlayerTags signatures changed on %s - skipped", SampVersionName());
		return true;
	}

	BYTE *tr = (BYTE *)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (!tr) return true;

	// Trampoline 0 (+0): BeginHealthBar (6 bytes + jmp)
	memcpy(tr, exBegin, 6);
	tr[6] = 0xE9;
	*(int32_t *)(tr + 7) = Rel32(tr + 6, pBegin + 6);
	oBeginHealthBar = (void(__thiscall *)(CPlayerTags *))tr;

	// Trampoline 1 (+32): EndHealthBar (6 bytes + jmp)
	memcpy(tr + 32, exEnd, 6);
	tr[38] = 0xE9;
	*(int32_t *)(tr + 39) = Rel32(tr + 38, pEnd + 6);
	oEndHealthBar = (void(__thiscall *)(CPlayerTags *))(tr + 32);

	FlushInstructionCache(GetCurrentProcess(), tr, 64);

	g_verts.reserve(1024);
	g_matView = mv;
	g_matProj = mp;

	BYTE jBegin[6] = { 0xE9, 0, 0, 0, 0, 0x90 };
	BYTE jEnd[6]   = { 0xE9, 0, 0, 0, 0, 0x90 };
	BYTE jDraw[5]  = { 0xE9, 0, 0, 0, 0 };
	*(int32_t *)(jBegin + 1) = Rel32(pBegin, (const void *)hkBeginHealthBar);
	*(int32_t *)(jEnd + 1)   = Rel32(pEnd,   (const void *)hkEndHealthBar);
	*(int32_t *)(jDraw + 1)  = Rel32(pDraw,  (const void *)hkDrawHealthBar);

	BYTE *addrs[3]      = { pBegin, pEnd, pDraw };
	const void *data[3] = { jBegin, jEnd, jDraw };
	size_t sizes[3]     = { 6, 6, 5 };

	if ((g_fixHealthBarBatch.active = PatchMainThread(addrs, data, sizes, 3)))
		Log("HealthBarBatch: CPlayerTags::DrawHealthBar batched (%s, 1 DrawPrimitiveUP per frame + SSE2 projection)", SampVersionName());
	else
		Log("HealthBarBatch: patch failed");
	return true;
}

static void Report(std::string &out, double secs)
{
	if (!g_barsDrawn) return;
	Appendf(out, Tr("HealthBarBatch: %.0f полосок/с -> %.0f вызовов DrawPrimitiveUP/с\n", "HealthBarBatch: %.0f bars/s -> %.0f DrawPrimitiveUP calls/s\n"), g_barsDrawn / secs, g_flushes / secs);
	g_barsDrawn = g_flushes = 0;
}

Fix g_fixHealthBarBatch = {
	.section = "HealthBarBatch",
	.title = "Полоски HP/брони одним вызовом",
	.gain = "-0.5–1.5% кадра",
	.summary = "Собирает все полоски HP и брони игроков над головой в один вызов DrawPrimitiveUP за кадр вместо 3–6 на каждого игрока.",
	.details = "В оригинальном SA-MP для КАЖДОГО видимого игрока с бронёй вызывается GetViewport, D3DXVec3Project, дважды SetFVF "
	           "и до 6 отдельных DrawPrimitiveUP (рамка, фон и заливка для HP и для брони). В толпе из 50 игроков это 300 вызовов "
	           "отрисовки каждый кадр только на полоски.\n\n"
	           "Теперь матрица View*Proj и Viewport берутся один раз за кадр, координаты считаются на SSE2, а все полоски всех игроков "
	           "отправляются в видеокарту ОДНИМ вызовом DrawPrimitiveUP. Попиксельно идентично.",
	.atSamp = Apply,
	.report = Report,
};
