// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [MsgKeysFast] CMessages::InsertPlayerControlKeysInString (0x69E160, ~0.4% self) allocates 816 B
// on the stack, zeroes 400 B (rep stosd * 100), copies the entire string to the stack and back, and
// zeroes the tail up to 400 B — EVEN WHEN the string contains no "~k~" key token at all (99.9% of
// all SA-MP HUD/GXT strings!). Scanning for "~k~" first skips all 800 B of wasted memory traffic.

#include "../registry.h"

static BYTE *g_tramp;
static unsigned g_calls, g_skipped;

static void __cdecl hkInsertControlKeys(char *str)
{
	if (!str) return;
	g_calls++;
	for (const char *p = str; *p; p++) {
		if (p[0] == '~' && p[1] == 'k' && p[2] == '~') {
			((void(__cdecl *)(char *))g_tramp)(str);
			return;
		}
	}
	g_skipped++;
}

static void Apply()
{
	// 0069E160: 8B 54 24 04  mov edx, [esp+4]   (4 bytes)
	// 0069E164: 81 EC 30 03 00 00 sub esp, 330h (6 bytes) -> 10 bytes total, cleanly relocatable!
	static const BYTE kExpect[10] = { 0x8B, 0x54, 0x24, 0x04, 0x81, 0xEC, 0x30, 0x03, 0x00, 0x00 };
	if (memcmp((void *)0x69E160, kExpect, sizeof(kExpect)) != 0) {
		Log("MsgKeysFast: prologue mismatch at 0x69E160 - skipped");
		return;
	}

	g_tramp = (BYTE *)VirtualAlloc(nullptr, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (!g_tramp) return;
	memcpy(g_tramp, kExpect, 10);
	g_tramp[10] = 0xE9;
	*(int32_t *)(g_tramp + 11) = Rel32(g_tramp + 10, (void *)0x69E16A);

	BYTE patch[10];
	memset(patch, 0x90, sizeof(patch));
	patch[0] = 0xE9;
	*(int32_t *)(patch + 1) = Rel32((BYTE *)0x69E160, (const void *)hkInsertControlKeys);
	g_fixMsgKeysFast.active = PatchBytes(0x69E160, nullptr, patch, sizeof(patch));
	Log(g_fixMsgKeysFast.active
	        ? "MsgKeysFast: CMessages::InsertPlayerControlKeysInString fast-skips strings without ~k~"
	        : "MsgKeysFast: patch failed");
}

static void Report(std::string &out, double secs)
{
	if (g_calls)
		Appendf(out, "MsgKeysFast: пропущено %.0f%% из %.0f строк/с\n", 100.0 * g_skipped / g_calls, g_calls / secs);
	g_calls = g_skipped = 0;
}

Fix g_fixMsgKeysFast = {
	.section = "MsgKeysFast",
	.title = "Пропуск пустой подстановки клавиш в HUD",
	.gain = "-0.4% кадра",
	.summary = "Не обнуляет 800 байт на стеке в InsertPlayerControlKeysInString, если в строке нет токена ~k~.",
	.details = "Перед выводом HUD-строк GTA SA зовёт CMessages::InsertPlayerControlKeysInString (0x69E160), которая "
	           "каждый раз обнуляет 400 байт буфера, дважды копирует строку и обнуляет хвост — даже если в тексте "
	           "вообще нет подстановок клавиш ~k~ (99.9% строк в SA-MP).\n\n"
	           "Быстрая проверка на ~k~ сразу возвращает управление. Если ~k~ есть — вызывается оригинал без изменений.",
	.gta = true,
	.atStart = Apply,
	.report = Report,
};
