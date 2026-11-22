// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
#include "game.h"

static bool CheckGta10US()
{
	// CCollision::Init starts with push 0x258 in 1.0 US; outside gta_sa.exe (tools/preview) the page is not there
	static const BYTE sig[] = { 0x68, 0x58, 0x02, 0x00, 0x00 };
	__try {
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
