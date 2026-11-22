// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [AnimMathSSE] CQuaternion::Slerp (0x59C300, every bone of every animated clump, every update) runs
// two x87 fsin (~100 cycles each). Same branches (halftheta == 0 -> copy `to`; > pi/2 -> theta' =
// pi - theta and the second weight negated), same weights * quaternions, with sin as a degree-9
// minimax polynomial on [-pi/2, pi/2] (max error 3.3e-9, below float resolution). Arguments outside
// the polynomial's range (theta outside (0, pi], t outside [0, 1], NaN) go to the original through a
// trampoline.
// RpHAnimIDGetIndex (0x7C51A0) finds a bone by ID with a linear scan of the hierarchy's node info,
// for every bone lookup; the index is cached per (hierarchy, ID) and checked before use.

#include "../registry.h"

static const DWORD kSlerp = 0x59C300;
static const DWORD kHAnimIDGetIndex = 0x7C51A0;

static DWORD g_slerpOrig; // trampoline: the first two instructions, then jmp 0x59C30A

static inline float SinPoly(float x)
{
	float x2 = x * x;
	return x * (0.9999999765893699f + x2 * (-0.16666647634365314f + x2 * (0.008332899819376866f + x2 * (-0.00019800897549276573f + x2 * 2.5904881178100494e-06f))));
}

static void __fastcall hkSlerp(float *self, void *, const float *from, const float *to, float halftheta, float sinInv, float t)
{
	if (halftheta == 0.0f) {
		self[0] = to[0]; self[1] = to[1]; self[2] = to[2]; self[3] = to[3];
		return;
	}
	if (!(halftheta > 0.0f && halftheta <= 3.14159274f && t >= 0.0f && t <= 1.0f)) {
		((void(__thiscall *)(float *, const float *, const float *, float, float, float))g_slerpOrig)(self, from, to, halftheta, sinInv, t);
		return;
	}
	float a, b;
	if (halftheta > 1.57079637f) {
		float th = 3.14159274f - halftheta;
		a = SinPoly((1.0f - t) * th) * sinInv;
		b = -(SinPoly(th * t) * sinInv);
	} else {
		a = SinPoly((1.0f - t) * halftheta) * sinInv;
		b = SinPoly(halftheta * t) * sinInv;
	}
	self[0] = a * from[0] + b * to[0];
	self[1] = b * to[1] + a * from[1];
	self[2] = b * to[2] + a * from[2];
	self[3] = b * to[3] + a * from[3];
}

struct HAnimIdxSlot { BYTE *h; int id, idx; };
static HAnimIdxSlot g_hanimIdx[2048];

static int __cdecl hkHAnimIDGetIndex(BYTE *h, int id)
{
	int n = *(int *)(h + 4);
	const BYTE *info = *(const BYTE **)(h + 0x10);
	HAnimIdxSlot &s = g_hanimIdx[(((DWORD)h >> 4) ^ ((DWORD)id * 2654435761u >> 21)) & 2047];
	if (s.h == h && s.id == id && s.idx < n && *(const int *)(info + 16 * s.idx) == id) return s.idx;
	for (int i = 0; i < n; i++)
		if (*(const int *)(info + 16 * i) == id) {
			s = { h, id, i };
			return i;
		}
	return -1;
}

static void Apply()
{
	static const BYTE slerpStart[] = { 0xD9, 0x44, 0x24, 0x0C, 0xD8, 0x1D, 0x50, 0x8B, 0x85, 0x00 };
	static const BYTE idxStart[] = { 0x8B, 0x54, 0x24, 0x04, 0x56, 0x83, 0xC8, 0xFF };
	int done = 0;
	if (!memcmp((void *)kSlerp, slerpStart, sizeof(slerpStart))) {
		BYTE *tr = (BYTE *)VirtualAlloc(nullptr, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		if (tr) {
			memcpy(tr, slerpStart, sizeof(slerpStart)); // fld [esp+0xC]; fcomp [0x858B50] - no relative operands
			tr[10] = 0xE9;
			*(int32_t *)(tr + 11) = (int32_t)(kSlerp + sizeof(slerpStart) - (DWORD)(tr + 15));
			FlushInstructionCache(GetCurrentProcess(), tr, 32);
			g_slerpOrig = (DWORD)tr;
			BYTE jmp[5] = { 0xE9 };
			*(int32_t *)(jmp + 1) = Rel32((BYTE *)kSlerp, hkSlerp);
			if (PatchBytes(kSlerp, nullptr, jmp, 5)) done++;
		}
	} else Log("AnimMathSSE: CQuaternion::Slerp is not original - skipped");
	if (!memcmp((void *)kHAnimIDGetIndex, idxStart, sizeof(idxStart))) {
		BYTE jmp[5] = { 0xE9 };
		*(int32_t *)(jmp + 1) = Rel32((BYTE *)kHAnimIDGetIndex, hkHAnimIDGetIndex);
		if (PatchBytes(kHAnimIDGetIndex, nullptr, jmp, 5)) done++;
	} else Log("AnimMathSSE: RpHAnimIDGetIndex is not original - skipped");
	g_fixAnimMathSSE.active = done > 0;
	Log("AnimMathSSE: %d/2 (Slerp without x87 fsin, bone index cache)", done);
}

Fix g_fixAnimMathSSE = {
	.section = "AnimMathSSE",
	.title = "Математика скелета",
	.gain = "-2% кадра",
	.summary = "Синус для поворота костей без медленного fsin и кеш поиска кости по номеру.",
	.details = "Для каждой кости каждого педа игра дважды считает синус старой инструкцией fsin (~100 тактов), а кость по номеру "
	           "ищет перебором всех ~32 костей.\n\n"
	           "Синус считается точным многочленом (ошибка меньше точности float), номер кости запоминается и проверяется. Без потерь.",
	.gta = true,
	.atStart = Apply,
};
