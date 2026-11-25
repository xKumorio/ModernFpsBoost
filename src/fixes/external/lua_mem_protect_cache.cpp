// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [LuaMemProtectCache] When MoonLoader Lua scripts call memory.write(..., true) / writeMemory(..., true)
// inside per-frame loops, MoonLoader.asi executes VirtualQuery + VirtualProtect(RWX) + memcpy +
// VirtualProtect(old) — 3 kernel syscalls and TLB flushes per write (~2% of the main thread in active
// assemblies). Hooking MoonLoader's IAT for VirtualProtect and VirtualQuery on the main thread keeps
// recently written pages RWX instead of toggling protection back and forth hundreds of times a second.

#include "../registry.h"

static BOOL(WINAPI *oVirtualProtect)(LPVOID, SIZE_T, DWORD, PDWORD);
static SIZE_T(WINAPI *oVirtualQuery)(LPCVOID, PMEMORY_BASIC_INFORMATION, SIZE_T);

struct PageSlot {
	DWORD page;
	DWORD time;
};
static PageSlot g_pages[256];
static unsigned g_vpCalls, g_vpSkipped, g_vqCalls, g_vqSkipped;

static bool IsCachedRwx(DWORD first, DWORD last, DWORD now)
{
	if (last - first >= 4) return false;
	for (DWORD p = first; p <= last; p++) {
		const PageSlot &s = g_pages[p & 255];
		if (s.page != p || now - s.time >= 2000) return false;
	}
	return true;
}

static void MarkCachedRwx(DWORD first, DWORD last, DWORD now)
{
	if (last - first >= 4) return;
	for (DWORD p = first; p <= last; p++)
		g_pages[p & 255] = { p, now };
}

static BOOL WINAPI hkVirtualProtect(LPVOID addr, SIZE_T size, DWORD newProt, PDWORD oldProt)
{
	if (GetCurrentThreadId() != g_mainThread || !size || !oldProt)
		return oVirtualProtect(addr, size, newProt, oldProt);
	g_vpCalls++;

	const DWORD first = (DWORD)addr >> 12;
	const DWORD last  = ((DWORD)addr + size - 1) >> 12;
	const DWORD now   = GetTickCount();

	if (newProt == PAGE_EXECUTE_READWRITE) {
		if (IsCachedRwx(first, last, now)) {
			*oldProt = PAGE_EXECUTE_READWRITE;
			g_vpSkipped++;
			return TRUE;
		}
		DWORD realOld = 0;
		BOOL ok = oVirtualProtect(addr, size, PAGE_EXECUTE_READWRITE, &realOld);
		if (ok) {
			MarkCachedRwx(first, last, now);
			// Tell MoonLoader the page was already RWX so its immediate post-write restore
			// also hits the cached RWX fast path instead of toggling protection back and forth.
			*oldProt = PAGE_EXECUTE_READWRITE;
		} else {
			*oldProt = realOld;
		}
		return ok;
	}

	// Non-RWX protection change: if MoonLoader is restoring a non-cached protection, pass through
	// and invalidate the cache for those pages.
	if (last - first < 4) {
		for (DWORD p = first; p <= last; p++)
			if (g_pages[p & 255].page == p) g_pages[p & 255].page = 0;
	}
	return oVirtualProtect(addr, size, newProt, oldProt);
}

static SIZE_T WINAPI hkVirtualQuery(LPCVOID addr, PMEMORY_BASIC_INFORMATION mbi, SIZE_T len)
{
	if (GetCurrentThreadId() != g_mainThread || !mbi || len < sizeof(MEMORY_BASIC_INFORMATION))
		return oVirtualQuery(addr, mbi, len);
	g_vqCalls++;

	const DWORD page = (DWORD)addr >> 12;
	const DWORD now = GetTickCount();
	if (IsCachedRwx(page, page, now)) {
		mbi->BaseAddress = (PVOID)(page << 12);
		mbi->AllocationBase = mbi->BaseAddress;
		mbi->AllocationProtect = PAGE_EXECUTE_READWRITE;
		mbi->RegionSize = 4096;
		mbi->State = MEM_COMMIT;
		mbi->Protect = PAGE_EXECUTE_READWRITE;
		mbi->Type = MEM_IMAGE;
		g_vqSkipped++;
		return sizeof(MEMORY_BASIC_INFORMATION);
	}
	return oVirtualQuery(addr, mbi, len);
}

static bool Apply(BYTE *)
{
	HMODULE ml = GetModuleHandleA("MoonLoader.asi");
	if (!ml) return true;
	bool okVp = PatchIat(ml, "kernel32.dll", "VirtualProtect", (void *)hkVirtualProtect, (void **)&oVirtualProtect);
	bool okVq = PatchIat(ml, "kernel32.dll", "VirtualQuery", (void *)hkVirtualQuery, (void **)&oVirtualQuery);
	g_fixLuaMemProtectCache.active = okVp || okVq;
	if (g_fixLuaMemProtectCache.active)
		Log("LuaMemProtectCache: MoonLoader VirtualProtect/VirtualQuery cached (%d/%d)", (int)okVp, (int)okVq);
	else
		Log("LuaMemProtectCache: MoonLoader IAT slots not found - skipped");
	return true;
}

static void Report(std::string &out, double secs)
{
	unsigned total = g_vpCalls + g_vqCalls;
	unsigned skipped = g_vpSkipped + g_vqSkipped;
	if (total)
		Appendf(out, Tr("LuaMemProtectCache: пропущено %.0f%% из %.0f VirtualProtect/Query/с\n", "LuaMemProtectCache: skipped %.0f%% of %.0f VirtualProtect/Query/s\n"), 100.0 * skipped / total, total / secs);
	g_vpCalls = g_vpSkipped = g_vqCalls = g_vqSkipped = 0;
}

Fix g_fixLuaMemProtectCache = {
	.section = "LuaMemProtectCache",
	.title = "Кеш защиты памяти Lua",
	.gain = "-1.5–2% кадра",
	.summary = "MoonLoader не дёргает VirtualQuery и VirtualProtect по 3 раза на каждую запись в память из Lua-скриптов.",
	.details = "Когда Lua-скрипт в цикле вызывает memory.write(..., true) или writeMemory(..., true), MoonLoader.asi "
	           "на каждую запись делает 3 тяжёлых системных вызова Windows: VirtualQuery, VirtualProtect(RWX) и "
	           "VirtualProtect(назад) со сбросом TLB (~2% всего кадра в сборках со скриптами).\n\n"
	           "Страница, которую MoonLoader открыл на запись в главном потоке, запоминается на 2 секунды и не "
	           "переключается туда-сюда сотни раз в секунду. Без потерь.",
	.atSamp = Apply,
	.report = Report,
};
