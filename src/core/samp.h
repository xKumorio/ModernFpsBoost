// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// samp.dll version and its addresses: every SA-MP address the plugin uses comes from here.
// Supported: 0.3.7 R1, R2, R3-1, R4, R5-1 and 0.3.DL-R1 (samp_addrs_gen.h, tools/gen_samp_addrs.py).
#pragma once

#include <windows.h>

#include "samp_addrs_gen.h"

extern int g_sampVersion; // SampVersion once samp.dll is identified, else -1

// Identifies samp.dll by its entry point; false for a version that is not in the table.
bool SampIdentify(HMODULE samp, DWORD *entry);
const char *SampVersionName(); // "0.3.7-R1", or nullptr while unknown

inline bool SampHas(SampId id) { return g_sampVersion >= 0 && kSampTable[id][g_sampVersion] != kSampMissing; }
inline int SampVal(SampId id) { return kSampTable[id][g_sampVersion]; }
// address in samp.dll (rva + base); nullptr when this version does not have it
BYTE *SampAt(SampId id);
