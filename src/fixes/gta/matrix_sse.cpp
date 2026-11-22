// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [MatrixSSE] RwMatrixMultiply (0x7F18B0) and RwMatrixTransform (0x7F25A0) multiply through the
// matrix module's function pointer ([RwEngineInstance + matrix module offset] + 8), which RW sets to
// an x87 routine (0x8572A0): dst rows = a rows * b's 3x3 (+ b.pos for the position row), 12 floats,
// flags and pads untouched, every register preserved. It is called for every frame of every bone and
// vehicle part whose LTM is rebuilt (~2.5% of the main thread in a crowd). The same multiply in SSE,
// same order of additions (x*right + y*up + z*at [+ pos]), same 12 floats written, same registers kept.

#include "../registry.h"

static const DWORD kRwEngineInstance = 0xC97B24;
static const DWORD kRwMatrixModule = 0xC979BC;
static const DWORD kRwMatrixMultiplyX87 = 0x8572A0;

static __declspec(naked) void MatrixMultiplySSE(/* RwMatrix *dst, const RwMatrix *a, const RwMatrix *b */)
{
	__asm {
		push eax
		push ecx
		push edx
		mov ecx, [esp + 16]   // dst
		mov eax, [esp + 20]   // a
		mov edx, [esp + 24]   // b
		movups xmm4, [edx]
		movups xmm5, [edx + 0x10]
		movups xmm6, [edx + 0x20]
		movups xmm7, [edx + 0x30]

		movss xmm0, [eax]
		shufps xmm0, xmm0, 0
		mulps xmm0, xmm4
		movss xmm1, [eax + 4]
		shufps xmm1, xmm1, 0
		mulps xmm1, xmm5
		addps xmm0, xmm1
		movss xmm1, [eax + 8]
		shufps xmm1, xmm1, 0
		mulps xmm1, xmm6
		addps xmm0, xmm1
		movlps [ecx], xmm0
		movhlps xmm0, xmm0
		movss [ecx + 8], xmm0

		movss xmm0, [eax + 0x10]
		shufps xmm0, xmm0, 0
		mulps xmm0, xmm4
		movss xmm1, [eax + 0x14]
		shufps xmm1, xmm1, 0
		mulps xmm1, xmm5
		addps xmm0, xmm1
		movss xmm1, [eax + 0x18]
		shufps xmm1, xmm1, 0
		mulps xmm1, xmm6
		addps xmm0, xmm1
		movlps [ecx + 0x10], xmm0
		movhlps xmm0, xmm0
		movss [ecx + 0x18], xmm0

		movss xmm0, [eax + 0x20]
		shufps xmm0, xmm0, 0
		mulps xmm0, xmm4
		movss xmm1, [eax + 0x24]
		shufps xmm1, xmm1, 0
		mulps xmm1, xmm5
		addps xmm0, xmm1
		movss xmm1, [eax + 0x28]
		shufps xmm1, xmm1, 0
		mulps xmm1, xmm6
		addps xmm0, xmm1
		movlps [ecx + 0x20], xmm0
		movhlps xmm0, xmm0
		movss [ecx + 0x28], xmm0

		movss xmm0, [eax + 0x30]
		shufps xmm0, xmm0, 0
		mulps xmm0, xmm4
		movss xmm1, [eax + 0x34]
		shufps xmm1, xmm1, 0
		mulps xmm1, xmm5
		addps xmm0, xmm1
		movss xmm1, [eax + 0x38]
		shufps xmm1, xmm1, 0
		mulps xmm1, xmm6
		addps xmm0, xmm1
		addps xmm0, xmm7
		movlps [ecx + 0x30], xmm0
		movhlps xmm0, xmm0
		movss [ecx + 0x38], xmm0

		pop edx
		pop ecx
		pop eax
		ret
	}
}

static DWORD WINAPI MatrixSSEThread(LPVOID)
{
	// the matrix module is set up by RwEngineInit after the ASIs load
	for (int i = 0; i < 3000; i++) {
		BYTE *engine = *(BYTE **)kRwEngineInstance;
		DWORD modOff = *(DWORD *)kRwMatrixModule;
		if (engine && modOff) {
			DWORD *slot = (DWORD *)(engine + modOff + 8);
			if (*slot == kRwMatrixMultiplyX87) {
				*slot = (DWORD)MatrixMultiplySSE; // aligned pointer write in RW's heap data
				extern Fix g_fixMatrixSSE;
				g_fixMatrixSSE.active = true;
				Log("MatrixSSE: RwMatrixMultiply/RwMatrixTransform use SSE");
				return 0;
			}
			if (*slot) {
				Log("MatrixSSE: matrix multiply is %08lX, not the x87 one - skipped", *slot);
				return 0;
			}
		}
		Sleep(100);
	}
	return 0;
}

static void Apply()
{
	CloseHandle(CreateThread(nullptr, 0, MatrixSSEThread, nullptr, 0, nullptr));
}

Fix g_fixMatrixSSE = {
	.section = "MatrixSSE",
	.title = "Матрицы на SSE",
	.gain = "-2.5% кадра",
	.summary = "Умножение матриц RenderWare по 4 числа за инструкцию вместо старой x87-математики.",
	.details = "RenderWare 2004 года умножает матрицы x87-инструкциями по одному числу, и делает это для каждой кости каждого педа "
	           "и каждой детали машины: десятки тысяч раз в секунду.\n\n"
	           "Та же формула (тот же порядок сложений, те же 12 чисел) на SSE. Подставляется через таблицу функций самого RenderWare.",
	.gta = true,
	.atStart = Apply,
};
