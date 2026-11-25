// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [VehicleLod] A vehicle is ~55 draw calls (every part and material of the detailed model, most of
// them through the sorted alpha list) up to ~100 m from the camera, and only beyond that the game
// switches to the model's own low-detail atomic ("_vlo", 1-2 draw calls). The switch is made by the
// atomics' render callbacks, which compare gVehicleDistanceFromCamera (0xC88024, squared, set per
// vehicle by CVisibilityPlugins::SetupVehicleVariables 0x733160) with ms_vehicleLod0Dist (0xC88040),
// or ms_bigVehicleLod0Dist (0xC88038) for big vehicles.
// The one call of SetupVehicleVariables (0x553293, CRenderer::RenderOneNonRoad) is wrapped: for a
// vehicle farther than lod_m whose clump has a low-detail atomic, the distance the callbacks see is
// raised to the game's threshold, so the detailed parts are skipped and the low-detail one is drawn.
// A model with no low-detail atomic (some replaced cars) is left as it is: it would not be drawn at all.

#include "../registry.h"

static const DWORD kSetupSite = 0x553293; // call SetupVehicleVariables in RenderOneNonRoad
static const DWORD kSetupVehicleVariables = 0x733160;
static float *const kVehicleDist = (float *)0xC88024;    // gVehicleDistanceFromCamera
static float *const kLod0Dist = (float *)0xC88040;       // ms_vehicleLod0Dist
static float *const kBigLod0Dist = (float *)0xC88038;    // ms_bigVehicleLod0Dist
static const DWORD kLowDetailCB = 0x7331E0;              // RenderVehicleReallyLowDetailCB
static const DWORD kLowDetailBoatCB = 0x7334F0;          // RenderVehicleLoDetailCB_Boat
static const DWORD kLowDetailBigCB = 0x732820;           // RenderVehicleReallyLowDetailCB_BigVehicle

static float g_lodM = 30.0f;
static DWORD g_low, g_detailed, g_noLow;

// the threshold of the clump's low-detail atomic, or null when it has none
// RpClump: atomic list at +8; RpAtomic: clump link at +0x40, render callback at +0x48
static const float *LowDetailThreshold(BYTE *clump)
{
	BYTE *head = clump + 8;
	int n = 0;
	for (BYTE *link = *(BYTE **)head; link && link != head && n < 256; link = *(BYTE **)link, n++) {
		DWORD cb = *(DWORD *)(link - 0x40 + 0x48);
		if (cb == kLowDetailCB || cb == kLowDetailBoatCB) return kLod0Dist;
		if (cb == kLowDetailBigCB) return kBigLod0Dist;
	}
	return nullptr;
}

static void __cdecl hkSetupVehicleVariables(BYTE *clump)
{
	((void(__cdecl *)(BYTE *))kSetupVehicleVariables)(clump);
	if (!clump || *clump != 2) return; // rpCLUMP, as the function itself checks
	float dist = *kVehicleDist;
	if (dist < g_lodM * g_lodM) {
		g_detailed++;
		return;
	}
	const float *threshold = LowDetailThreshold(clump);
	if (!threshold) {
		g_noLow++;
		return;
	}
	g_low++;
	if (dist < *threshold) *kVehicleDist = *threshold;
}

static void Load(const Ini &ini)
{
	g_lodM = ini.Float("VehicleLod", "lod_m", 30.0f);
}

static void Apply()
{
	if (g_lodM <= 0) return;
	if (!IsCallTo((BYTE *)kSetupSite, kSetupVehicleVariables)) {
		Log("VehicleLod: the SetupVehicleVariables call is not original - skipped");
		return;
	}
	g_fixVehicleLod.active = RedirectCall((BYTE *)kSetupSite, (void *)hkSetupVehicleVariables);
	if (g_fixVehicleLod.active) Log("VehicleLod: vehicles beyond %.0f m are drawn with their low-detail model", g_lodM);
}

static void Report(std::string &out, double secs)
{
	if (g_low + g_detailed + g_noLow)
		Appendf(out, Tr("VehicleLod: машин %.0f/с упрощённых, %.0f/с подробных (ближе lod_m), %.0f/с подробных без упрощённой модели\n", "VehicleLod: %.0f/s LOD, %.0f/s detailed (within lod_m), %.0f/s detailed (no LOD model)\n"),
		        g_low / secs, g_detailed / secs, g_noLow / secs);
	g_low = g_detailed = g_noLow = 0;
}

static const Param kParams[] = {
	{ "lod_m", "Подробные ближе, м", nullptr, &g_lodM, 10, 150, true },
};

Fix g_fixVehicleLod = {
	.section = "VehicleLod",
	.title = "Упрощённые машины вдали",
	.gain = "до -10% кадра",
	.summary = "Машины дальше lod_m рисуются своей упрощённой моделью: 1-2 вызова видеокарты вместо ~55. Меняет картинку.",
	.details = "Подробная модель машины — это ~55 вызовов видеокарты (каждая деталь и материал отдельно), и игра рисует её так "
	           "до ~100 м. 7 машин в кадре — около 14% кадра.\n\n"
	           "У каждой машины есть встроенная упрощённая модель, которую игра включает только после 100 м. Теперь она включается "
	           "уже после lod_m: вдали машины выглядят проще (без салона, стёкол и мелких деталей). Машины без упрощённой модели "
	           "(некоторые замены) рисуются как раньше. Меньше lod_m — больше FPS и раньше видно упрощение.",
	.load = Load,
	.gta = true,
	.atStart = Apply,
	.report = Report,
	.params = kParams,
	.paramCount = Count(kParams),
};
