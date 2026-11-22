// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
#pragma once

#include <windows.h>

// [MaterialFontCache], applied by TextCache once its d3dx font hooks are in (they purge the cache).
void MaterialFontCacheApply(BYTE *samp);
void MaterialFontCachePurge(); // device lost / reset
