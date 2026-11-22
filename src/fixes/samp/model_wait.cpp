// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [SampModelWait] When SA-MP creates an object whose model is not in memory yet, it requests the model
// and then waits for it on the game's thread: up to 15 x { Sleep(1); loaded? } (R1 samp+0xA7B63). The
// model cannot arrive during that wait - the game turns a model read from the disk into an object on
// this same thread, which is asleep - so every such object costs 15 timer ticks (15-230 ms), and a
// server that streams in a few dozen at once freezes the game for up to a second.
// The Sleep(1) of that loop is removed: the 15 checks run at once, and what happens after them (the
// object is created, or left for later) is as before. The loop is found by its code (R1 has it twice,
// samp+0x97E70 and samp+0xA7B63), so every SA-MP version that has it is covered.

// The same wait is in CVehicle::AddComponent (R1 samp+0xB1A70, a tuning part): REQUEST_VEHICLE_MOD,
// then up to 20 x { HAS_VEHICLE_MOD_LOADED?; Sleep(1) } with Sleep in ebx. It never succeeds for a part
// that is not in memory yet (30 ms, and the part is not added). There Sleep is replaced by
// CStreaming::LoadAllRequestedModels(false) - what SA-MP's own LOAD_ALL_MODELS_NOW a few lines above
// does: the part is loaded in the first pass and added.

#include "../registry.h"

static void WINAPI LoadNow(DWORD)
{
	((void(__cdecl *)(bool))0x40EA10)(false); // CStreaming::LoadAllRequestedModels
}

// push 1; call ebx; dec edi; jne - and before it mov ebx, [Sleep]
static int PatchPartWait(BYTE *samp, BYTE *from, BYTE *to)
{
	static const BYTE loop[] = { 0x6A, 0x01, 0xFF, 0xD3, 0x4F, 0x75 };
	int patched = 0;
	for (BYTE *p = from; p < to; p++) {
		if (*p != loop[0] || memcmp(p, loop, sizeof(loop))) continue;
		for (BYTE *m = p - 6; m > p - 0x40; m--) {
			if (m[0] != 0x8B || m[1] != 0x1D) continue;
			BYTE mov[6] = { 0xBB, 0, 0, 0, 0, 0x90 }; // mov ebx, LoadNow; nop
			*(DWORD *)(mov + 1) = (DWORD)LoadNow;
			BYTE old[6];
			memcpy(old, m, 6);
			if (PatchBytes((DWORD)m, old, mov, 6)) {
				patched++;
				Log("SampModelWait: the tuning part wait at samp+%X loads the part instead of sleeping", (DWORD)(p - samp));
			}
			break;
		}
	}
	return patched;
}

static bool Apply(BYTE *samp)
{
	// push 1; call edi (Sleep); inc esi; cmp esi, 15; je
	static const BYTE loop[] = { 0x6A, 0x01, 0xFF, 0xD7, 0x46, 0x83, 0xFE, 0x0F, 0x74 };
	static const BYTE nops[] = { 0x90, 0x90, 0x90, 0x90 };
	auto nt = (IMAGE_NT_HEADERS *)(samp + ((IMAGE_DOS_HEADER *)samp)->e_lfanew);
	IMAGE_SECTION_HEADER *code = IMAGE_FIRST_SECTION(nt);
	BYTE *from = samp + code->VirtualAddress, *to = from + code->Misc.VirtualSize - sizeof(loop);
	int found = 0, patched = 0;
	for (BYTE *p = from; p < to; p++)
		if (*p == loop[0] && !memcmp(p, loop, sizeof(loop))) {
			found++;
			if (PatchBytes((DWORD)p, loop, nops, sizeof(nops))) {
				patched++;
				Log("SampModelWait: the model wait at samp+%X no longer sleeps", (DWORD)(p - samp));
			}
		}
	int parts = PatchPartWait(samp, from, to);
	patched += parts;
	found += parts;
	if (!found) Log("SampModelWait: this samp.dll has no such wait loop - nothing to do");
	g_fixSampModelWait.active = patched > 0;
	return true;
}

Fix g_fixSampModelWait = {
	.section = "SampModelWait",
	.title = "Ожидание модели объекта",
	.gain = "без фризов до 1 с",
	.summary = "SA-MP при создании объекта с незагруженной моделью больше не усыпляет игру на 15 тиков таймера.",
	.details = "Когда сервер создаёт объект, модели которого ещё нет в памяти, SA-MP запрашивает модель и ждёт её прямо в потоке "
	           "игры: до 15 раз Sleep(1). Но модель за это время появиться не может — распаковывает её этот же поток, а он "
	           "спит. Каждый такой объект стоит 15-230 мс, несколько десятков разом — фриз до секунды.\n\n"
	           "Sleep из этого ожидания убран: те же 15 проверок проходят сразу, дальше всё как раньше.\n\n"
	           "Так же SA-MP ждёт детали тюнинга машины (до 20 раз Sleep(1), фриз ~30 мс, а деталь так и не ставится). "
	           "Там вместо Sleep деталь теперь загружается сразу и ставится.",
	.atSamp = Apply,
};
