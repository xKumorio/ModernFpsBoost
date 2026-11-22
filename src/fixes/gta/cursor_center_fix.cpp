// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [CursorCenterFix] RsMouseSetPos (0x7453F0) calls SetCursorPos(centerX, centerY) every single frame
// to keep the hidden Windows cursor inside the window, even though GTA SA reads mouse movement via
// DirectInput. At 500-1000 FPS that means 500-1000 win32u!NtUserSetCursorPos kernel syscalls per
// second (~0.9% of the main thread). Re-centering to the exact same coordinates at most once every
// interval_ms (default 8 ms = 125 Hz) keeps the cursor centered while eliminating ~80-90% of syscalls.
// Calls with changed coordinates always execute immediately.

#include "../registry.h"

static const DWORD kIatSetCursorPos = 0x858300;
static BOOL(WINAPI *oSetCursorPos)(int, int);

static int g_intervalMs = 8;
static int g_lastX = -100000, g_lastY = -100000;
static DWORD g_lastAt;
static unsigned g_calls, g_skipped;

static BOOL WINAPI hkSetCursorPos(int x, int y)
{
	if (GetCurrentThreadId() != g_mainThread || g_intervalMs <= 0)
		return oSetCursorPos(x, y);
	g_calls++;
	const DWORD now = GetTickCount();
	if (x == g_lastX && y == g_lastY && now - g_lastAt < (DWORD)g_intervalMs) {
		g_skipped++;
		return TRUE;
	}
	g_lastX = x;
	g_lastY = y;
	g_lastAt = now;
	return oSetCursorPos(x, y);
}

static void Load(const Ini &ini)
{
	g_intervalMs = ini.Int("CursorCenterFix", "interval_ms", 8);
	if (g_intervalMs < 0) g_intervalMs = 0;
	if (g_intervalMs > 32) g_intervalMs = 32;
}

static void Apply()
{
	void *orig = *(void **)kIatSetCursorPos;
	if (!orig) return;
	oSetCursorPos = (BOOL(WINAPI *)(int, int))orig;
	void *hook = (void *)hkSetCursorPos;
	g_fixCursorCenterFix.active = PatchBytes(kIatSetCursorPos, &orig, &hook, 4);
	Log(g_fixCursorCenterFix.active ? "CursorCenterFix: SetCursorPos throttled to %d ms when unchanged" : "CursorCenterFix: IAT patch failed", g_intervalMs);
}

static void Report(std::string &out, double secs)
{
	if (g_calls)
		Appendf(out, "CursorCenterFix: пропущено %.0f%% из %.0f SetCursorPos/с\n", 100.0 * g_skipped / g_calls, g_calls / secs);
	g_calls = g_skipped = 0;
}

static const Param kParams[] = {
	{ "interval_ms", "Интервал, мс", &g_intervalMs, nullptr, 0, 32, true },
};

Fix g_fixCursorCenterFix = {
	.section = "CursorCenterFix",
	.title = "Реже центрировать курсор",
	.gain = "-0.9% кадра",
	.summary = "Не дёргает системный вызов SetCursorPos по 600 раз в секунду в одну и ту же точку.",
	.details = "GTA SA читает мышь через DirectInput, но всё равно каждый кадр вызывает SetCursorPos в центр экрана, "
	           "чтобы скрытый системный курсор не улетал за край окна. На высоком FPS это сотни переходов в ядро Windows "
	           "(win32u!NtUserSetCursorPos) в секунду — около 0.9% кадра.\n\n"
	           "Повторные вызовы в те же координаты делаются не чаще раза в interval_ms (по умолчанию 8 мс = 125 Гц). "
	           "На сенсу, прицел и инпутлаг никак не влияет.",
	.load = Load,
	.gta = true,
	.atStart = Apply,
	.report = Report,
	.params = kParams,
	.paramCount = Count(kParams),
};
