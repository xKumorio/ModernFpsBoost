// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [AsiMemProtectCache] Several popular ASI plugins (resamp++.asi, FramerateVigilante.SA.asi,
// SAMPFUNCS.asi, crashes.asi) call VirtualProtect(addr, 4, PAGE_EXECUTE_READWRITE, &old) + restore
// every single frame on GTA memory pages, triggering ntdll!ZwProtectVirtualMemory syscalls and TLB
// flushes (~0.5-0.7% of the main thread). Caching recently unprotected RWX pages on the main thread
// (same proven mechanism as LuaMemProtectCache) removes ~99% of these syscalls.

#include "../registry.h"

static BOOL(WINAPI *oVirtualProtect)(LPVOID, SIZE_T, DWORD, PDWORD);

struct AsiPageSlot {
	DWORD page;
	DWORD time;
};
static AsiPageSlot g_pages[256];
static unsigned g_calls, g_skipped;

static bool IsCachedRwx(DWORD first, DWORD last, DWORD now)
{
	if (last - first >= 4) return false;
	for (DWORD p = first; p <= last; p++) {
		const AsiPageSlot &s = g_pages[p & 255];
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
	g_calls++;

	const DWORD first = (DWORD)addr >> 12;
	const DWORD last  = ((DWORD)addr + size - 1) >> 12;
	const DWORD now   = GetTickCount();

	if (newProt == PAGE_EXECUTE_READWRITE) {
		if (IsCachedRwx(first, last, now)) {
			*oldProt = PAGE_EXECUTE_READWRITE;
			g_skipped++;
			return TRUE;
		}
		DWORD realOld = 0;
		BOOL ok = oVirtualProtect(addr, size, PAGE_EXECUTE_READWRITE, &realOld);
		if (ok) {
			MarkCachedRwx(first, last, now);
			*oldProt = PAGE_EXECUTE_READWRITE;
		} else {
			*oldProt = realOld;
		}
		return ok;
	}

	if (last - first < 4) {
		for (DWORD p = first; p <= last; p++)
			if (g_pages[p & 255].page == p) g_pages[p & 255].page = 0;
	}
	return oVirtualProtect(addr, size, newProt, oldProt);
}

static bool Apply(BYTE *)
{
	oVirtualProtect = VirtualProtect;
	const char *mods[] = {
		"resamp++.asi",
		"FramerateVigilante.SA.asi",
		"SAMPFUNCS.asi",
		"crashes.asi",
	};
	int hooked = 0;
	for (const char *name : mods) {
		HMODULE m = GetModuleHandleA(name);
		if (!m) continue;
		void *prev = nullptr;
		if (PatchIat(m, "kernel32.dll", "VirtualProtect", (void *)hkVirtualProtect, &prev))
			hooked++;
	}
	g_fixAsiMemProtectCache.active = hooked > 0;
	Log("AsiMemProtectCache: VirtualProtect cached in %d ASI plugin(s)", hooked);
	return true;
}

static void Report(std::string &out, double secs)
{
	if (g_calls)
		Appendf(out, Tr("AsiMemProtectCache: пропущено %.0f%% из %.0f VirtualProtect/с\n", "AsiMemProtectCache: skipped %.0f%% of %.0f VirtualProtect/s\n"), 100.0 * g_skipped / g_calls, g_calls / secs);
	g_calls = g_skipped = 0;
}

Fix g_fixAsiMemProtectCache = {
	.section = "AsiMemProtectCache",
	.title = "Кеш VirtualProtect для ASI-плагинов",
	.gain = "-0.5–0.7% кадра",
	.summary = "Кеширует снятие защиты памяти в resamp++, FramerateVigilante, SAMPFUNCS и crashes.asi.",
	.details = "Часть популярных ASI-плагинов (включая resamp++.asi и FramerateVigilante.SA.asi) каждый кадр снимает и "
	           "возвращает защиту памяти через VirtualProtect ради записи 4 байт. Каждый такой вызов — переход в ядро "
	           "Windows (ntdll!ZwProtectVirtualMemory) и сброс TLB.\n\n"
	           "По аналогии с LuaMemProtectCache, страницы, открытые на запись в главном потоке, кешируются на 2 секунды "
	           "без повторных системных вызовов. Без потерь.",
	.atSamp = Apply,
	.report = Report,
};
