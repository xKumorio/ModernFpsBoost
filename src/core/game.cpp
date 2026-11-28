// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
#include "game.h"

static bool CheckGta10US()
{
	// Check standard GTA SA 1.0 US CRT signature (0x82457C / 0x8245BC == 0x94BF, untouched by
	// fastman92 Limit Adjuster / Arizona RP runtime patches) with fallback to CCollision::Init.
	static const BYTE sig[] = { 0x68, 0x58, 0x02, 0x00, 0x00 };
	__try {
		if (*(const DWORD *)0x0082457C == 0x94BFu || *(const DWORD *)0x008245BC == 0x94BFu)
			return true;
		return !memcmp((void *)0x416260, sig, sizeof(sig));
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

// a global, so it is read while the module initializes: before DllMain runs any fix
static const bool g_gta10US = CheckGta10US();

bool IsGta10US()
{
	return g_gta10US;
}
