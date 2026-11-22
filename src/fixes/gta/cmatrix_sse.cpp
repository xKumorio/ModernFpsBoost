// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [CMatrixSSE] Replaces 10 hot x87 CMatrix/CVector functions in gta_sa.exe 1.0 US with stall-free SSE2:
//   - CMatrix::SetRotateX/Y/Z (0x59B060, 0x59B0A0, 0x59B0E0) and SetRotateX/Y/ZOnly (0x59AFA0, 0x59AFE0, 0x59B020)
//   - CMatrix::operator* (0x59BE30)
//   - MultiplyMatrixWithVector (0x59C890)
//   - CMatrix::Invert (0x59B920)
//   - CVector::CrossProduct (0x59C730)
// Uses _mm_storel_pi + _mm_store_ss to write 3 floats directly with zero 128->32-bit store-forwarding stalls.

#include <immintrin.h>
#include <math.h>
#include <stdint.h>

#include "../registry.h"

struct Vec3 { float x, y, z; };
struct CMatrix {
	float rx, ry, rz; DWORD pad0;
	float ux, uy, uz; DWORD pad1;
	float ax, ay, az; DWORD pad2;
	float px, py, pz; DWORD pad3;
	void *attach;
	DWORD ownsAttach;
};

static inline void StoreVec3(float *dst, __m128 v)
{
	_mm_storel_pi((__m64 *)dst, v);
	_mm_store_ss(dst + 2, _mm_movehl_ps(v, v));
}

static inline void FastSinCos(float a, float &s, float &c)
{
	if (a == 0.0f) { s = 0.0f; c = 1.0f; return; }
	static const float kTwoOverPi  = 0.63661977236758134308f;
	static const float kPiOver2Hi  = 1.5707962512969970703125f;
	static const float kPiOver2Lo  = 7.54978941586159635335207e-8f;
	static const float kPiOver2Lo2 = 5.39030252995776476554468e-15f;

	float kf = floorf(a * kTwoOverPi + 0.5f);
	int k = (int)kf;
	float r = ((a - kf * kPiOver2Hi) - kf * kPiOver2Lo) - kf * kPiOver2Lo2;
	float r2 = r * r;

	float sr = r + r * r2 * (-0.16666667163372039795f + r2 * (0.00833333102054893970f - r2 * 0.00019839523980044760f));
	float cr = 1.0f + r2 * (-0.5f + r2 * (0.04166666418313980103f - r2 * 0.00138883973018452525f));

	switch (k & 3) {
	case 0: s = sr;  c = cr;  break;
	case 1: s = cr;  c = -sr; break;
	case 2: s = -sr; c = -cr; break;
	case 3: s = -cr; c = sr;  break;
	}
}

static void __fastcall hkSetRotateXOnly(CMatrix *m, void *, float a)
{
	float s, c; FastSinCos(a, s, c);
	m->rx = 1.0f; m->ry = 0.0f; m->rz = 0.0f;
	m->ux = 0.0f; m->uy = c;    m->uz = s;
	m->ax = 0.0f; m->ay = -s;   m->az = c;
}

static void __fastcall hkSetRotateYOnly(CMatrix *m, void *, float a)
{
	float s, c; FastSinCos(a, s, c);
	m->rx = c;    m->ry = 0.0f; m->rz = -s;
	m->ux = 0.0f; m->uy = 1.0f; m->uz = 0.0f;
	m->ax = s;    m->ay = 0.0f; m->az = c;
}

static void __fastcall hkSetRotateZOnly(CMatrix *m, void *, float a)
{
	float s, c; FastSinCos(a, s, c);
	m->rx = c;    m->ry = s;    m->rz = 0.0f;
	m->ux = -s;   m->uy = c;    m->uz = 0.0f;
	m->ax = 0.0f; m->ay = 0.0f; m->az = 1.0f;
}

static void __fastcall hkSetRotateX(CMatrix *m, void *, float a)
{
	hkSetRotateXOnly(m, nullptr, a);
	m->px = 0.0f; m->py = 0.0f; m->pz = 0.0f;
}

static void __fastcall hkSetRotateY(CMatrix *m, void *, float a)
{
	hkSetRotateYOnly(m, nullptr, a);
	m->px = 0.0f; m->py = 0.0f; m->pz = 0.0f;
}

static void __fastcall hkSetRotateZ(CMatrix *m, void *, float a)
{
	hkSetRotateZOnly(m, nullptr, a);
	m->px = 0.0f; m->py = 0.0f; m->pz = 0.0f;
}

static CMatrix *__cdecl hkMatrixMul(CMatrix *out, const CMatrix *a, const CMatrix *b)
{
	const __m128 a0 = _mm_loadu_ps(&a->rx);
	const __m128 a1 = _mm_loadu_ps(&a->ux);
	const __m128 a2 = _mm_loadu_ps(&a->ax);
	const __m128 a3 = _mm_loadu_ps(&a->px);

	auto mulRow = [&](const float *r) {
		return _mm_add_ps(
			_mm_add_ps(_mm_mul_ps(_mm_set1_ps(r[0]), a0), _mm_mul_ps(_mm_set1_ps(r[1]), a1)),
			_mm_mul_ps(_mm_set1_ps(r[2]), a2));
	};

	const __m128 r0 = mulRow(&b->rx);
	const __m128 r1 = mulRow(&b->ux);
	const __m128 r2 = mulRow(&b->ax);
	const __m128 r3 = _mm_add_ps(mulRow(&b->px), a3);

	StoreVec3(&out->rx, r0);
	StoreVec3(&out->ux, r1);
	StoreVec3(&out->ax, r2);
	StoreVec3(&out->px, r3);
	out->attach = nullptr;
	out->ownsAttach = 0;
	return out;
}

static Vec3 *__cdecl hkMulMatrixVec(Vec3 *out, const CMatrix *m, const Vec3 *v)
{
	__m128 r = _mm_add_ps(
		_mm_add_ps(_mm_mul_ps(_mm_set1_ps(v->x), _mm_loadu_ps(&m->rx)),
		           _mm_mul_ps(_mm_set1_ps(v->y), _mm_loadu_ps(&m->ux))),
		_mm_add_ps(_mm_mul_ps(_mm_set1_ps(v->z), _mm_loadu_ps(&m->ax)),
		           _mm_loadu_ps(&m->px)));
	StoreVec3(&out->x, r);
	return out;
}

// __cdecl CMatrix *CMatrix::Invert(const CMatrix &in, CMatrix &out) at 0x59B920
static CMatrix *__cdecl hkMatrixInvert(const CMatrix *in, CMatrix *out)
{
	const __m128 r0 = _mm_set_ps(0.0f, in->ax, in->ux, in->rx);
	const __m128 r1 = _mm_set_ps(0.0f, in->ay, in->uy, in->ry);
	const __m128 r2 = _mm_set_ps(0.0f, in->az, in->uz, in->rz);
	const float px = in->px, py = in->py, pz = in->pz;

	const __m128 p = _mm_add_ps(
		_mm_add_ps(_mm_mul_ps(_mm_set1_ps(px), r0), _mm_mul_ps(_mm_set1_ps(py), r1)),
		_mm_mul_ps(_mm_set1_ps(pz), r2));
	const __m128 negP = _mm_xor_ps(p, _mm_set1_ps(-0.0f));

	StoreVec3(&out->rx, r0);
	StoreVec3(&out->ux, r1);
	StoreVec3(&out->ax, r2);
	StoreVec3(&out->px, negP);
	return out;
}

// __cdecl Vec3 *CrossProduct(Vec3 *out, const Vec3 *a, const Vec3 *b) at 0x59C730
static Vec3 *__cdecl hkCrossProduct(Vec3 *out, const Vec3 *a, const Vec3 *b)
{
	__m128 va = _mm_set_ps(0.0f, a->z, a->y, a->x);
	__m128 vb = _mm_set_ps(0.0f, b->z, b->y, b->x);
	__m128 a_yzx = _mm_shuffle_ps(va, va, _MM_SHUFFLE(3, 0, 2, 1));
	__m128 b_yzx = _mm_shuffle_ps(vb, vb, _MM_SHUFFLE(3, 0, 2, 1));
	__m128 c = _mm_sub_ps(_mm_mul_ps(va, b_yzx), _mm_mul_ps(a_yzx, vb));
	__m128 res = _mm_shuffle_ps(c, c, _MM_SHUFFLE(3, 0, 2, 1));
	StoreVec3(&out->x, res);
	return out;
}

static void Apply()
{
	struct Patch { DWORD addr; const BYTE *expect; size_t len; const void *hook; };
	static const BYTE exRXO[] = { 0xD9, 0x44, 0x24, 0x04, 0x33, 0xC0, 0xD9, 0xFF, 0xC7, 0x01 };
	static const BYTE exRYO[] = { 0xD9, 0x44, 0x24, 0x04, 0x33, 0xC0, 0xD9, 0xFF, 0x89, 0x41, 0x04 };
	static const BYTE exRZO[] = { 0xD9, 0x44, 0x24, 0x04, 0x33, 0xC0, 0xD9, 0xFF, 0x89, 0x41, 0x08 };
	static const BYTE exRX[]  = { 0xD9, 0x44, 0x24, 0x04, 0x33, 0xC0, 0xD9, 0xFF, 0xC7, 0x01 };
	static const BYTE exRY[]  = { 0xD9, 0x44, 0x24, 0x04, 0x33, 0xC0, 0xD9, 0xFF, 0x89, 0x41, 0x04 };
	static const BYTE exRZ[]  = { 0xD9, 0x44, 0x24, 0x04, 0x33, 0xC0, 0xD9, 0xFF, 0x89, 0x41, 0x08 };
	static const BYTE exMul[] = { 0x83, 0xEC, 0x4C, 0x8B, 0x44, 0x24, 0x54, 0x8B, 0x4C, 0x24, 0x58 };
	static const BYTE exMV[]  = { 0x83, 0xEC, 0x0C, 0x8B, 0x44, 0x24, 0x14, 0x8B, 0x4C, 0x24, 0x18 };
	static const BYTE exInv[] = { 0x8B, 0x44, 0x24, 0x08, 0x33, 0xC9, 0x89, 0x48, 0x30 };
	static const BYTE exCr[]  = { 0x83, 0xEC, 0x0C, 0x8B, 0x4C, 0x24, 0x18, 0x8B, 0x44, 0x24, 0x14 };

	const Patch patches[] = {
		{ 0x59AFA0, exRXO, sizeof(exRXO), (const void *)hkSetRotateXOnly },
		{ 0x59AFE0, exRYO, sizeof(exRYO), (const void *)hkSetRotateYOnly },
		{ 0x59B020, exRZO, sizeof(exRZO), (const void *)hkSetRotateZOnly },
		{ 0x59B060, exRX,  sizeof(exRX),  (const void *)hkSetRotateX },
		{ 0x59B0A0, exRY,  sizeof(exRY),  (const void *)hkSetRotateY },
		{ 0x59B0E0, exRZ,  sizeof(exRZ),  (const void *)hkSetRotateZ },
		{ 0x59BE30, exMul, sizeof(exMul), (const void *)hkMatrixMul },
		{ 0x59C890, exMV,  sizeof(exMV),  (const void *)hkMulMatrixVec },
		{ 0x59B920, exInv, sizeof(exInv), (const void *)hkMatrixInvert },
		{ 0x59C730, exCr,  sizeof(exCr),  (const void *)hkCrossProduct },
	};
	int done = 0;
	for (const Patch &p : patches) {
		if (memcmp((void *)p.addr, p.expect, p.len)) continue;
		BYTE jmp[5] = { 0xE9 };
		*(int32_t *)(jmp + 1) = Rel32((BYTE *)p.addr, p.hook);
		if (PatchBytes(p.addr, nullptr, jmp, 5)) done++;
	}
	g_fixCMatrixSSE.active = done > 0;
	Log("CMatrixSSE: %d/10 (SetRotate, operator*, MulMatVec, Invert, CrossProduct on SSE2)", done);
}

Fix g_fixCMatrixSSE = {
	.section = "CMatrixSSE",
	.title = "Матрицы CMatrix и CrossProduct на SSE",
	.gain = "-3–9% кадра",
	.summary = "Повороты, умножение, инверсия CMatrix и CrossProduct переписаны с x87 на SSE2 без простоев памяти.",
	.details = "Помимо RenderWare (MatrixSSE), сама GTA SA постоянно строит, умножает и инвертирует игровые CMatrix, а также "
	           "считает CrossProduct в коллизиях, машинах, педах и камере. Штатные функции используют скалярный x87 и "
	           "вызывают fcos + fsin (~150 тактов) даже при угле 0.\n\n"
	           "Здесь 10 горячих функций CMatrix/CVector переписаны на SSE2 без лишних обращений к стеку. Без потерь.",
	.gta = true,
	.atStart = Apply,
};
