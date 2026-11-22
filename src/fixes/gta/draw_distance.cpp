// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [DrawDistance] The draw distance below the minimum of the game's own setting. CCamera::Process
// multiplies TheCamera.m_fLODDistMultiplier by CRenderer::ms_lodDistScale (0x8CD800, the menu's draw
// distance: 0.925 - 1.8) every frame, and every model is drawn up to its own distance times that.
// The two instructions that read the scale (0x52C9F7 in CCamera::Process, 0x5099B0 the getter) are
// pointed at a value of our own, percent / 100: fewer buildings and objects are drawn in detail (their
// LOD models take over sooner) and fewer pass the visibility check. The menu's slider then does nothing.

#include "../registry.h"

static const DWORD kLodDistScale = 0x8CD800;
static const DWORD kReaders[] = { 0x52C9F9, 0x5099B2 }; // the address operand of fld dword ptr [0x8CD800]

static float g_percent = 60.0f;
static float g_scale = 0.6f;

static void Load(const Ini &ini)
{
	g_percent = ini.Float("DrawDistance", "percent", 60.0f);
}

// the game reads g_scale every frame: the menu's slider acts at once
static void Sync()
{
	if (g_percent < 20) g_percent = 20;
	if (g_percent > 180) g_percent = 180;
	g_scale = g_percent / 100.0f;
}

static void Apply()
{
	if (g_percent <= 0) return;
	Sync();
	const float *mine = &g_scale;
	int ok = 0;
	for (DWORD at : kReaders) ok += PatchBytes(at, &kLodDistScale, &mine, 4);
	g_fixDrawDistance.active = ok == 2;
	Log("DrawDistance: %d/%d readers of the draw distance use %.0f%%", ok, 2, g_percent);
}

static const Param kParams[] = {
	{ "percent", "Дальность, %", nullptr, &g_percent, 20, 180, true },
};

Fix g_fixDrawDistance = {
	.section = "DrawDistance",
	.title = "Дальность прорисовки",
	.gain = "до -10% кадра",
	.summary = "Дальность прорисовки мира ниже минимума из настроек игры. Меняет картинку.",
	.details = "Каждое здание и объект рисуется до своей дальности, умноженной на настройку «дальность прорисовки» (в игре от 92% "
	           "до 180%). Здесь она задаётся напрямую, в том числе ниже игрового минимума.\n\n"
	           "Меньше процент — подробные здания и объекты маппинга появляются ближе (вдали вместо них упрощённые модели), "
	           "рисуется и проверяется на видимость меньше. Меняется сразу, без перезапуска. Ползунок дальности в настройках игры при включённом фиксе не действует.",
	.load = Load,
	.gta = true,
	.atStart = Apply,
	.changed = Sync,
	.params = kParams,
	.paramCount = Count(kParams),
};
