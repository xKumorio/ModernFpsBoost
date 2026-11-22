// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [ObjectPrefetch] nametags.
// (Prefetching all object pool slots before SA-MP's object loops was dropped: ~1000 objects x 4 lines
// overflow L1/L2 before the loop reaches them, the prefetch cost 0.9% for loops costing 0.3%.)
// The nametag/health bar loop (R1 samp+0x70D40, called at samp+0x71195) walks every connected player
// the same way: CPlayerInfo, then CRemotePlayer, a cache miss each. Prefetched in two passes first.

#include <xmmintrin.h>

#include "../registry.h"

static DWORD g_tagLoop;

static void __cdecl hkTagLoop()
{
	if (BYTE *pool = SampPlayerPool()) {
		int largest = *(int *)(pool + g_sl.poolLargest);
		if (largest >= kMaxPlayers) largest = kMaxPlayers - 1;
		const int *used = (const int *)(pool + g_sl.poolUsed);
		BYTE *const *info = (BYTE *const *)(pool + g_sl.poolInfo);
		for (int i = 0; i <= largest; i++)
			if (used[i] == 1 && info[i]) _mm_prefetch((const char *)info[i], _MM_HINT_T0);
		for (int i = 0; i <= largest; i++)
			if (used[i] == 1 && info[i])
				if (BYTE *rp = *(BYTE **)(info[i] + g_sl.infoRemote)) {
					_mm_prefetch((const char *)rp, _MM_HINT_T0);
					_mm_prefetch((const char *)rp + 0xB3, _MM_HINT_T0);
				}
	}
	((void(__cdecl *)())g_tagLoop)();
}

static bool Apply(BYTE *)
{
	BYTE *p = SampAt(S_TAG_LOOP_SITE);
	g_tagLoop = (DWORD)SampAt(S_TAG_LOOP);
	if (!p || !g_tagLoop || !IsCallTo(p, g_tagLoop)) {
		Log("ObjectPrefetch: nametag loop call is not original - skipped");
		return true;
	}
	if ((g_fixObjectPrefetch.active = RedirectCall(p, hkTagLoop))) Log("ObjectPrefetch: nametag loop prefetched");
	return true;
}

Fix g_fixObjectPrefetch = {
	.section = "ObjectPrefetch",
	.title = "Цикл ников и HP",
	.gain = "-0.5% кадра",
	.summary = "Данные игроков для ников и полосок здоровья запрашиваются у памяти заранее и разом.",
	.details = "Цикл ников по очереди читает данные каждого игрока, и промахи кэша идут строго один за другим.\n\nПеред циклом данные всех игроков подгружаются разом (prefetch), промахи идут параллельно. Сам цикл SA-MP без изменений.",
	.atSamp = Apply,
};
