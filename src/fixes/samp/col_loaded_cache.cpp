// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [ColLoadedCache] For every nearby remote ped, every frame, SA-MP (R1 samp+0xB0970, from its ped hooks)
// asks CColStore::HasCollisionLoaded (0x410CE0), which marks the col sectors around the point, then
// walks the whole col pool clearing the marks and testing the boxes. The marks are set and cleared
// within the call, so it is a pure query whose answer changes only when collision streams in or out.
// The answer is reused for 250 ms per 2 m cell and area (the call goes through mov edx, 0x410CE0 at
// samp+0xB09DB, whose immediate is pointed here).

#include <math.h>

#include "../registry.h"

static const DWORD kHasCollisionLoaded = 0x410CE0;

struct ColLoadedSlot { int x, y, z, area; DWORD time; BYTE result; bool used; };
static ColLoadedSlot g_colLoaded[512];

static BYTE __cdecl hkHasCollisionLoaded(const float *pos, int area)
{
	auto orig = (BYTE(__cdecl *)(const float *, int))kHasCollisionLoaded;
	if (GetCurrentThreadId() != g_mainThread) return orig(pos, area);
	int x = (int)floorf(pos[0] * 0.5f), y = (int)floorf(pos[1] * 0.5f), z = (int)floorf(pos[2] * 0.5f);
	ColLoadedSlot &s = g_colLoaded[(unsigned)(x * 73856093 ^ y * 19349663 ^ z * 83492791 ^ area) & 511];
	DWORD now = TimeMs();
	if (s.used && s.x == x && s.y == y && s.z == z && s.area == area && now - s.time < Jitter((DWORD)&s, 250)) return s.result;
	BYTE r = orig(pos, area);
	s = { x, y, z, area, now, r, true };
	return r;
}

static bool Apply(BYTE *)
{
	BYTE *p = SampAt(S_COL_LOADED_SITE);
	if (!p || p[0] != 0xBA || *(DWORD *)(p + 1) != kHasCollisionLoaded || p[5] != 0xFF || p[6] != 0xD2) {
		Log("ColLoadedCache: the HasCollisionLoaded call is not original - skipped");
		return true;
	}
	DWORD target = (DWORD)hkHasCollisionLoaded;
	BYTE *addrs[] = { p + 1 };
	const void *data[] = { &target };
	size_t sizes[] = { 4 };
	if ((g_fixColLoadedCache.active = PatchMainThread(addrs, data, sizes, 1))) Log("ColLoadedCache: SA-MP's collision-loaded check per ped cached for 250 ms");
	return true;
}

Fix g_fixColLoadedCache = {
	.section = "ColLoadedCache",
	.title = "Коллизия под игроками",
	.gain = "-0.3% кадра",
	.summary = "Ответ «загружена ли коллизия под игроком» держится 250 мс для клетки 2 м.",
	.details = "SA-MP каждый кадр для каждого ближнего игрока спрашивает, загружена ли под ним карта столкновений, а GTA на каждый вопрос перебирает все области коллизий карты.\n\nОтвет меняется только при подгрузке коллизии, поэтому держится 250 мс.",
	.atSamp = Apply,
};
