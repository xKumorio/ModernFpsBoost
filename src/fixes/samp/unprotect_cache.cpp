// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [UnprotectCache] R1 samp+0xA4DD0 is SA-MP's UnFuck(addr, size): VirtualProtect(addr, size,
// PAGE_EXECUTE_READWRITE) without restoring the old protection. SA-MP calls it before its per-player
// code patches every frame (~140 times a frame in a crowd), a syscall through WoW64 each time, while
// the page is already writable since the first call. A page made RWX here less than a second ago on
// the main thread is not made RWX again; other threads and older pages go to VirtualProtect as before.

#include "../registry.h"

// the call sites: every direct call of UnFuck in each samp.dll (kSampUnfuckSites, capstone linear sweep)

struct UnprotSlot { DWORD page, time; };
static UnprotSlot g_unprot[256];
static unsigned g_unprotCalls, g_unprotSkipped;

static BOOL __cdecl hkUnFuck(void *addr, SIZE_T size)
{
	DWORD old;
	if (GetCurrentThreadId() != g_mainThread || !size)
		return VirtualProtect(addr, size, PAGE_EXECUTE_READWRITE, &old);
	g_unprotCalls++;
	DWORD first = (DWORD)addr >> 12, last = ((DWORD)addr + size - 1) >> 12, now = GetTickCount();
	bool fresh = last - first < 4;
	for (DWORD p = first; p <= last && fresh; p++) {
		UnprotSlot &s = g_unprot[p & 255];
		fresh = s.page == p && now - s.time < 1000;
	}
	if (fresh) {
		g_unprotSkipped++;
		return TRUE;
	}
	BOOL ok = VirtualProtect(addr, size, PAGE_EXECUTE_READWRITE, &old);
	if (ok)
		for (DWORD p = first; p <= last; p++) g_unprot[p & 255] = { p, now };
	return ok;
}

static bool Apply(BYTE *samp)
{
	const int n = kSampUnfuckSitesMax;
	BYTE *addrs[n];
	const void *data[n];
	size_t sizes[n];
	int32_t rel[n];
	int np = 0;
	BYTE *target = SampAt(S_UNFUCK);
	static const BYTE expect[] = { 0x51, 0x8B, 0x4C, 0x24, 0x0C, 0x8B, 0x54, 0x24, 0x08, 0x8D, 0x04, 0x24, 0x50, 0x6A, 0x40 };
	if (!target || memcmp(target, expect, sizeof(expect))) {
		Log("UnprotectCache: UnFuck is not original - skipped");
		return true;
	}
	for (const DWORD *at = kSampUnfuckSites[g_sampVersion]; *at; at++) {
		BYTE *p = samp + *at;
		if (!IsCallTo(p, (DWORD)target)) continue;
		rel[np] = Rel32(p, hkUnFuck);
		addrs[np] = p + 1;
		data[np] = &rel[np];
		sizes[np] = 4;
		np++;
	}
	if ((g_fixUnprotectCache.active = np && PatchMainThread(addrs, data, sizes, np))) Log("UnprotectCache: %d UnFuck calls cached", np);
	else Log("UnprotectCache: not installed");
	return true;
}

static void Report(std::string &out, double secs)
{
	if (g_unprotCalls)
		Appendf(out, "UnprotectCache: пропущено %.0f%% из %.0f VirtualProtect/с\n", 100.0 * g_unprotSkipped / g_unprotCalls, g_unprotCalls / secs);
	g_unprotCalls = g_unprotSkipped = 0;
}

Fix g_fixUnprotectCache = {
	.section = "UnprotectCache",
	.title = "Защита памяти SA-MP",
	.gain = "-1.5% кадра",
	.summary = "SA-MP больше не просит Windows открыть на запись память, которая и так открыта.",
	.details = "SA-MP для каждого игрока каждый кадр правит кусочки кода GTA и перед каждой записью вызывает VirtualProtect (~140 раз за кадр). Это системный вызов через WoW64, хотя память стала записываемой ещё при первом вызове.\n\nСтраница, открытая меньше секунды назад, повторно не открывается. Без потерь.",
	.atSamp = Apply,
	.report = Report,
};
