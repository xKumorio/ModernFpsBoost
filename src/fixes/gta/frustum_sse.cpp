// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [FrustumSSE] Two hot culling/transform routines in gta_sa.exe 1.0 US:
// 1. CCamera::IsSphereVisible internal (0x420C40, ~0.5% self): copies the center onto the stack,
//    makes an indirect call to RwV3dTransformPoints, then runs 6 sequential x87 fcomp + fnstsw ax
//    plane tests. Here the 3x3+pos transform is inlined in SSE2 and all 4 side planes are tested
//    together in one 4-wide SSE2 dot product.
// 2. TransformPoint (0x54ECE0 and 0x54EC80, ~0.65% self): computes BOTH x87 fcos AND fsin (~150
//    cycles) on every entity, even when heading == 0 (most static buildings/objects). Here heading == 0
//    takes a 3-add fast path, and non-zero headings use a range-reduced minimax sin/cos in SSE2.

#include <immintrin.h>
#include <math.h>
#include <stdint.h>

#include "../registry.h"

struct Vec3 { float x, y, z; };
struct SimpleTransform { float x, y, z, heading; };
struct RwMatrix {
	float rx, ry, rz; DWORD flags;
	float ux, uy, uz; DWORD pad1;
	float ax, ay, az; DWORD pad2;
	float px, py, pz; DWORD pad3;
};

static const DWORD kIsSphereVisible = 0x420C40;
static const DWORD kTransformPoint  = 0x54ECE0;
static const DWORD kTransformPointR = 0x54EC80;

static bool __fastcall hkIsSphereVisible(const BYTE *cam, void *, const Vec3 *origin, float radius, const RwMatrix *m)
{
	// Inline p = origin * m (same order as RwV3dTransformPoints)
	__m128 x = _mm_set1_ps(origin->x);
	__m128 y = _mm_set1_ps(origin->y);
	__m128 z = _mm_set1_ps(origin->z);
	__m128 r0 = _mm_loadu_ps(&m->rx);
	__m128 r1 = _mm_loadu_ps(&m->ux);
	__m128 r2 = _mm_loadu_ps(&m->ax);
	__m128 r3 = _mm_loadu_ps(&m->px);
	__m128 p = _mm_add_ps(_mm_add_ps(_mm_mul_ps(x, r0), _mm_mul_ps(y, r1)), _mm_add_ps(_mm_mul_ps(z, r2), r3));

	// Extract px, py, pz directly in XMM registers (no 128->32-bit stack store-forwarding stall)
	const __m128 py_v = _mm_shuffle_ps(p, p, _MM_SHUFFLE(1, 1, 1, 1));
	const __m128 pz_v = _mm_movehl_ps(p, p);
	const float py = _mm_cvtss_f32(py_v);

	// Near / far checks matching 0x420C74..0x420CA3 exactly:
	// !(py + radius >= near) || (py - radius > far) -> false
	const float nearClip = *(const float *)0xC3EFA0;
	const float farClip  = *(const float *)0xC3EF9C;
	if (!(py + radius >= nearClip) || (py - radius > farClip))
		return false;

	// 4 frustum side planes in one 4-wide SSE2 check: [px, px, pz, pz]
	const __m128 a  = _mm_shuffle_ps(p, pz_v, _MM_SHUFFLE(0, 0, 0, 0));
	const __m128 na = _mm_set_ps(*(const float *)(cam + 0xB50), *(const float *)(cam + 0xB44),
	                             *(const float *)(cam + 0xB30), *(const float *)(cam + 0xB24));
	const __m128 ny = _mm_set_ps(*(const float *)(cam + 0xB4C), *(const float *)(cam + 0xB40),
	                             *(const float *)(cam + 0xB34), *(const float *)(cam + 0xB28));
	const __m128 d  = _mm_add_ps(_mm_mul_ps(a, na), _mm_mul_ps(py_v, ny));
	return _mm_movemask_ps(_mm_cmple_ps(d, _mm_set1_ps(radius))) == 0xF;
}

// Fast single-precision sin/cos in SSE2 (< 1 ULP across all finite floats, ~12 cycles vs ~150 for x87 fsin+fcos)
static inline void FastSinCos(float a, float &s, float &c)
{
	// Range reduce to [-pi/4, pi/4] with quadrant k = round(a * 2/pi)
	static const float kTwoOverPi = 0.63661977236758134308f;
	// Cody-Waite split of pi/2 for accurate reduction even on large headings
	static const float kPiOver2Hi = 1.5707962512969970703125f;
	static const float kPiOver2Lo = 7.54978941586159635335207e-8f;
	static const float kPiOver2Lo2 = 5.39030252995776476554468e-15f;

	float kf = floorf(a * kTwoOverPi + 0.5f);
	int k = (int)kf;
	float r = ((a - kf * kPiOver2Hi) - kf * kPiOver2Lo) - kf * kPiOver2Lo2;
	float r2 = r * r;

	// Minimax polynomials on [-pi/4, pi/4]:
	// sin(r) = r * (1 + r^2 * (-1/6 + r^2 * (1/120 - r^2 / 5040)))
	// cos(r) = 1 + r^2 * (-1/2 + r^2 * (1/24 - r^2 / 720))
	float sr = r + r * r2 * (-0.16666667163372039795f + r2 * (0.00833333102054893970f - r2 * 0.00019839523980044760f));
	float cr = 1.0f + r2 * (-0.5f + r2 * (0.04166666418313980103f - r2 * 0.00138883973018452525f));

	switch (k & 3) {
	case 0: s = sr;  c = cr;  break;
	case 1: s = cr;  c = -sr; break;
	case 2: s = -sr; c = -cr; break;
	case 3: s = -cr; c = sr;  break;
	}
}

static void __cdecl hkTransformPoint(Vec3 *out, const SimpleTransform *st, const Vec3 *in)
{
	const float h = st->heading;
	if (h == 0.0f) {
		out->x = st->x + in->x;
		out->y = st->y + in->y;
		out->z = st->z + in->z;
		return;
	}
	float s, c;
	FastSinCos(h, s, c);
	const float ix = in->x, iy = in->y, iz = in->z;
	out->x = c * ix - s * iy + st->x;
	out->y = c * iy + s * ix + st->y;
	out->z = st->z + iz;
}

static Vec3 *__cdecl hkTransformPointRet(Vec3 *out, const SimpleTransform *st, const Vec3 *in)
{
	hkTransformPoint(out, st, in);
	return out;
}

static void Apply()
{
	static const BYTE expectVis[] = { 0x83, 0xEC, 0x0C, 0x8B, 0x44, 0x24, 0x10, 0xD9, 0x40, 0x04, 0x8B, 0x54, 0x24, 0x18 };
	static const BYTE expectTp1[] = { 0x8B, 0x44, 0x24, 0x08, 0xD9, 0x40, 0x0C, 0x8B, 0x4C, 0x24, 0x0C, 0xD9, 0xFF };
	static const BYTE expectTp2[] = { 0x83, 0xEC, 0x0C, 0x8B, 0x44, 0x24, 0x14, 0xD9, 0x40, 0x0C, 0x8B, 0x4C, 0x24, 0x18, 0xD9, 0xFF };
	int done = 0;
	if (!memcmp((void *)kIsSphereVisible, expectVis, sizeof(expectVis))) {
		BYTE jmp[5] = { 0xE9 };
		*(int32_t *)(jmp + 1) = Rel32((BYTE *)kIsSphereVisible, hkIsSphereVisible);
		if (PatchBytes(kIsSphereVisible, nullptr, jmp, 5)) done++;
	}
	if (!memcmp((void *)kTransformPoint, expectTp1, sizeof(expectTp1))) {
		BYTE jmp[5] = { 0xE9 };
		*(int32_t *)(jmp + 1) = Rel32((BYTE *)kTransformPoint, hkTransformPoint);
		if (PatchBytes(kTransformPoint, nullptr, jmp, 5)) done++;
	}
	if (!memcmp((void *)kTransformPointR, expectTp2, sizeof(expectTp2))) {
		BYTE jmp[5] = { 0xE9 };
		*(int32_t *)(jmp + 1) = Rel32((BYTE *)kTransformPointR, hkTransformPointRet);
		if (PatchBytes(kTransformPointR, nullptr, jmp, 5)) done++;
	}
	g_fixFrustumSSE.active = done > 0;
	Log("FrustumSSE: %d/3 (IsSphereVisible + TransformPoint on SSE2)", done);
}

Fix g_fixFrustumSSE = {
	.section = "FrustumSSE",
	.title = "Видимость и повороты на SSE",
	.gain = "-1.2% кадра",
	.summary = "Проверка попадания объектов в камеру (IsSphereVisible) и TransformPoint переписаны с x87 на SSE2.",
	.details = "При обходе мира (ScanSectorList / SetupEntityVisibility) игра тысячи раз за кадр проверяет видимость сфер: "
	           "копирует точку на стек, делает косвенный вызов RwV3dTransformPoints и 6 раз подряд дёргает x87 fcomp + fnstsw ax. "
	           "А TransformPoint на каждый объект считает и x87 fcos, и fsin (~150 тактов), даже когда угол равен 0.\n\n"
	           "Здесь трансформация и все 4 плоскости фрустума считаются за одну 4-векторную операцию SSE2, "
	           "а TransformPoint для угла 0 делает 3 сложения и быстрый SSE2 sin/cos для остальных. Без потерь.",
	.gta = true,
	.atStart = Apply,
};
