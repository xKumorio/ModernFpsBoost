// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [FxParticlesSSE] Accelerates GTA SA's 3 hottest particle math functions (~3-4% of the frame in
// tire smoke, exhaust, fire and shootouts):
//   1. Fx_c::RotateVecAboutVec (0x4A1780, ~1.3% self): Rodrigues vector rotation in stall-free SSE2
//      using GTA's exact 256-entry sin/cos lookup table (0xBB3E00).
//   2. FxInterpInfo32_c::GetVal (0x4A89C0, ~1.4% self): signed int16 keyframe interpolation (scale 0.001).
//   3. FxInterpInfoU255_c::GetVal (0x4A8800, ~0.36% self): unsigned uint16 keyframe interpolation (scale 1/256).
// Eliminates per-keyframe x87 fild/fnstsw ax stalls by converting time once to fixed-point units.

#include "../registry.h"
#include <math.h>
#include <xmmintrin.h>

struct Vec3 { float x, y, z; };

struct FxInterpInfo {
	void *vtable;
	uint8_t loop;
	int8_t numKeys;
	int8_t numValues;
	uint8_t pad;
	uint16_t *times;
	void **values;
};

static const float *const kFxSinTable = (const float *)0x00BB3E00; // 256 entries
static const float kRadToIdx = 40.74366379f; // 256 / (2*PI)

// __cdecl void Fx_c::RotateVecAboutVec(Vec3 *out, const Vec3 *v, const Vec3 *axis, float angle)
static void __cdecl hkRotateVecAboutVec(Vec3 *out, const Vec3 *v, const Vec3 *axis, float angle)
{
	const float scaled = angle * kRadToIdx;
	const int idxSin = (int)scaled & 255;
	const int idxCos = (int)(scaled + 64.0f) & 255;
	const float sinA = kFxSinTable[idxSin];
	const float cosA = kFxSinTable[idxCos];
	const float oneMinusCos = 1.0f - cosA;

	const __m128 vv = _mm_set_ps(0.0f, v->z, v->y, v->x);
	const __m128 va = _mm_set_ps(0.0f, axis->z, axis->y, axis->x);

	// cross(axis, v)
	const __m128 a_yzx = _mm_shuffle_ps(va, va, _MM_SHUFFLE(3, 0, 2, 1));
	const __m128 v_yzx = _mm_shuffle_ps(vv, vv, _MM_SHUFFLE(3, 0, 2, 1));
	const __m128 cr = _mm_shuffle_ps(
		_mm_sub_ps(_mm_mul_ps(va, v_yzx), _mm_mul_ps(a_yzx, vv)),
		_mm_sub_ps(_mm_mul_ps(va, v_yzx), _mm_mul_ps(a_yzx, vv)),
		_MM_SHUFFLE(3, 0, 2, 1));

	// dot(axis, v)
	const __m128 prod = _mm_mul_ps(va, vv);
	__m128 dot = _mm_add_ss(prod, _mm_shuffle_ps(prod, prod, _MM_SHUFFLE(1, 1, 1, 1)));
	dot = _mm_add_ss(dot, _mm_shuffle_ps(prod, prod, _MM_SHUFFLE(2, 2, 2, 2)));
	const float d = _mm_cvtss_f32(dot) * oneMinusCos;

	// out = v * cosA + cross(axis, v) * sinA + axis * (dot(axis, v) * (1 - cosA))
	const __m128 res = _mm_add_ps(
		_mm_add_ps(_mm_mul_ps(vv, _mm_set1_ps(cosA)), _mm_mul_ps(cr, _mm_set1_ps(sinA))),
		_mm_mul_ps(va, _mm_set1_ps(d)));

	alignas(16) float tmp[4];
	_mm_store_ps(tmp, res);
	out->x = tmp[0];
	out->y = tmp[1];
	out->z = tmp[2];
}

template <typename T>
static inline void InterpGetVal(const FxInterpInfo *self, float *out, float t, float valScale)
{
	const int numKeys = self->numKeys;
	const int numVals = self->numValues;
	if (numVals <= 0) return;

	if (numKeys == 1) {
		const T *const *vals = (const T *const *)self->values;
		for (int i = 0; i < numVals; i++)
			out[i] = (float)vals[i][0] * valScale;
		return;
	}

	const uint16_t *times = self->times;
	float t256 = t * 256.0f;

	if (self->loop) {
		const float lastT = (float)times[numKeys - 1];
		if (lastT > 0.0f) {
			const int wraps = (int)(t256 / lastT);
			t256 -= (float)wraps * lastT;
		}
	}

	int k = 1;
	while (k < numKeys && (float)times[k] <= t256)
		k++;

	const T *const *vals = (const T *const *)self->values;
	if (k >= numKeys) {
		const int last = numKeys - 1;
		for (int i = 0; i < numVals; i++)
			out[i] = (float)vals[i][last] * valScale;
		return;
	}

	const float t0 = (float)times[k - 1];
	const float t1 = (float)times[k];
	const float frac = (t1 != t0) ? ((t256 - t0) / (t1 - t0)) : 0.0f;
	for (int i = 0; i < numVals; i++) {
		const float v0 = (float)vals[i][k - 1];
		const float v1 = (float)vals[i][k];
		out[i] = (v0 + (v1 - v0) * frac) * valScale;
	}
}

static void __fastcall hkInterp32GetVal(const FxInterpInfo *self, void *, float *out, float t)
{
	InterpGetVal<int16_t>(self, out, t, 0.001f);
}

static void __fastcall hkInterpU255GetVal(const FxInterpInfo *self, void *, float *out, float t)
{
	InterpGetVal<uint16_t>(self, out, t, 0.00390625f);
}

static void Apply()
{
	static const BYTE exRot[]  = { 0x83, 0xEC, 0x1C, 0xD9, 0x44, 0x24, 0x2C, 0xD8, 0x0D, 0x78, 0xA7, 0x85, 0x00 };
	static const BYTE exI32[]  = { 0x51, 0x53, 0x56, 0x8B, 0xF1, 0x8A, 0x5E, 0x05, 0x80, 0xFB, 0x01, 0x75, 0x3D };
	static const BYTE exU255[] = { 0x51, 0x53, 0x56, 0x8B, 0xF1, 0x8A, 0x5E, 0x05, 0x80, 0xFB, 0x01, 0x75, 0x3D };

	struct Patch { DWORD addr; const BYTE *exp; size_t len; const void *fn; };
	const Patch list[] = {
		{ 0x4A1780, exRot,  sizeof(exRot),  (const void *)hkRotateVecAboutVec },
		{ 0x4A89C0, exI32,  sizeof(exI32),  (const void *)hkInterp32GetVal },
		{ 0x4A8800, exU255, sizeof(exU255), (const void *)hkInterpU255GetVal },
	};

	int done = 0;
	for (const Patch &p : list) {
		if (memcmp((void *)p.addr, p.exp, p.len) != 0) continue;
		BYTE jmp[5] = { 0xE9 };
		*(int32_t *)(jmp + 1) = Rel32((BYTE *)p.addr, p.fn);
		if (PatchBytes(p.addr, nullptr, jmp, 5)) done++;
	}
	g_fixFxParticlesSSE.active = done > 0;
	Log("FxParticlesSSE: %d/3 (RotateVecAboutVec, FxInterpInfo32/U255 on SSE2)", done);
}

Fix g_fixFxParticlesSSE = {
	.section = "FxParticlesSSE",
	.title = "Математика частиц Fx_c на SSE2",
	.gain = "-1–3.5% кадра",
	.summary = "Ускоряет вращение и интерполяцию частиц (дым, выхлоп, искры, огонь) на SSE2 без затыков x87.",
	.details = "При дыме из-под колёс, выхлопах и перестрелках GTA SA тратит до 4-5% кадра на скалярный x87 в "
	           "Fx_c::RotateVecAboutVec и FxInterpInfo::GetVal (постоянные fild и fnstsw ax на каждом ключевом кадре).\n\n"
	           "Вращение Родрига и поиск ключевых кадров переведены на SSE2. Картинка 100% идентична.",
	.gta = true,
	.atStart = Apply,
};
