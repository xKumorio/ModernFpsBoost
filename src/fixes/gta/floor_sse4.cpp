// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [FloorSSE4] GTA SA's CRT floor() (0x8219F0) and ceil() (0x823820) execute stmxcsr + fnstcw
// (serializing FPU stalls) and ~40 SSE2 bit-fiddling instructions before pushing the result onto st(0).
// On CPUs with SSE4.1 (every CPU since ~2008), roundsd computes the exact IEEE 754 floor/ceil in a
// single instruction with no FPU control-word stalls.

#include <intrin.h>

#include "../registry.h"

static const DWORD kFloor = 0x8219F0;
static const DWORD kCeil  = 0x823820;

static __declspec(naked) void FloorSSE4()
{
	__asm {
		sub esp, 8
		movsd xmm0, qword ptr [esp + 12]
		// roundsd xmm0, xmm0, 9  (floor | suppress precision exception)
		_emit 0x66
		_emit 0x0F
		_emit 0x3A
		_emit 0x0B
		_emit 0xC0
		_emit 0x09
		movsd qword ptr [esp], xmm0
		fld qword ptr [esp]
		add esp, 8
		ret
	}
}

static __declspec(naked) void CeilSSE4()
{
	__asm {
		sub esp, 8
		movsd xmm0, qword ptr [esp + 12]
		// roundsd xmm0, xmm0, 10 (ceil | suppress precision exception)
		_emit 0x66
		_emit 0x0F
		_emit 0x3A
		_emit 0x0B
		_emit 0xC0
		_emit 0x0A
		movsd qword ptr [esp], xmm0
		fld qword ptr [esp]
		add esp, 8
		ret
	}
}

static void Apply()
{
	int r[4];
	__cpuid(r, 1);
	if (!(r[2] & (1 << 19))) {
		Log("FloorSSE4: CPU has no SSE4.1 - skipped");
		return;
	}
	static const BYTE expectFloor[] = { 0x83, 0x3D, 0x00, 0xC4, 0xC9, 0x00, 0x00, 0x0F, 0x84, 0x63, 0x70, 0x00, 0x00 };
	static const BYTE expectCeil[]  = { 0x83, 0x3D, 0x00, 0xC4, 0xC9, 0x00, 0x00, 0x0F, 0x84, 0x04, 0x82, 0x00, 0x00 };
	int done = 0;
	if (!memcmp((void *)kFloor, expectFloor, sizeof(expectFloor))) {
		BYTE jmp[5] = { 0xE9 };
		*(int32_t *)(jmp + 1) = Rel32((BYTE *)kFloor, FloorSSE4);
		if (PatchBytes(kFloor, nullptr, jmp, 5)) done++;
	}
	if (!memcmp((void *)kCeil, expectCeil, sizeof(expectCeil))) {
		BYTE jmp[5] = { 0xE9 };
		*(int32_t *)(jmp + 1) = Rel32((BYTE *)kCeil, CeilSSE4);
		if (PatchBytes(kCeil, nullptr, jmp, 5)) done++;
	}
	g_fixFloorSSE4.active = done > 0;
	Log("FloorSSE4: %d/2 (floor/ceil use SSE4.1 roundsd)", done);
}

Fix g_fixFloorSSE4 = {
	.section = "FloorSSE4",
	.title = "floor/ceil за одну инструкцию",
	.gain = "-0.4% кадра",
	.summary = "Округление floor() и ceil() через инструкцию roundsd (SSE4.1) вместо тяжёлой функции 2004 года.",
	.details = "Встроенные floor() и ceil() в gta_sa.exe на каждый вызов читают регистры состояния FPU/SSE (stmxcsr + fnstcw, "
	           "сбрасывая конвейер) и затем вручную сдвигают биты мантиссы за ~40 инструкций. Вызываются по всему движку: "
	           "рендер, коллизии, стриминг, отражения машин.\n\n"
	           "Инструкция roundsd (SSE4.1, все процессоры с 2008 года) даёт побитово тот же результат IEEE 754 за один такт. Без потерь.",
	.gta = true,
	.atStart = Apply,
};
