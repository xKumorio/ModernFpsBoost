// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// Code patching helpers.
#pragma once

#include <windows.h>
#include <stdint.h>

// Writes n bytes at addr (if expect is given, only when the old bytes match it).
bool PatchBytes(DWORD addr, const void *expect, const void *data, size_t n);

// Writes code the main thread may be executing: suspends it outside every [addrs[k], addrs[k] + sizes[k]).
bool PatchMainThread(BYTE *const *addrs, const void *const *data, const size_t *sizes, int count);

// Points the E8/E9 at site to dest (rel32 at site + 1), written with the main thread outside it.
bool RedirectCall(BYTE *site, const void *dest);

// A hook that took over a call site and calls on through *next: other code that wants the same call
// (the profiler) patches *next instead of the site.
void AddCallChain(BYTE *site, DWORD *next);
DWORD *CallChainNext(BYTE *site);

// Points an import of mod (from dll) at hook; the old pointer goes to *orig.
bool PatchIat(HMODULE mod, const char *dll, const char *fn, void *hook, void **orig);

bool InModule(HMODULE m, const void *p);
BYTE *ModuleEnd(HMODULE m);

// site is E8 rel32 calling target
static inline bool IsCallTo(const BYTE *site, DWORD target)
{
	return site[0] == 0xE8 && (DWORD)(site + 5) + *(const int32_t *)(site + 1) == target;
}

// rel32 for an E8/E9 at site jumping to dest
static inline int32_t Rel32(const BYTE *site, const void *dest)
{
	return (int32_t)((DWORD)dest - (DWORD)(site + 5));
}
