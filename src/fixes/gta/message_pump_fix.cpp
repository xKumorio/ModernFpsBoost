// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [MessagePumpFix] WinMain (0x748A57) calls PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE | PM_NOYIELD)
// every single frame. At 600-1000+ FPS, NtUserPeekMessage + WH_CALLWNDPROC callbacks (DINPUT8,
// nvoglv32, MoonLoader) consume ~10-11% of the entire main thread!
//
// USER32!GetInputState() reads TEB->pClientThreadInfo->fsChangeBits & (QS_KEY | QS_MOUSEBUTTON)
// in pure usermode (~5 cycles, zero syscalls) on Windows 10/11. By waking immediately whenever
// GetInputState() != 0 (keyboard press/release or mouse click = 0.00 ms added latency), whenever
// a previous PeekMessageA returned TRUE (draining a burst), whenever the game is unfocused or in
// the pause menu, and otherwise polling at most once per interval_ms (default 3 ms = 333 Hz), we
// eliminate 65-80% of Win32 kernel message-pump transitions with zero perceptible input lag.

#include "../registry.h"

static const DWORD kIatPeekMessageA = 0x8582BC;
static const DWORD kForegroundApp   = 0x8D621C; // 1 while GTA window is focused
static const DWORD kMenuActive      = 0xBA67A4; // 1 while ESC pause menu is open

static BOOL(WINAPI *oPeekMessageA)(LPMSG, HWND, UINT, UINT, UINT);

static int g_intervalMs = 3;
static LONGLONG g_freq;
static LONGLONG g_lastPump;
static LONGLONG g_lastRawInput;
static bool g_draining;
static unsigned g_calls, g_skipped;

static BOOL WINAPI hkPeekMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg)
{
	if (GetCurrentThreadId() != g_mainThread || g_intervalMs <= 0 ||
	    hWnd != nullptr || wMsgFilterMin != 0 || wMsgFilterMax != 0 || !(wRemoveMsg & PM_REMOVE))
		return oPeekMessageA(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);

	g_calls++;

	LARGE_INTEGER now = {};
	// Bypass throttling while Raw Input (WM_INPUT, e.g. RawInput.asi / SensFix) is active,
	// because GetInputState() only checks QS_KEY | QS_MOUSEBUTTON and ignores QS_RAWINPUT.
	bool rawInputActive = false;
	if (g_lastRawInput != 0) {
		QueryPerformanceCounter(&now);
		rawInputActive = (now.QuadPart - g_lastRawInput) < (g_freq * 5);
	}

	// Always pump immediately when unfocused, in pause menu, draining a burst, Raw Input is active,
	// or any keyboard/mouse-button event is pending (GetInputState is pure usermode on Win10/11).
	if (rawInputActive || !*(int *)kForegroundApp || *(BYTE *)kMenuActive || g_draining || GetInputState()) {
		BOOL res = oPeekMessageA(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
		g_draining = (res != FALSE);
		if (res && lpMsg && lpMsg->message == WM_INPUT) {
			if (!now.QuadPart) QueryPerformanceCounter(&now);
			g_lastRawInput = now.QuadPart;
		} else if (!res) {
			if (!now.QuadPart) QueryPerformanceCounter(&now);
			g_lastPump = now.QuadPart;
		}
		return res;
	}

	if (!now.QuadPart) QueryPerformanceCounter(&now);
	const LONGLONG minTicks = (g_freq * g_intervalMs) / 1000;
	if (now.QuadPart - g_lastPump < minTicks) {
		g_skipped++;
		return FALSE;
	}

	BOOL res = oPeekMessageA(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
	g_draining = (res != FALSE);
	if (res && lpMsg && lpMsg->message == WM_INPUT)
		g_lastRawInput = now.QuadPart;
	else if (!res)
		g_lastPump = now.QuadPart;
	return res;
}

static void Load(const Ini &ini)
{
	g_intervalMs = ini.Int("MessagePumpFix", "interval_ms", 3);
	if (g_intervalMs < 0) g_intervalMs = 0;
	if (g_intervalMs > 8) g_intervalMs = 8;
}

static void Apply()
{
	LARGE_INTEGER f;
	QueryPerformanceFrequency(&f);
	g_freq = f.QuadPart ? f.QuadPart : 10000000LL;

	void *orig = *(void **)kIatPeekMessageA;
	if (!orig) return;
	oPeekMessageA = (BOOL(WINAPI *)(LPMSG, HWND, UINT, UINT, UINT))orig;
	void *hook = (void *)hkPeekMessageA;
	g_fixMessagePumpFix.active = PatchBytes(kIatPeekMessageA, &orig, &hook, 4);
	Log(g_fixMessagePumpFix.active
	        ? "MessagePumpFix: PeekMessageA smart-throttled to %d ms (instant wake on GetInputState)"
	        : "MessagePumpFix: IAT patch failed",
	    g_intervalMs);
}

static void Report(std::string &out, double secs)
{
	if (g_calls)
		Appendf(out, Tr("MessagePumpFix: пропущено %.0f%% из %.0f PeekMessageA/с\n", "MessagePumpFix: skipped %.0f%% of %.0f PeekMessageA/s\n"), 100.0 * g_skipped / g_calls, g_calls / secs);
	g_calls = g_skipped = 0;
}

static const Param kParams[] = {
	{ "interval_ms", "Интервал, мс", &g_intervalMs, nullptr, 0, 8, true },
};

Fix g_fixMessagePumpFix = {
	.section = "MessagePumpFix",
	.title = "Умный опрос очереди сообщений Windows",
	.gain = "-6–9% кадра",
	.summary = "Не дёргает ядро Windows (PeekMessageA + хуки DINPUT8/драйвера) на каждом кадре, если нет нажатий клавиш.",
	.details = "Главный цикл GTA SA вызывает PeekMessageA на каждом кадре (700-1000 раз/с). На высоком FPS один только "
	           "переход в ядро (win32u!NtUserPeekMessage) и цепочка системных хуков DirectInput/NVIDIA внутри него съедают "
	           "до 10-11% всего главного потока.\n\n"
	           "Перед входом в ядро проверяется GetInputState() (5 тактов в usermode без системного вызова): любое нажатие "
	           "клавиши или кнопки мыши будит очередь мгновенно на этом же кадре (0 мс задержки). Прицел мышью идёт через "
	           "DirectInput и вообще не зависит от PeekMessage. В остальное время очередь проверяется раз в interval_ms "
	           "(по умолчанию 3 мс = 333 Гц).",
	.load = Load,
	.gta = true,
	.atStart = Apply,
	.report = Report,
	.params = kParams,
	.paramCount = Count(kParams),
};
