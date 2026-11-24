// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [PlayerLoop] CPlayerPool::Process (R1 samp+0x10320) calls CRemotePlayer::Process (samp+0x12EF0) for
// every connected player each frame - ~950 on a full server - and that returns at once when the
// player has no ped (not streamed in). The cost is two cache misses per slot (CPlayerInfo, then
// CRemotePlayer). The loop (from R1 samp+0x10359) is replaced by one that first collects the slots
// with a ped, prefetching both levels for all slots at once so the misses overlap, and then calls
// Process only for those, inside the same SEH frame and try state as the original. If Process
// throws, SA-MP's handler resumes at the next index and re-enters this loop from there.

#include <xmmintrin.h>

#include "../registry.h"

static WORD g_plAct[kMaxPlayers];
static DWORD g_plN, g_plK;
static DWORD g_plGetAt, g_plProcess, g_plExit;
static int g_plUsed; // CPlayerPool used[] offset of this version, read by the asm below

static void __cdecl PlayerLoopCollect(BYTE *pool, int start)
{
	static WORD id[kMaxPlayers];
	static BYTE *rp[kMaxPlayers];
	const int *used = (const int *)(pool + g_sl.poolUsed);
	BYTE *const *info = (BYTE *const *)(pool + g_sl.poolInfo);
	int n = 0;
	for (int i = start; i < kMaxPlayers; i++)
		if (used[i] == 1 && info[i]) {
			_mm_prefetch((const char *)info[i], _MM_HINT_T0);
			id[n++] = (WORD)i;
		}
	for (int k = 0; k < n; k++) {
		rp[k] = *(BYTE **)(info[id[k]] + g_sl.infoRemote);
		if (rp[k]) _mm_prefetch((const char *)rp[k] + g_sl.remotePed, _MM_HINT_T0);
	}
	int m = 0;
	for (int k = 0; k < n; k++) // CRemotePlayer::Process returns at once without a ped
		if (rp[k] && *(void **)(rp[k] + g_sl.remotePed)) g_plAct[m++] = id[k];
	g_plN = m;
	g_plK = 0;
}

// entered at the loop head with edi = pool, esi = first index, ebx = -1, ebp = the function's frame
// (the frame slots -0x1C and -4 are the same in every version: checked in Apply)
static __declspec(naked) void PlayerLoopHook()
{
	__asm {
		push esi
		push edi
		call PlayerLoopCollect
		add esp, 8
	next:
		mov eax, g_plK
		cmp eax, g_plN
		jae done
		inc g_plK
		movzx esi, word ptr [g_plAct + eax * 2]
		mov [ebp - 0x1C], esi       // index the SEH handler resumes from
		mov eax, g_plUsed
		lea eax, [eax + esi * 4]
		cmp dword ptr [edi + eax], 1
		jne next
		mov dword ptr [ebp - 4], 0  // try state, as the original
		push esi
		mov ecx, edi
		call g_plGetAt
		test eax, eax
		je skip
		mov ecx, eax
		call g_plProcess
	skip:
		mov [ebp - 4], ebx
		jmp next
	done:
		mov esi, 0x3EC
		jmp g_plExit
	}
}

static bool Apply(BYTE *)
{
	static const BYTE expect[] = { 0x66, 0x81, 0xFE, 0xEC, 0x03, 0x73 }; // cmp si, 0x3EC; jae exit
	BYTE *at = SampAt(S_PLAYER_LOOP), *call = SampAt(S_PLAYER_LOOP_GETAT);
	g_plGetAt = (DWORD)SampAt(S_PLAYERPOOL_GETAT);
	g_plProcess = (DWORD)SampAt(S_REMOTE_PROCESS);
	g_plExit = (DWORD)SampAt(S_PLAYER_LOOP_EXIT);
	g_plUsed = g_sl.poolUsed;
	// head, exit (jae target), GetAt + Process calls and the frame slots must all be the original ones
	bool ok = at && call && g_plGetAt && g_plProcess && g_plExit && IsReadableRange(at, 7) && IsReadableRange(call, 12) &&
	          SafeMemMatch(at, expect, sizeof(expect)) &&
	          (DWORD)(at + 7 + (int8_t)at[6]) == g_plExit && IsCallTo(call, g_plGetAt) && call[5] == 0x8B && call[6] == 0xC8 &&
	          IsCallTo(call + 7, g_plProcess) && SampVal(S_PLAYER_LOOP_IDX) == -0x1C && SampVal(S_PLAYER_LOOP_TRY) == -4;
	if (!ok) {
		Log("PlayerLoop: the player loop is not original - skipped");
		return true;
	}
	BYTE jmp[5] = { 0xE9 };
	*(int32_t *)(jmp + 1) = Rel32(at, PlayerLoopHook);
	BYTE *addrs[] = { at };
	const void *data[] = { jmp };
	size_t sizes[] = { 5 };
	if ((g_fixPlayerLoop.active = PatchMainThread(addrs, data, sizes, 1))) Log("PlayerLoop: only players with a ped are processed");
	else Log("PlayerLoop: could not patch - skipped");
	return true;
}

static void Report(std::string &out, double)
{
	Appendf(out, "PlayerLoop: игроков с педом в этом кадре: %lu\n", g_plN);
}

Fix g_fixPlayerLoop = {
	.section = "PlayerLoop",
	.title = "Обход игроков",
	.gain = "-1.5–2% кадра",
	.summary = "Обработка вызывается только для игроков рядом (с педом), память читается заранее и разом.",
	.details = "SA-MP каждый кадр вызывает обработку для каждого подключённого игрока (~950 на полном сервере), хотя у далёких она сразу выходит. Дорого чтение памяти: два промаха кэша на каждого.\n\nОдним проходом с упреждающей загрузкой собираются игроки с педом, обработка идёт только для них тем же кодом SA-MP. Без потерь.",
	.atSamp = Apply,
	.report = Report,
};
