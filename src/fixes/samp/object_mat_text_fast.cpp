// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [ObjectMatTextFast] Every frame, CObjectPool::Process (samp+0xF660 in 0.3.7-R1) calls
// CObject::CreateMaterialTextTextures (samp+0xA47F0) for every object with materials. Inside
// samp+0xA47F0, a scalar 16-iteration loop walks 3 separate arrays plus a 0xD7-strided struct array
// (3.4 KB per object) just to check `tex[i] != 0 && type[i] == 2 && created[i] == 0` (~0.6% CPU).
//
// Replacing the pre-check with 4-wide SSE2 over the three contiguous 16-dword arrays checks all
// 16 slots in ~12 ns with zero branches and 100% exactness (zero staleness), falling through to
// original samp+0xA47F0 only when a material-text slot genuinely needs texture creation.

#include "../registry.h"
#include <emmintrin.h>

typedef void(__thiscall *ProcessMatText_t)(BYTE *obj);
static ProcessMatText_t oProcessMatText;
static unsigned g_calls, g_skipped;

static void __fastcall hkProcessMaterialText(BYTE *obj, void *)
{
	g_calls++;
	const __m128i zero = _mm_setzero_si128();
	const __m128i two  = _mm_set1_epi32(2);
	__m128i hit = zero;

	// Layout in SA-MP 0.3.7-R1 CObject:
	//   +0x217: m_pMaterialTexture[16]     (16 x 4B = 64B)
	//   +0x2DB: m_nMaterialType[16]        (16 x 4B = 64B)  (2 == MATERIAL_TEXT)
	//   +0x31B: m_bMaterialTextCreated[16] (16 x 4B = 64B)  (0 == needs creation)
	for (int i = 0; i < 64; i += 16) {
		__m128i t = _mm_loadu_si128((const __m128i *)(obj + 0x2DB + i));
		__m128i c = _mm_loadu_si128((const __m128i *)(obj + 0x31B + i));
		__m128i x = _mm_loadu_si128((const __m128i *)(obj + 0x217 + i));
		__m128i need = _mm_and_si128(_mm_cmpeq_epi32(t, two), _mm_cmpeq_epi32(c, zero));
		need = _mm_andnot_si128(_mm_cmpeq_epi32(x, zero), need);
		hit = _mm_or_si128(hit, need);
	}

	if (_mm_movemask_epi8(hit)) {
		oProcessMatText(obj);
		return;
	}
	g_skipped++;
}

static bool Apply(BYTE *)
{
	BYTE *callSite = SampAt(S_OBJ_MATTEXT_SITE);
	BYTE *target   = SampAt(S_OBJ_MATTEXT_FN);
	if (!callSite || !target || callSite[0] != 0xE8 || callSite + 5 + *(int32_t *)(callSite + 1) != target) {
		Log("ObjectMatTextFast: call site mismatch on %s - skipped", SampVersionName());
		return true;
	}

	static const BYTE kExpect[] = { 0x83, 0xEC, 0x08, 0x53, 0x55, 0x56, 0x8B, 0xE9, 0x57, 0x8D, 0xBD, 0xDB, 0x02, 0x00, 0x00 };
	if (memcmp(target, kExpect, sizeof(kExpect)) != 0) {
		Log("ObjectMatTextFast: prologue mismatch on %s - skipped", SampVersionName());
		return true;
	}

	oProcessMatText = (ProcessMatText_t)target;
	int32_t rel = Rel32(callSite, (const void *)hkProcessMaterialText);
	BYTE *addrs[] = { callSite + 1 };
	const void *data[] = { &rel };
	size_t sizes[] = { 4 };
	g_fixObjectMatTextFast.active = PatchMainThread(addrs, data, sizes, 1);
	Log(g_fixObjectMatTextFast.active
	        ? "ObjectMatTextFast: SSE2 fast-path for CObject::CreateMaterialTextTextures (%s)"
	        : "ObjectMatTextFast: patch failed",
	    SampVersionName());
	return true;
}

static void Report(std::string &out, double secs)
{
	if (g_calls)
		Appendf(out, "ObjectMatTextFast: пропущено %.0f%% из %.0f проверок/с\n", 100.0 * g_skipped / g_calls, g_calls / secs);
	g_calls = g_skipped = 0;
}

Fix g_fixObjectMatTextFast = {
	.section = "ObjectMatTextFast",
	.title = "Быстрая проверка 3D-текста на объектах (SSE2)",
	.gain = "-0.6% кадра",
	.summary = "Проверяет все 16 слотов материалов объекта SA-MP векторно на SSE2 вместо 16 ветвлений по 3.4 КБ памяти.",
	.details = "Каждый кадр SA-MP обходит все объекты с материалами и в цикле из 16 итераций (samp+0xA47F0) проверяет, "
	           "не нужно ли создать текстуру для SetObjectMaterialText — даже если текстуры уже давно созданы или на "
	           "объекте обычные материалы.\n\n"
	           "SSE2 проверяет все 16 слотов за ~12 нс без единого ветвления и вызывает оригинальную функцию только тогда, "
	           "когда реально появился несозданный текст. Без кеша и без потерь.",
	.atSamp = Apply,
	.report = Report,
};
