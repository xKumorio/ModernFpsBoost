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
		DWORD entry = 0;
		if (!SampIdentify(samp, &entry)) {
			Log("samp.dll of an unknown version (entry %08lX) - SA-MP fixes off, GTA fixes and the menu work", entry);
			return 0;
		}
		g_samp = (BYTE *)samp;
		static bool loggedSamp;
		if (!loggedSamp) {
			loggedSamp = true;
			Log("SA-MP %s", SampVersionName());
		}
		if (FixesAttachSamp((BYTE *)samp)) return 0;
	}
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH) {
		DisableThreadLibraryCalls(inst);
		g_module = inst;
		GetModuleFileNameA(inst, g_dir, MAX_PATH);
		if (char *slash = strrchr(g_dir, '\\')) *slash = 0;
		g_mainThread = GetCurrentThreadId();
		Log(PLUGIN_NAME " v" PLUGIN_VERSION " - (c) 2026 " PLUGIN_AUTHOR ", " PLUGIN_AUTHOR_URL ". All rights reserved.");
		g_ini.Open();
		FixesLoad(g_ini);
		FixesStart();
		MenuStart(g_ini);
		CloseHandle(CreateThread(nullptr, 0, AttachThread, nullptr, 0, nullptr));
	}
	return TRUE;
}
