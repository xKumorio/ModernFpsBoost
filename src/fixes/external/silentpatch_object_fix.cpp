// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [SilentPatchObjectFix] SilentPatchSA.asi hooks CObject::Render (0x59F180) to fix broken/remapped
// object rendering. Inside its hook (target+0x4B), MSVC emitted a 256-iteration loop calling a
// non-inlined constructor (std::pair<void*,void*> = {0,0}) to zero-initialize 2048 bytes of stack
// array BEFORE checking whether the object even has a remap texture (m_nRemapTxd != -1 &&
// m_nObjectType == 3 && m_nColDamageEffect < 0), which is false for 99.9% of objects.
// Even on the 0.1% path, the array is written sequentially from slot 0 and only read up to `count`
// (`esi = (end - start) >> 3`, initialized to 0 at target+0x70). Thus the 256-call loop is 100%
// dead work burning ~2.0% of the entire game CPU in SA-MP scenes with many objects.
//
// SilentPatch installs its hook lazily during game startup (often after samp.dll loads), so
// SilentPatchObjectFixTick() retries once per second until the hook appears or the timeout expires.

#include "../registry.h"

static bool g_done;
static int g_tries;

static void TryPatch(bool finalTry)
{
	if (g_done || !g_fixSilentPatchObjectFix.enabled) return;
	if (!IsGta10US()) { g_done = true; return; }

	const BYTE *entry = (const BYTE *)0x59F180;
	if (entry[0] != 0xE9) {
		if (finalTry) {
			g_done = true;
			Log("SilentPatchObjectFix: CObject::Render not hooked by SilentPatch - skipped");
		}
		return;
	}

	g_done = true;
	BYTE *fn = (BYTE *)(0x59F185 + *(const int32_t *)(entry + 1));
	MEMORY_BASIC_INFORMATION mbi{};
	if (!VirtualQuery(fn, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT ||
	    (DWORD)fn + 0x1B0 > (DWORD)mbi.BaseAddress + mbi.RegionSize) {
		Log("SilentPatchObjectFix: target outside readable region - skipped");
		return;
	}

	static const BYTE kLoopHead[14] = { 0x8D, 0xB5, 0xF0, 0xF7, 0xFF, 0xFF, 0xBF, 0x00, 0x01, 0x00, 0x00, 0x8B, 0xCE, 0xE8 };
	static const BYTE kLoopTail[8]  = { 0x83, 0xC6, 0x08, 0x83, 0xEF, 0x01, 0x75, 0xF1 };
	static const BYTE kXorEsi[2]    = { 0x33, 0xF6 };
	static const BYTE kRestore[6]   = { 0x33, 0xC0, 0x85, 0xF6, 0x74, 0x15 };

	if (memcmp(fn + 0x4D, kLoopHead, sizeof(kLoopHead)) != 0 ||
	    memcmp(fn + 0x5F, kLoopTail, sizeof(kLoopTail)) != 0 ||
	    memcmp(fn + 0x72, kXorEsi, sizeof(kXorEsi)) != 0 ||
	    memcmp(fn + 0x19B, kRestore, sizeof(kRestore)) != 0) {
		Log("SilentPatchObjectFix: SilentPatch build mismatch - skipped");
		return;
	}

	BYTE nops[26];
	memset(nops, 0x90, sizeof(nops));
	g_fixSilentPatchObjectFix.active = PatchBytes((DWORD)(fn + 0x4D), nullptr, nops, sizeof(nops));
	Log(g_fixSilentPatchObjectFix.active
	        ? "SilentPatchObjectFix: removed 256x zero-init loop in SilentPatch CObject::Render"
	        : "SilentPatchObjectFix: patch failed");
}

static bool Apply(BYTE *)
{
	TryPatch(false);
	return true;
}

void SilentPatchObjectFixTick()
{
	if (g_done || !g_fixSilentPatchObjectFix.enabled) return;
	TryPatch(++g_tries >= 20);
}

Fix g_fixSilentPatchObjectFix = {
	.section = "SilentPatchObjectFix",
	.title = "Ускорить CObject::Render в SilentPatch",
	.gain = "-2% кадра",
	.summary = "Убирает 256 бесполезных вызовов конструктора на каждый рисуемый объект внутри хука SilentPatchSA.asi.",
	.details = "SilentPatchSA.asi перехватывает CObject::Render и на КАЖДЫЙ нарисованный объект в кадре в цикле 256 раз "
	           "вызывает конструктор std::pair, чтобы обнулить 2 КБ временного массива на стеке — ещё до проверки, "
	           "нужен ли этот массив вообще (для 99.9% объектов не нужен).\n\n"
	           "Даже когда массив используется, элементы сначала записываются подряд от 0 до count, и читаются только "
	           "записанные count штук. Удаление этого мёртвого цикла экономит ~2% всего кадра на картах с объектами SA-MP "
	           "при 100% идентичном поведении.",
	.atSamp = Apply,
};
