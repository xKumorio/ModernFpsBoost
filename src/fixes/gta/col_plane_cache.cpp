// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [ColPlaneCache] CCollision::ms_colModelCache keeps precomputed triangle planes for only 50 col
// models (CCollision::Init 0x416260: new CLink[50], 12 bytes each). In a crowd on a big map the LRU
// thrashes and CCollisionData::CalculateTrianglePlanes runs again and again. The planes are a pure
// function of the triangles, so a bigger cache changes nothing but the time spent.

#include "../registry.h"

static int g_size = 1000;

static void Load(const Ini &ini)
{
	g_size = ini.Int("ColPlaneCache", "size", 1000);
}

static void Apply()
{
	if (g_size <= 50) return;
	if (*(DWORD *)0x96595C) { // links already allocated: CCollision::Init has run
		Log("ColPlaneCache: CCollision::Init already ran, asi loaded too late - skipped");
		return;
	}
	DWORD bytes = (DWORD)g_size * 12;
	BYTE push[5] = { 0x68 }, mov[5] = { 0xB9 };
	memcpy(push + 1, &bytes, 4);
	memcpy(mov + 1, &bytes, 4);
	static const BYTE oldPush[] = { 0x68, 0x58, 0x02, 0x00, 0x00 }, oldMov[] = { 0xB9, 0x58, 0x02, 0x00, 0x00 };
	if (memcmp((void *)0x416260, oldPush, 5) || memcmp((void *)0x41629B, oldMov, 5)) {
		Log("ColPlaneCache: CCollision::Init bytes differ (patched by another mod?) - skipped");
		return;
	}
	PatchBytes(0x416260, nullptr, push, 5);
	PatchBytes(0x41629B, nullptr, mov, 5);
	g_fixColPlaneCache.active = true;
	Log("ColPlaneCache: triangle plane cache 50 -> %d col models", g_size);
}

static const Param kParams[] = {
	{ "size", "Моделей в кеше", &g_size, nullptr, 50, 5000, false },
};

Fix g_fixColPlaneCache = {
	.section = "ColPlaneCache",
	.title = "Кеш плоскостей коллизии",
	.gain = "-1.5% кадра",
	.summary = "Плоскости треугольников коллизии считаются один раз, а не заново каждый кадр.",
	.details = "GTA держит готовые плоскости треугольников коллизии только для 50 моделей. "
	           "В толпе на большой карте кеш переполняется, и плоскости пересчитываются по кругу.\n\n"
	           "Кеш увеличен до size моделей. Плоскости зависят только от модели, результат тот же. Без потерь, пара МБ памяти.",
	.load = Load,
	.gta = true,
	.atStart = Apply,
	.params = kParams,
	.paramCount = Count(kParams),
};
