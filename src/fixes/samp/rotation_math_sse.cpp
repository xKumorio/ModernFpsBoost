#include "../registry.h"
#include "rotation_math_sse.h"
#include <atomic>

using Rotation = void (__thiscall *)(BYTE *, float, float, float);
static Rotation g_original;
static std::atomic<DWORD> g_fast, g_fallback;

static void __fastcall Hook(BYTE *object, void *, float x, float y, float z)
{
    if (GetCurrentThreadId() != g_mainThread) {
        g_original(object, x, y, z);
        return;
    }
    if (RotationMathSSE::Try(object, x, y, z)) {
        g_fast.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_fallback.fetch_add(1, std::memory_order_relaxed);
    g_original(object, x, y, z);
}

static bool OriginalBody(BYTE *samp)
{
    BYTE *body = samp + 0xA2D60;
    const int offsets[] = {10, 30, 60};
    for (int offset : offsets)
        if (*(DWORD *)(body + offset) != (DWORD)(samp + 0xDA128)) return false;
    if (*(DWORD *)(samp + 0xDA128) != 0x3C8EFA35) return false;
    uint32_t hash = 2166136261u;
    for (int i = 0; i < 255; i++) {
        if ((i >= 10 && i < 14) || (i >= 30 && i < 34) || (i >= 60 && i < 64)) continue;
        hash = (hash ^ body[i]) * 16777619u;
    }
    return hash == 0xC208ED08;
}

static bool Apply(BYTE *samp)
{
    if (g_sampVersion != SAMP_R1) {
        Log("SampRotationSSE: only the audited SA-MP R1 routine is supported - skipped");
        return true;
    }
    BYTE *site = samp + 0xA4699;
    g_original = (Rotation)(samp + 0xA2D60);
    if (!OriginalBody(samp) || !IsCallTo(site, (DWORD)g_original)) {
        Log("SampRotationSSE: rotation routine or call site differs - skipped");
        return true;
    }
    g_fixSampRotationSSE.active = RedirectCall(site, (void *)Hook);
    if (g_fixSampRotationSSE.active)
        Log("SampRotationSSE: experimental approximate SSE rotation, bounded angles and FPU state, original retained as fallback");
    return true;
}

static void Report(std::string &out, double secs)
{
    DWORD fast = g_fast.exchange(0, std::memory_order_relaxed);
    DWORD fallback = g_fallback.exchange(0, std::memory_order_relaxed);
    if (fast + fallback)
        Appendf(out, "SampRotationSSE: %.0f rotations/s SSE, %.0f/s original\n", fast / secs, fallback / secs);
}

Fix g_fixSampRotationSSE = {
    .section = "SampRotationSSE",
    .title = "Математика поворота объектов",
    .gain = "эксперимент",
    .summary = "Поворот объекта SA-MP R1: три пары sin/cos считаются на SSE с небольшой погрешностью.",
    .details = "При обработке поворота объекта SA-MP выполняет три fsin и три fcos. SSE считает три угла параллельно. "
               "Это приближение: на стенде ошибка элементов матрицы меньше 0.000001, побитового равенства нет. "
               "Углы вне [-360, 360], неподдерживаемый режим FPU и вызовы из других потоков идут в оригинал. "
               "Прирост FPS и совместимость в игре ещё проверяются.",
    .defaultOn = true,
    .atSamp = Apply,
    .report = Report,
};
