// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [ColBoxSSE] Three hot collision broadphase functions in gta_sa.exe 1.0 US:
// 1. CCollision::TestSphereBox (0x4120C0, ~1.3% self): 6 sequential x87 fcomp + fnstsw ax checks ->
//    6 branchless SSE2 instructions.
// 2. CEntity::GetIsTouching(CEntity *) (0x5343F0) and GetIsTouching(const CVector *, float) (0x5344B0,
//    ~2.05% self): each vanilla call calls TransformFromObjectSpace (0x5334F0), looks up modelInfo twice,
//    and computes squared distance in x87 + fnstsw ax. Here bound-center transform is inlined in SSE2
//    with a single colModel lookup and SSE2 squared-distance comparison.

#include <immintrin.h>
#include <math.h>
#include <stdint.h>

#include "../registry.h"

struct Vec3 { float x, y, z; };
struct ColSphere { float x, y, z, r; };
struct ColBox { float minX, minY, minZ, maxX, maxY, maxZ; };
struct CMatrix {
	float rx, ry, rz; DWORD pad0;
	float ux, uy, uz; DWORD pad1;
	float ax, ay, az; DWORD pad2;
	float px, py, pz; DWORD pad3;
};

static const DWORD kTestSphereBox      = 0x4120C0;
static const DWORD kGetIsTouchingEnt   = 0x5343F0;
static const DWORD kGetIsTouchingPt    = 0x5344B0;
static const DWORD kModelInfoPtrs      = 0xA9B0C8;

static bool __cdecl hkTestSphereBox(const ColSphere *s, const ColBox *b)
{
	__m128 c = _mm_loadu_ps(&s->x);          // [x, y, z, r]
	__m128 r = _mm_shuffle_ps(c, c, 0xFF);   // [r, r, r, r]
	__m128 bmin = _mm_loadu_ps(&b->minX);    // [minX, minY, minZ, maxX]
	__m128 bmax = _mm_set_ps(0.0f, b->maxZ, b->maxY, b->maxX);
	__m128 ok = _mm_and_ps(_mm_cmpge_ps(_mm_add_ps(c, r), bmin),
	                       _mm_cmple_ps(_mm_sub_ps(c, r), bmax));
	return (_mm_movemask_ps(ok) & 7) == 7;
}

static inline void FastSinCos(float a, float &s, float &c)
{
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

static inline bool BoundSphereWorld(const BYTE *ent, __m128 &outCenter, float &outRadius)
{
	// Read live ms_modelInfoPtrs pointer & sign-extension opcode from 0x5343F6/0x5343FD so
	// OpenSAA, III.VC.SA.LimitAdjuster, fastman92, and SA-MP 0.3.DL custom models never crash.
	const BYTE *const *ptrs = *(const BYTE *const *const *)0x5343FD;
	const int modelId = (*(const BYTE *)0x5343F7 == 0xB7)
		? (int)*(const uint16_t *)(ent + 0x22)
		: (int)*(const int16_t *)(ent + 0x22);
	if (!ptrs || modelId < 0) return false;
	if (ptrs == (const BYTE *const *)kModelInfoPtrs && modelId >= 20000) return false;
	const BYTE *mi = ptrs[modelId];
	if (!mi) return false;
	const BYTE *col = *(const BYTE *const *)(mi + 0x14);
	if (!col) return false;
	// CColModel::m_boundSphere at col+0x18: { cx, cy, cz, radius }
	const float *bs = (const float *)(col + 0x18);
	outRadius = bs[3];

	const CMatrix *m = *(const CMatrix *const *)(ent + 0x14);
	if (m) {
		outCenter = _mm_add_ps(
			_mm_add_ps(_mm_mul_ps(_mm_set1_ps(bs[0]), _mm_loadu_ps(&m->rx)),
			           _mm_mul_ps(_mm_set1_ps(bs[1]), _mm_loadu_ps(&m->ux))),
			_mm_add_ps(_mm_mul_ps(_mm_set1_ps(bs[2]), _mm_loadu_ps(&m->ax)),
			           _mm_loadu_ps(&m->px)));
		return true;
	}
	// CSimpleTransform at ent+0x04: { x, y, z, heading }
	const float *st = (const float *)(ent + 0x04);
	const float h = st[3];
	if (h == 0.0f) {
		outCenter = _mm_add_ps(_mm_loadu_ps(st), _mm_loadu_ps(bs));
		return true;
	}
	float s, c;
	FastSinCos(h, s, c);
	outCenter = _mm_set_ps(0.0f, st[2] + bs[2], c * bs[1] + s * bs[0] + st[1], c * bs[0] - s * bs[1] + st[0]);
	return true;
}

static inline float DistSq3(__m128 a, __m128 b)
{
	__m128 d = _mm_sub_ps(a, b);
	__m128 d2 = _mm_mul_ps(d, d);
	__m128 y = _mm_shuffle_ps(d2, d2, _MM_SHUFFLE(1, 1, 1, 1));
	__m128 z = _mm_movehl_ps(d2, d2);
	return _mm_cvtss_f32(_mm_add_ss(_mm_add_ss(d2, y), z));
}

static int __fastcall hkGetIsTouchingEnt(const BYTE *self, void *, const BYTE *other)
{
	__m128 c1, c2;
	float r1, r2;
	if (!self || !other || !BoundSphereWorld(self, c1, r1) || !BoundSphereWorld(other, c2, r2))
		return 0;
	float rsum = r1 + r2;
	return DistSq3(c1, c2) < rsum * rsum ? 1 : 0;
}

static int __fastcall hkGetIsTouchingPt(const BYTE *self, void *, const Vec3 *pt, float radius)
{
	__m128 c1;
	float r1;
	if (!self || !pt || !BoundSphereWorld(self, c1, r1))
		return 0;
	__m128 c2 = _mm_set_ps(0.0f, pt->z, pt->y, pt->x);
	float rsum = r1 + radius;
	return DistSq3(c1, c2) < rsum * rsum ? 1 : 0;
}

static void Apply()
{
	static const BYTE exBox[]  = { 0x8B, 0x4C, 0x24, 0x04, 0xD9, 0x01, 0x8B, 0x54, 0x24, 0x08, 0xD8, 0x41, 0x0C };
	static const BYTE exEnt[]  = { 0x83, 0xEC, 0x18, 0x56, 0x8B, 0xF1, 0x0F, 0xBF, 0x46, 0x22 };
	static const BYTE exPt[]   = { 0x83, 0xEC, 0x0C, 0x56, 0x8B, 0xF1, 0x0F, 0xBF, 0x46, 0x22, 0x90, 0x90, 0xE9 };
	int done = 0;
	if (!memcmp((void *)kTestSphereBox, exBox, sizeof(exBox))) {
		BYTE jmp[5] = { 0xE9 };
		*(int32_t *)(jmp + 1) = Rel32((BYTE *)kTestSphereBox, hkTestSphereBox);
		if (PatchBytes(kTestSphereBox, nullptr, jmp, 5)) done++;
	}
	if (!memcmp((void *)kGetIsTouchingEnt, exEnt, sizeof(exEnt))) {
		BYTE jmp[5] = { 0xE9 };
		*(int32_t *)(jmp + 1) = Rel32((BYTE *)kGetIsTouchingEnt, hkGetIsTouchingEnt);
		if (PatchBytes(kGetIsTouchingEnt, nullptr, jmp, 5)) done++;
	}
	if (!memcmp((void *)kGetIsTouchingPt, exPt, sizeof(exPt))) {
		BYTE jmp[5] = { 0xE9 };
		*(int32_t *)(jmp + 1) = Rel32((BYTE *)kGetIsTouchingPt, hkGetIsTouchingPt);
		if (PatchBytes(kGetIsTouchingPt, nullptr, jmp, 5)) done++;
	}
	g_fixColBoxSSE.active = done > 0;
	Log("ColBoxSSE: %d/3 (TestSphereBox + GetIsTouching on SSE2)", done);
}

Fix g_fixColBoxSSE = {
	.section = "ColBoxSSE",
	.title = "Сферы и боксы коллизий на SSE",
	.gain = "-2.5–3.5% кадра",
	.summary = "Быстрые SSE2-проверки пересечения сфер и боксов (TestSphereBox и GetIsTouching) без x87 и лишних вызовов.",
	.details = "Перед детальной проверкой треугольников игра тысячи раз за кадр проверяет пересечение ограничивающих сфер и "
	           "боксов (GetIsTouching — ~2% кадра, TestSphereBox — ~1.3%). Штатно это скалярный x87 с 6 подряд fnstsw ax, "
	           "двойным чтением таблицы моделей и лишними вызовами TransformFromObjectSpace.\n\n"
	           "Здесь TestSphereBox выполняется за 6 векторных команд SSE2 без ветвлений, а GetIsTouching считает центр и "
	           "квадрат дистанции напрямую в XMM-регистрах. Физика та же.",
	.gta = true,
	.atStart = Apply,
};
