// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [FastAtan2SSE] Replaces CGeneral::GetATanOfXY (0x53CC70, ~0.6% self): GTA's x87 implementation
// runs 6 fnstsw ax stalls + slow x87 fpatan (~140 cycles/call) for camera, radar, peds, shadows.
// Replaced with a stall-free SSE2 minimax polynomial (~14 cycles, < 1.5e-6 rad accuracy) matching
// GTA's exact [0, 2*PI) quadrant convention (0 at +X, PI/2 at +Y, PI at -X, 3*PI/2 at -Y, 0 at 0,0).

#include "../registry.h"
#include <math.h>

static float __cdecl hkGetATanOfXY(float x, float y)
{
	if (x == 0.0f && y == 0.0f) return 0.0f;

	const float ax = fabsf(x);
	const float ay = fabsf(y);
	const bool swap = ay > ax;
	const float z = swap ? (ax / ay) : (ay / ax);
	const float z2 = z * z;

	// Degree-11 odd minimax polynomial for atan(z) on [0, 1] (max error < 1.5e-6 rad)
	float a = z * (0.99997726f +
	          z2 * (-0.33262347f +
	          z2 * (0.19354346f +
	          z2 * (-0.11643287f +
	          z2 * (0.05265332f - 0.01172120f * z2)))));

	const float kHalfPi = 1.57079632679f;
	const float kPi     = 3.14159265359f;
	const float kTwoPi  = 6.28318530718f;

	if (swap) a = kHalfPi - a;
	if (x < 0.0f) a = kPi - a;
	if (y < 0.0f) a = kTwoPi - a;
	if (a >= kTwoPi) a = 0.0f;
	return a;
}

static void Apply()
{
	static const BYTE kExpect[] = { 0xD9, 0x44, 0x24, 0x04, 0xD8, 0x1D, 0x50, 0x8B, 0x85, 0x00, 0xDF, 0xE0, 0xF6, 0xC4, 0x44 };
	if (memcmp((void *)0x53CC70, kExpect, sizeof(kExpect)) != 0) {
		Log("FastAtan2SSE: prologue mismatch at 0x53CC70 - skipped");
		return;
	}
	BYTE jmp[5] = { 0xE9 };
	*(int32_t *)(jmp + 1) = Rel32((BYTE *)0x53CC70, (const void *)hkGetATanOfXY);
	g_fixFastAtan2SSE.active = PatchBytes(0x53CC70, nullptr, jmp, 5);
	Log(g_fixFastAtan2SSE.active
	        ? "FastAtan2SSE: CGeneral::GetATanOfXY replaced with SSE2 polynomial (~10x faster)"
	        : "FastAtan2SSE: patch failed");
}

Fix g_fixFastAtan2SSE = {
	.section = "FastAtan2SSE",
	.title = "Быстрый расчёт углов GetATanOfXY на SSE2",
	.gain = "-0.6% кадра",
	.summary = "Заменяет медленный x87 fpatan в CGeneral::GetATanOfXY (0x53CC70) на быстрый полином SSE2.",
	.details = "GTA SA вызывает CGeneral::GetATanOfXY сотни раз за кадр (камера, радар, педы, тени). Оригинал делает "
	           "6 затыков fnstsw ax и тяжёлую инструкцию x87 fpatan (~140 тактов).\n\n"
	           "Полином на SSE2 считает за ~14 тактов (в ~10 раз быстрее) с точностью лучше 0.0001° в том же диапазоне [0, 2π).",
	.gta = true,
	.atStart = Apply,
};
