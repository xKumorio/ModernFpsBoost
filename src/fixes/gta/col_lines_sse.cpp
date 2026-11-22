// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [ColLinesSSE] Replaces CCollision::ClosestSquaredDistanceBetweenFiniteLines (0x415A40, ~1.0% self
// even in empty interiors due to camera sphere-cast against every wall/floor polygon edge!).
// Original GTA SA uses 100% scalar x87 with 64 bytes of stack spills and 12 fnstsw ax pipeline stalls.
// Exact same Ericson/Sunday finite-segment squared distance formula rewritten in stall-free SSE2.

#include "../registry.h"
#include <math.h>
#include <xmmintrin.h>

struct Vec3 { float x, y, z; };

static inline float Dot3(__m128 a, __m128 b)
{
	__m128 m = _mm_mul_ps(a, b);
	__m128 shuf = _mm_shuffle_ps(m, m, _MM_SHUFFLE(2, 1, 0, 1));
	__m128 s = _mm_add_ss(m, shuf);
	s = _mm_add_ss(s, _mm_shuffle_ps(m, m, _MM_SHUFFLE(2, 2, 2, 2)));
	return _mm_cvtss_f32(s);
}

static inline __m128 Load3(const Vec3 *v)
{
	return _mm_set_ps(0.0f, v->z, v->y, v->x);
}

// __cdecl float ClosestSquaredDistanceBetweenFiniteLines(
//     const Vec3 *p1, const Vec3 *p2, const Vec3 *p3, const Vec3 *dir1, float dir1LenSq)
static float __cdecl hkClosestSqDistFiniteLines(const Vec3 *p1, const Vec3 *p2, const Vec3 *p3, const Vec3 *dir1, float a)
{
	const __m128 vp1 = Load3(p1);
	const __m128 vp2 = Load3(p2);
	const __m128 vp3 = Load3(p3);
	const __m128 u   = Load3(dir1);

	const __m128 v = _mm_sub_ps(vp3, vp2);
	const __m128 w = _mm_sub_ps(vp1, vp2);

	const float b = Dot3(v, u);
	const float c = Dot3(v, v);
	const float d = Dot3(w, u);
	const float e = Dot3(w, v);
	const float D = c * a - b * b;

	const float kEps = 1e-5f;
	float sN, sD = D;
	float tN, tD = D;

	if (D < kEps) {
		sN = 0.0f;
		sD = 1.0f;
		tN = e;
		tD = c;
	} else {
		sN = b * e - c * d;
		tN = a * e - b * d;
		if (sN < 0.0f) {
			sN = 0.0f;
			tN = e;
			tD = c;
		} else if (sN > sD) {
			sN = sD;
			tN = e + b;
			tD = c;
		}
	}

	if (tN < 0.0f) {
		tN = 0.0f;
		if (-d < 0.0f) {
			sN = 0.0f;
		} else if (-d > a) {
			sN = sD;
		} else {
			sN = -d;
			sD = a;
		}
	} else if (tN > tD) {
		tN = tD;
		if ((-d + b) < 0.0f) {
			sN = 0.0f;
		} else if ((-d + b) > a) {
			sN = sD;
		} else {
			sN = -d + b;
			sD = a;
		}
	}

	const float sc = (fabsf(sN) < kEps) ? 0.0f : (sN / sD);
	const float tc = (fabsf(tN) < kEps) ? 0.0f : (tN / tD);

	// diff = w + sc * u - tc * v
	const __m128 diff = _mm_sub_ps(_mm_add_ps(w, _mm_mul_ps(_mm_set1_ps(sc), u)),
	                               _mm_mul_ps(_mm_set1_ps(tc), v));
	return Dot3(diff, diff);
}

static void Apply()
{
	static const BYTE kExpect[] = { 0x83, 0xEC, 0x40, 0x8B, 0x44, 0x24, 0x48, 0x8B, 0x4C, 0x24, 0x4C, 0xD9, 0x01 };
	if (memcmp((void *)0x415A40, kExpect, sizeof(kExpect)) != 0) {
		Log("ColLinesSSE: prologue mismatch at 0x415A40 - skipped");
		return;
	}
	BYTE jmp[5] = { 0xE9 };
	*(int32_t *)(jmp + 1) = Rel32((BYTE *)0x415A40, (const void *)hkClosestSqDistFiniteLines);
	g_fixColLinesSSE.active = PatchBytes(0x415A40, nullptr, jmp, 5);
	Log(g_fixColLinesSSE.active
	        ? "ColLinesSSE: CCollision::ClosestSquaredDistanceBetweenFiniteLines on SSE2"
	        : "ColLinesSSE: patch failed");
}

Fix g_fixColLinesSSE = {
	.section = "ColLinesSSE",
	.title = "Коллизия отрезков камеры на SSE2",
	.gain = "-1% кадра",
	.summary = "Переписывает CCollision::ClosestSquaredDistanceBetweenFiniteLines (0x415A40) со скалярного x87 на SSE2.",
	.details = "При проверке столкновений камеры со стенами (SphereCastVersusVsPoly) GTA SA тысячи раз за кадр считает "
	           "расстояние между отрезками через скалярный x87 с 12 затыками fnstsw ax — это ~1% кадра даже в пустом доме.\n\n"
	           "Точная формула Эриксона переписана на векторный SSE2 без затыков FPU. 100% идентичный результат.",
	.gta = true,
	.atStart = Apply,
};
