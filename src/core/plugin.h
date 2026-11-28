// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// Shared plugin state: name, folder, main thread, logging.
#pragma once

#include <windows.h>
#include <stdio.h>
#include <string>
#include <vector>

#define PLUGIN_NAME "ModernFpsBoost"
#define PLUGIN_OLD_NAME "OptimizeFpsByInquisitorGang" // before 1.0: its ini is taken over
#define PLUGIN_AUTHOR "Softik Inquisitor"
#define PLUGIN_AUTHOR_URL "https://vk.ru/miurawr"
#define PLUGIN_VERSION "1.0.12"

extern HMODULE g_module;   // this .asi
extern char    g_dir[MAX_PATH]; // folder of the .asi (the game folder)
extern DWORD   g_mainThread; // set in DllMain: ASIs are loaded on the game's main thread
extern BYTE   *g_samp;     // samp.dll once it is loaded and its version is known (core/samp.h)
extern bool    g_english;  // [Language] english=1 in ModernFpsBoost.ini

inline const char *Tr(const char *ru, const char *en) { return g_english ? en : ru; }

void Log(const char *fmt, ...);
void LogTail(std::vector<std::string> &out, size_t &seen); // lines logged since `seen`

void Appendf(std::string &out, const char *fmt, ...);

// Cache lifetimes are spread over [base/2, base] by key, so entries cached in the same frame do not all
// expire in the same frame again (a periodic spike with a steady average fps).
static inline DWORD Jitter(DWORD key, DWORD base)
{
	return base - ((key * 2654435761u) >> 12) % (base / 2 + 1);
}
