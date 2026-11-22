// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// ModernFpsBoostBySoftikInquisitor(vk-miurawr).asi (by Softik Inquisitor) - FPS fixes for GTA SA 1.0 US + SA-MP 0.3.7 R1-R5 / 0.3.DL.
//
// Every fix is its own ini section (see src/fixes/registry.cpp for the list). gta_sa.exe fixes are
// applied here in DllMain, before the game initializes; samp.dll fixes from a thread once SA-MP is
// loaded.

#include "core/ini.h"
#include "core/plugin.h"
#include "core/samp.h"
#include "fixes/registry.h"
#include "ui/menu.h"

static DWORD WINAPI AttachThread(LPVOID)
{
	for (;;) {
		Sleep(250);
		HMODULE samp = GetModuleHandleA("samp.dll");
		if (!samp) continue;
		DWORD entry;
		if (!SampIdentify(samp, &entry)) {
			Log("samp.dll of an unknown version (entry %08lX) - SA-MP fixes off, GTA fixes and the menu work", entry);
			return 0;
		}
		g_samp = (BYTE *)samp;
		Log("SA-MP %s", SampVersionName());
		if (FixesAttachSamp((BYTE *)samp)) return 0;
	}
}

// The plugin's file keeps the author's name: renamed, it is renamed back when the game loads it (a
// loaded DLL's file can be renamed, not deleted). Another file already there under that name: left as is.
static void KeepFileName(HINSTANCE inst)
{
	wchar_t cur[MAX_PATH], want[MAX_PATH];
	DWORD n = GetModuleFileNameW(inst, cur, MAX_PATH);
	if (!n || n >= MAX_PATH) return;
	wchar_t *name = wcsrchr(cur, L'\\');
	name = name ? name + 1 : cur;
	if (!_wcsicmp(name, PLUGIN_FILE_NAME)) return;
	size_t dir = name - cur;
	if (dir + wcslen(PLUGIN_FILE_NAME) >= MAX_PATH) return;
	wcsncpy_s(want, cur, dir);
	wcscat_s(want, PLUGIN_FILE_NAME);
	if (GetFileAttributesW(want) != INVALID_FILE_ATTRIBUTES) {
		Log("The plugin file was renamed and %ls already exists - not renamed back", PLUGIN_FILE_NAME);
		return;
	}
	if (MoveFileExW(cur, want, 0)) Log("The plugin file was renamed - renamed back to %ls", PLUGIN_FILE_NAME);
	else Log("The plugin file was renamed - renaming it back failed (error %lu)", GetLastError());
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH) {
		DisableThreadLibraryCalls(inst);
		g_module = inst;
		GetModuleFileNameA(inst, g_dir, MAX_PATH);
		if (char *slash = strrchr(g_dir, '\\')) *slash = 0;
		g_mainThread = GetCurrentThreadId();
		KeepFileName(inst);
		Log(PLUGIN_NAME " v" PLUGIN_VERSION " - (c) 2026 " PLUGIN_AUTHOR ", " PLUGIN_AUTHOR_URL ". All rights reserved.");
		g_ini.Open();
		FixesLoad(g_ini);
		FixesStart();
		MenuStart(g_ini);
		CloseHandle(CreateThread(nullptr, 0, AttachThread, nullptr, 0, nullptr));
	}
	return TRUE;
}
