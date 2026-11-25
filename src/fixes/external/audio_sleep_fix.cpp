// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [AudioSleepFix] BASS_ChannelPlay (bass.dll, called by moonloader scripts for every sound - gun
// shot, hit) waits for its mixer with Sleep(1) on the calling thread, which is the game's main thread:
// a timer tick, ~1.5 ms per sound, two per shot. On the main thread, short Sleeps whose caller is
// bass.dll become SwitchToThread: the same wait loop, re-checked as soon as the mixer has run instead
// of a tick later. KernelBase!Sleep is hot-patchable (5 x int3 + mov edi, edi): the 2-byte jump goes
// to a long jump.
// That shortens the wait but cannot end it: starting a sound still waits for the mixer thread to take
// it, and when the output has gone quiet that is 10-15 ms of the frame. So MoonLoader's BASS_ChannelPlay
// itself is taken off the game thread: the call is queued, a worker makes it, the script gets TRUE at
// once. A stop, pause or free of a channel whose play is still queued drops the queued play first, and
// "is it active" answers "playing" for it, so a script sees what it would have seen.

#include <intrin.h>

#include "../registry.h"

static DWORD g_sleepCont; // KernelBase!Sleep + 2 (after mov edi, edi)
static BYTE *g_bassLo, *g_bassHi;

static __declspec(noinline) void __stdcall SleepFromCaller(DWORD ms, BYTE *ra)
{
	if (ms - 1 < 5 && GetCurrentThreadId() == g_mainThread) {
		if (!g_bassLo)
			if (HMODULE b = GetModuleHandleA("bass.dll")) {
				g_bassHi = ModuleEnd(b);
				g_bassLo = (BYTE *)b;
			}
		if (ra >= g_bassLo && ra < g_bassHi) {
			SwitchToThread();
			return;
		}
	}
	((void(WINAPI *)(DWORD))g_sleepCont)(ms);
}

static void WINAPI hkSleep(DWORD ms)
{
	SleepFromCaller(ms, (BYTE *)_ReturnAddress());
}

// ---- BASS_ChannelPlay on a worker thread

static BOOL(WINAPI *oChannelPlay)(DWORD handle, BOOL restart);
static BOOL(WINAPI *oChannelStop)(DWORD handle);
static BOOL(WINAPI *oChannelPause)(DWORD handle);
static BOOL(WINAPI *oStreamFree)(DWORD handle);
static DWORD(WINAPI *oChannelIsActive)(DWORD handle);

struct QueuedPlay {
	DWORD handle;
	BOOL restart;
};
static CRITICAL_SECTION g_playLock;
static QueuedPlay g_plays[64];
static int g_playN;
static volatile DWORD g_playingNow; // the channel the worker is starting right now
static HANDLE g_playEvent;
static DWORD g_queued;

static DWORD WINAPI PlayThread(LPVOID)
{
	for (;;) {
		WaitForSingleObject(g_playEvent, INFINITE);
		for (;;) {
			EnterCriticalSection(&g_playLock);
			if (!g_playN) {
				LeaveCriticalSection(&g_playLock);
				break;
			}
			QueuedPlay play = g_plays[0];
			memmove(g_plays, g_plays + 1, --g_playN * sizeof(QueuedPlay));
			g_playingNow = play.handle;
			LeaveCriticalSection(&g_playLock);
			oChannelPlay(play.handle, play.restart);
			g_playingNow = 0;
		}
	}
}

static bool IsQueued(DWORD handle)
{
	bool queued = g_playingNow == handle;
	EnterCriticalSection(&g_playLock);
	for (int i = 0; i < g_playN; i++) queued |= g_plays[i].handle == handle;
	LeaveCriticalSection(&g_playLock);
	return queued;
}

// before anything that ends a channel: its play must not arrive after it
static void DropQueued(DWORD handle)
{
	EnterCriticalSection(&g_playLock);
	int kept = 0;
	for (int i = 0; i < g_playN; i++)
		if (g_plays[i].handle != handle) g_plays[kept++] = g_plays[i];
	g_playN = kept;
	LeaveCriticalSection(&g_playLock);
	while (g_playingNow == handle) SwitchToThread();
}

static BOOL WINAPI hkChannelPlay(DWORD handle, BOOL restart)
{
	if (GetCurrentThreadId() == g_mainThread) {
		EnterCriticalSection(&g_playLock);
		bool room = g_playN < (int)(sizeof(g_plays) / sizeof(g_plays[0]));
		if (room) g_plays[g_playN++] = { handle, restart };
		LeaveCriticalSection(&g_playLock);
		if (room) {
			g_queued++;
			SetEvent(g_playEvent);
			return TRUE;
		}
	}
	return oChannelPlay(handle, restart);
}

static BOOL WINAPI hkChannelStop(DWORD handle)
{
	DropQueued(handle);
	return oChannelStop(handle);
}

static BOOL WINAPI hkChannelPause(DWORD handle)
{
	DropQueued(handle);
	return oChannelPause(handle);
}

static BOOL WINAPI hkStreamFree(DWORD handle)
{
	DropQueued(handle);
	return oStreamFree(handle);
}

static DWORD WINAPI hkChannelIsActive(DWORD handle)
{
	if (IsQueued(handle)) return 1; // BASS_ACTIVE_PLAYING
	return oChannelIsActive(handle);
}

static void PlayOffTheGameThread()
{
	HMODULE ml = GetModuleHandleA("MoonLoader.asi");
	if (!ml) return;
	InitializeCriticalSection(&g_playLock);
	g_playEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
	// the hooks that keep the order go in first; the play hook last, once the worker exists
	if (!PatchIat(ml, "bass.dll", "BASS_ChannelStop", (void *)hkChannelStop, (void **)&oChannelStop) ||
	    !PatchIat(ml, "bass.dll", "BASS_ChannelPause", (void *)hkChannelPause, (void **)&oChannelPause) ||
	    !PatchIat(ml, "bass.dll", "BASS_StreamFree", (void *)hkStreamFree, (void **)&oStreamFree) ||
	    !PatchIat(ml, "bass.dll", "BASS_ChannelIsActive", (void *)hkChannelIsActive, (void **)&oChannelIsActive)) {
		Log("AudioSleepFix: MoonLoader's BASS imports were not found - sounds start on the game thread as before");
		return;
	}
	HANDLE thread = CreateThread(nullptr, 0, PlayThread, nullptr, 0, nullptr);
	if (!thread) return;
	CloseHandle(thread);
	if (PatchIat(ml, "bass.dll", "BASS_ChannelPlay", (void *)hkChannelPlay, (void **)&oChannelPlay))
		Log("AudioSleepFix: script sounds are started off the game thread");
}

static void Report(std::string &out, double secs)
{
	if (g_queued) Appendf(out, Tr("AudioSleepFix: звуков скриптов %.1f/с запущено вне потока игры\n", "AudioSleepFix: %.1f script sounds/s started off main thread\n"), g_queued / secs);
	g_queued = 0;
}

static bool Apply(BYTE *)
{
	PlayOffTheGameThread();
	HMODULE kb = GetModuleHandleA("kernelbase.dll");
	BYTE *f = kb ? (BYTE *)GetProcAddress(kb, "Sleep") : nullptr;
	static const BYTE pad[] = { 0xCC, 0xCC, 0xCC, 0xCC, 0xCC }, nop[] = { 0x90, 0x90, 0x90, 0x90, 0x90 };
	if (!f || !IsReadableRange(f - 5, 7) || f[0] != 0x8B || f[1] != 0xFF || (memcmp(f - 5, pad, 5) && memcmp(f - 5, nop, 5))) {
		Log("AudioSleepFix: KernelBase!Sleep is not hot-patchable - skipped");
		return true;
	}
	g_sleepCont = (DWORD)f + 2;
	BYTE jmp[5] = { 0xE9 };
	*(int32_t *)(jmp + 1) = Rel32(f - 5, hkSleep);
	static const BYTE back[2] = { 0xEB, 0xF9 }; // jmp -7: to the long jump in the padding
	DWORD old;
	if (!VirtualProtect(f - 5, 7, PAGE_EXECUTE_READWRITE, &old)) return true;
	memcpy(f - 5, jmp, 5);
	*(volatile WORD *)f = *(const WORD *)back; // one aligned-enough 2-byte store replaces mov edi, edi
	VirtualProtect(f - 5, 7, old, &old);
	FlushInstructionCache(GetCurrentProcess(), f - 5, 7);
	g_fixAudioSleepFix.active = true;
	Log("AudioSleepFix: BASS waits on the game thread no longer sleep a timer tick");
	return true;
}

Fix g_fixAudioSleepFix = {
	.section = "AudioSleepFix",
	.title = "Звуки скриптов",
	.gain = "без подлагов",
	.summary = "Звуки из Lua (выстрелы, хитсаунды) запускаются вне потока игры и не замораживают кадр.",
	.details = "BASS при запуске каждого звука ждёт свой поток через Sleep(1) прямо в потоке игры, а это минимум тик таймера Windows. В перестрелке кадр прыгает с 6.7 до 13 мс.\n\nВ потоке игры короткие ожидания BASS не спят тик, а сразу перепроверяют готовность. Звук тот же.\n\n"
	           "Сам запуск звука тоже ждёт поток микшера BASS — до 10-15 мс, если звук давно не играл. Поэтому запуск "
	           "звуков MoonLoader вынесен в отдельный поток: скрипт сразу получает ответ, звук стартует тогда же.",
	.atSamp = Apply,
	.report = Report,
};
