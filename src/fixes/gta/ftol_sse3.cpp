// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [FtolSSE3] Every (int)float in the game's code (MSVC 2003) is a call to the CRT's __ftol2
// (0x821B40): store, fistp with the current rounding, fild back, compare and fix up by hand for
// truncation - a few dozen cycles and a store-forwarding stall, called from all over the engine
// (~0.5% of the main thread). SSE3 fisttp truncates directly: same result in edx:eax (out of range ->
// 0x8000000000000000 like fistp), st(0) popped the same way, ecx untouched (the original even clobbers it).

#include <intrin.h>

#include "../registry.h"

static const DWORD kFtol2 = 0x821B40;

static __declspec(naked) void FtolFisttp()
{
	__asm {
		sub esp, 8
		fisttp qword ptr [esp]
		mov eax, [esp]
		mov edx, [esp + 4]
		add esp, 8
		ret
	}
}

static void Apply()
{
	int r[4];
	__cpuid(r, 1);
	if (!(r[2] & 1)) {
		Log("FtolSSE3: CPU has no SSE3 - skipped");
		return;
	}
	static const BYTE expect[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x20, 0x83, 0xE4, 0xF0, 0xD9, 0xC0, 0xD9, 0x54, 0x24, 0x18, 0xDF, 0x7C, 0x24, 0x10 };
	BYTE jmp[5] = { 0xE9 };
	*(int32_t *)(jmp + 1) = Rel32((BYTE *)kFtol2, FtolFisttp);
	bool ok = memcmp((void *)kFtol2, expect, sizeof(expect)) == 0 && PatchBytes(kFtol2, nullptr, jmp, 5);
	g_fixFtolSSE3.active = ok;
	Log(ok ? "FtolSSE3: (int)float conversions use fisttp" : "FtolSSE3: __ftol2 at 0x821B40 is not original - skipped");
}

Fix g_fixFtolSSE3 = {
	.section = "FtolSSE3",
	.title = "(int)float за одну инструкцию",
	.gain = "-0.5% кадра",
	.summary = "Перевод дробных чисел в целые через fisttp вместо медленной функции компилятора 2004 года.",
	.details = "Каждое (int)float в коде игры это вызов функции, которая сохраняет число, округляет, загружает обратно и правит "
	           "округление вручную: десятки тактов, и так по всему движку.\n\n"
	           "Инструкция fisttp (SSE3, все процессоры с 2005 года) делает то же самое за одну операцию. Результат тот же.",
	.gta = true,
	.atStart = Apply,
};
