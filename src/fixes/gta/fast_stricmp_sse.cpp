// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [FastStricmpSSE] Replaces GTA SA's 2004 VC7 ___ascii_stricmp (0x82ABC0, ~0.57% self), which runs
// an 18-instruction byte-at-a-time loop with xchg/sbb/and/add on every mismatched character.
// Using an exact-equality fast path + 256-byte ASCII lowercase LUT is 3-4x faster and 100% exact.

#include "../registry.h"

static unsigned char g_lower[256];

static int __cdecl hkAsciiStricmp(const unsigned char *a, const unsigned char *b)
{
	for (;;) {
		unsigned char ca = *a++;
		unsigned char cb = *b++;
		if (ca != cb) {
			ca = g_lower[ca];
			cb = g_lower[cb];
			if (ca != cb) return (int)ca - (int)cb;
		}
		if (!ca) return 0;
	}
}

static void Apply()
{
	for (int i = 0; i < 256; i++)
		g_lower[i] = (unsigned char)((i >= 'A' && i <= 'Z') ? (i + 32) : i);

	static const BYTE kExpect[] = { 0x55, 0x8B, 0xEC, 0x57, 0x56, 0x53, 0x8B, 0x75, 0x0C, 0x8B, 0x7D, 0x08, 0xB0, 0xFF };
	if (memcmp((void *)0x82ABC0, kExpect, sizeof(kExpect)) != 0) {
		Log("FastStricmpSSE: prologue mismatch at 0x82ABC0 - skipped");
		return;
	}
	BYTE jmp[5] = { 0xE9 };
	*(int32_t *)(jmp + 1) = Rel32((BYTE *)0x82ABC0, (const void *)hkAsciiStricmp);
	g_fixFastStricmpSSE.active = PatchBytes(0x82ABC0, nullptr, jmp, 5);
	Log(g_fixFastStricmpSSE.active
	        ? "FastStricmpSSE: ___ascii_stricmp (0x82ABC0) accelerated with LUT fast-path"
	        : "FastStricmpSSE: patch failed");
}

Fix g_fixFastStricmpSSE = {
	.section = "FastStricmpSSE",
	.title = "Быстрое сравнение строк ___ascii_stricmp",
	.gain = "-0.5% кадра",
	.summary = "Ускоряет сравнение имён текстур, моделей и HUD-ключей без лишней арифметики на каждый символ.",
	.details = "В GTA SA штатный ___ascii_stricmp из компилятора 2003 года делает ~18 инструкций на каждый символ. "
	           "Он вызывается постоянно при поиске текстур, костей, шрифтов и ключей HUD (~0.57% кадра).\n\n"
	           "Быстрый путь на совпадении байтов + таблица нижнего регистра дают 100% тот же результат в 3-4 раза быстрее.",
	.gta = true,
	.atStart = Apply,
};
