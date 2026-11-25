// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [SampKeyPollCache] SA-MP's per-frame render hook (samp+0x71130 in 0.3.7-R1) calls GetAsyncKeyState
// 3 times every single frame (VK_F5 netstats, VK_F10 hide HUD, VK_F1 help) — 2000-3000 kernel
// syscalls/sec (win32u!NtUserGetAsyncKeyState, ~0.7-1.1% of the main thread). SAMPFUNCS and
// mousefix also call GetAsyncKeyState via IAT. Caching each VK's state on the main thread for
// interval_ms (default 4 ms = 250 Hz) and clearing bit 0x0001 on cached hits eliminates ~90% of
// kernel transitions with zero perceptible delay and zero double-triggers.

#include "../registry.h"

static SHORT(WINAPI *oGetAsyncKeyState)(int);

struct KeySlot {
	LONGLONG at;
	SHORT val;
};
static KeySlot g_keys[256];
static LONGLONG g_freq;
static int g_intervalMs = 4;
static unsigned g_calls, g_skipped;

static SHORT WINAPI hkGetAsyncKeyState(int vk)
{
	if (GetCurrentThreadId() != g_mainThread || g_intervalMs <= 0 || (unsigned)vk >= 256)
		return oGetAsyncKeyState(vk);

	g_calls++;
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	const LONGLONG maxAge = (g_freq * g_intervalMs) / 1000;
	KeySlot &s = g_keys[vk];
	if (s.at && now.QuadPart - s.at < maxAge) {
		g_skipped++;
		return (SHORT)(s.val & ~1); // preserve 0x8000 (currently held), never re-fire latched 0x0001
	}
	SHORT v = oGetAsyncKeyState(vk);
	s.at = now.QuadPart;
	s.val = v;
	return v;
}

static void Load(const Ini &ini)
{
	g_intervalMs = ini.Int("SampKeyPollCache", "interval_ms", 4);
	if (g_intervalMs < 0) g_intervalMs = 0;
	if (g_intervalMs > 16) g_intervalMs = 16;
}

static bool Apply(BYTE *samp)
{
	LARGE_INTEGER f;
	QueryPerformanceFrequency(&f);
	g_freq = f.QuadPart ? f.QuadPart : 10000000LL;
	oGetAsyncKeyState = GetAsyncKeyState;

	int hooked = 0;

	// 1. SA-MP render hook (R1..R5, 0.3.DL): 6-byte instruction right before `push VK_F5; call esi`
	//    loads ESI = GetAsyncKeyState (packed 6-byte call thunk on R1/R2, `mov esi, [IAT]` on R3..DL).
	BYTE *pF5 = SampAt(S_RENDER_HOOK_F5);
	if (pF5 && IsReadableRange(pF5 - 6, 13)) {
		BYTE *pLoad = pF5 - 6;
		static const BYTE exF5[] = { 0x6A, 0x74, 0xFF, 0xD6, 0x66, 0x85, 0xC0 };
		const bool validLoad = (pLoad[0] == 0x8B && pLoad[1] == 0x35) ||
		                       (pLoad[0] == 0xE8 && (pLoad[5] == 0xEC || pLoad[5] == 0x9C));
		if (validLoad && memcmp(pF5, exF5, sizeof(exF5)) == 0) {
			BYTE patch[6] = { 0xBE, 0, 0, 0, 0, 0x90 }; // mov esi, &hkGetAsyncKeyState; nop
			*(DWORD *)(patch + 1) = (DWORD)(void *)hkGetAsyncKeyState;
			BYTE *addrs[] = { pLoad };
			const void *data[] = { patch };
			size_t sizes[] = { sizeof(patch) };
			if (PatchMainThread(addrs, data, sizes, 1)) hooked++;
		}
	}

	// 2. Also cache IAT calls in SAMPFUNCS.asi and mousefix.asi if loaded
	const char *mods[] = { "SAMPFUNCS.asi", "mousefix.asi" };
	for (const char *name : mods) {
		HMODULE m = GetModuleHandleA(name);
		if (!m) continue;
		void *prev = nullptr;
		if (PatchIat(m, "user32.dll", "GetAsyncKeyState", (void *)hkGetAsyncKeyState, &prev))
			hooked++;
	}

	g_fixSampKeyPollCache.active = hooked > 0;
	Log("SampKeyPollCache: %d source(s) cached (%d ms window)", hooked, g_intervalMs);
	return true;
}

static void Report(std::string &out, double secs)
{
	if (g_calls)
		Appendf(out, Tr("SampKeyPollCache: пропущено %.0f%% из %.0f GetAsyncKeyState/с\n", "SampKeyPollCache: skipped %.0f%% of %.0f GetAsyncKeyState/s\n"), 100.0 * g_skipped / g_calls, g_calls / secs);
	g_calls = g_skipped = 0;
}

static const Param kParams[] = {
	{ "interval_ms", "Интервал, мс", &g_intervalMs, nullptr, 0, 16, true },
};

Fix g_fixSampKeyPollCache = {
	.section = "SampKeyPollCache",
	.title = "Кеш опроса клавиш F1/F5/F10 в SA-MP",
	.gain = "-0.7–1.1% кадра",
	.summary = "Не вызывает системный GetAsyncKeyState по 2000-3000 раз в секунду для проверки F1, F5 и F10.",
	.details = "В рендер-хуке SA-MP каждый кадр трижды вызывается win32u!NtUserGetAsyncKeyState (для F5, F10 и F1). "
	           "На высоком FPS это тысячи переходов в ядро Windows в секунду (~1% всего кадра), а SAMPFUNCS и "
	           "mousefix добавляют ещё.\n\n"
	           "Состояние клавиши кешируется на interval_ms (по умолчанию 4 мс = 250 Гц), а флаг одиночного нажатия "
	           "(бит 0x0001) сбрасывается на попадании в кеш, чтобы не было двойных срабатываний.",
	.load = Load,
	.atSamp = Apply,
	.report = Report,
	.params = kParams,
	.paramCount = Count(kParams),
};
