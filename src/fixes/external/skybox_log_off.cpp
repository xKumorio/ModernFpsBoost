// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [SkyboxLogOff] RealSkybox.SA.asi (this build) writes debug lines to RealSkybox.SA.log on the main
// thread all the time - 170 000 lines - and a write now and then blocks for 20-25 ms (a visible
// freeze). Its import of WriteFile is pointed at a function that reports the write done without
// writing; the mod writes no other file.

#include "../registry.h"

static BOOL WINAPI hkSkyboxWriteFile(HANDLE, LPCVOID, DWORD n, LPDWORD written, LPOVERLAPPED ov)
{
	if (written) *written = n;
	if (ov) ov->Internal = 0, ov->InternalHigh = n;
	return TRUE;
}

static bool Apply(BYTE *)
{
	HMODULE m = GetModuleHandleA("RealSkybox.SA.asi");
	if (!m) {
		Log("SkyboxLogOff: RealSkybox.SA.asi not loaded - skipped");
		return true;
	}
	BYTE *lo = (BYTE *)m, *hi = ModuleEnd(m);
	DWORD targets[2] = { (DWORD)GetProcAddress(GetModuleHandleA("kernel32.dll"), "WriteFile"),
	                     (DWORD)GetProcAddress(GetModuleHandleA("kernelbase.dll"), "WriteFile") };
	int n = 0;
	// the build is UPX-packed: no usable import directory, so every readable dword is checked
	for (BYTE *p = lo; p < hi;) {
		MEMORY_BASIC_INFORMATION mi;
		if (!VirtualQuery(p, &mi, sizeof(mi))) break;
		BYTE *end = (BYTE *)mi.BaseAddress + mi.RegionSize;
		if (end > hi) end = hi;
		bool readable = mi.State == MEM_COMMIT && !(mi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
		                (mi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
		if (readable)
			for (DWORD *q = (DWORD *)(((DWORD)p + 3) & ~3u); (BYTE *)(q + 1) <= end; q++)
				if (*q && (*q == targets[0] || *q == targets[1])) {
					DWORD old;
					if (VirtualProtect(q, 4, PAGE_READWRITE, &old)) {
						*q = (DWORD)hkSkyboxWriteFile;
						VirtualProtect(q, 4, old, &old);
						n++;
					}
				}
		p = end;
	}
	g_fixSkyboxLogOff.active = n > 0;
	if (n) Log("SkyboxLogOff: RealSkybox log writes dropped (%d import slot%s)", n, n == 1 ? "" : "s");
	else Log("SkyboxLogOff: no WriteFile import found in RealSkybox.SA.asi - skipped");
	return true;
}

Fix g_fixSkyboxLogOff = {
	.section = "SkyboxLogOff",
	.title = "Лог RealSkybox",
	.gain = "без фризов",
	.summary = "RealSkybox больше не пишет свой лог на диск в потоке игры: пропадают фризы по 20–25 мс.",
	.details = "Эта сборка RealSkybox постоянно пишет отладочные цифры в RealSkybox.SA.log (170 000 строк) прямо в потоке игры, и запись иногда блокируется на десятки миллисекунд.\n\nЗапись в файл у RealSkybox отключена. На небо не влияет.",
	.atSamp = Apply,
};
