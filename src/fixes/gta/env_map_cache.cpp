// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [EnvMapCache] CVehicle::PreRender (0x6D6480) calls CLinkedUpgradeList::SetEnvMapCoeff (0x4C9800)
// every frame for every visible vehicle: quantizes coeff*1000 to an int and walks every atomic and
// material of the car clump (RpClumpForAllAtomics -> RpGeometryForAllMaterials). For parked cars or
// cars under constant sky lighting the integer coefficient does not change between frames, yet the
// full clump walk runs 600 times/sec (~0.5% on 5 cars, several percent in a full parking lot).
// Caching the last integer coefficient per RpClump skips redundant clump walks with identical visuals.

#include <math.h>
#include <stdint.h>

#include "../registry.h"

static const DWORD kSetEnvMapCoeff = 0x4C9800;
static const DWORD kRpClumpForAllAtomics = 0x749B70;
static const DWORD kSetEnvMapAtomicCB = 0x4C9430;

struct EnvSlot {
	void *clump;
	int val;
	DWORD tick;
};
static EnvSlot g_envCache[256];
static unsigned g_envCalls, g_envSkipped;

static void __fastcall hkSetEnvMapCoeff(BYTE *modelInfo, void *, float coeff)
{
	void *clump = *(void **)(modelInfo + 0x1C);
	if (!clump) return;
	const int val = (int)floorf(coeff * 1000.0f);
	g_envCalls++;

	const DWORD now = GetTickCount();
	const uint32_t idx = (((uintptr_t)clump >> 4) * 2654435761u) & 255u;
	EnvSlot &s = g_envCache[idx];
	// Refresh at least once per second in case a model was reloaded at the same clump pointer
	if (s.clump == clump && s.val == val && now - s.tick < 1000) {
		g_envSkipped++;
		return;
	}
	s = { clump, val, now };
	((void *(__cdecl *)(void *, DWORD, int))kRpClumpForAllAtomics)(clump, kSetEnvMapAtomicCB, val);
}

static void Apply()
{
	static const BYTE expect[] = { 0xD9, 0x44, 0x24, 0x04, 0x56, 0xD8, 0x0D, 0x4C, 0x8C, 0x85, 0x00 };
	if (memcmp((void *)kSetEnvMapCoeff, expect, sizeof(expect))) {
		Log("EnvMapCache: SetEnvMapCoeff at 0x4C9800 is not original - skipped");
		return;
	}
	BYTE jmp[5] = { 0xE9 };
	*(int32_t *)(jmp + 1) = Rel32((BYTE *)kSetEnvMapCoeff, hkSetEnvMapCoeff);
	g_fixEnvMapCache.active = PatchBytes(kSetEnvMapCoeff, nullptr, jmp, 5);
	Log(g_fixEnvMapCache.active ? "EnvMapCache: redundant vehicle env-map clump walks skipped" : "EnvMapCache: patch failed");
}

static void Report(std::string &out, double secs)
{
	if (g_envCalls)
		Appendf(out, Tr("EnvMapCache: пропущено %.0f%% из %.0f обходов отражений машин/с\n", "EnvMapCache: skipped %.0f%% of %.0f vehicle reflection walks/s\n"), 100.0 * g_envSkipped / g_envCalls, g_envCalls / secs);
	g_envCalls = g_envSkipped = 0;
}

Fix g_fixEnvMapCache = {
	.section = "EnvMapCache",
	.title = "Кеш отражений машин",
	.gain = "-0.5–2% кадра",
	.summary = "Не обходит все детали и материалы каждой машины, если яркость её отражений не изменилась.",
	.details = "Каждый кадр в CVehicle::PreRender игра переводит яркость отражений машины в целое число и запускает "
	           "RpClumpForAllAtomics по всем деталям и материалам кузова — даже когда машина стоит на месте и число "
	           "точно такое же, как кадр назад. На 5 машинах это ~0.5% кадра, на парковке из 30 машин — уже несколько процентов.\n\n"
	           "Последнее значение запоминается по указателю модели (с обновлением раз в секунду). Картинка идентична.",
	.gta = true,
	.atStart = Apply,
	.report = Report,
};
