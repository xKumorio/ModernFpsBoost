// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [SpikeProfiler] What makes a frame late. Average FPS says nothing about freezes and input lag: those
// are single long frames. Every frame (from the end of one Present to the end of the next) is split by
// two clocks read at Present's entry and exit, the wall clock and the main thread's own cycle counter
// (QueryThreadCycleTime: it only runs while the thread does):
//   cpu     - the thread was running the game's and the plugins' code,
//   wait    - it was not running outside Present: disk, a lock, Sleep, another thread on its core,
//   present - inside Present: the video driver / DXVK queue, vsync, a frame limiter there.
// A frame much longer than the recent normal one is a spike; it is kept with what grew in it, the
// model memory streamed in and the page faults during it, and, while the frame profiler runs, the
// functions whose own time grew the most in that frame.
// Input lag is measured from CPad::UpdatePads (the game reads keys and mouse, its call in
// CGame::Process 0x53BEE6) to the end of that frame's Present.
// Nothing is collected while the menu is open: its own drawing would be measured.

#define PSAPI_VERSION 2
#include <windows.h>
#include <intrin.h>
#include <psapi.h>
#include <algorithm>

#include "../core/game.h"
#include "../core/patch.h"
#include "../core/plugin.h"
#include "profilers.h"
#include "qpc.h"

static const DWORD kUpdatePadsSite = 0x53BEE6; // call CPad::UpdatePads in CGame::Process
static const DWORD kUpdatePads = 0x541DD0;
static const DWORD kStreamMemoryUsed = 0x8E4CB4; // CStreaming::ms_memoryUsedBytes

static const int kRing = 4096;  // frames kept for the percentiles and the graph
static const int kMaxRows = 300; // spikes kept
static const int kMaxTimers = 4096;
// a spike: this many times the normal frame and at least this much longer
static const double kSpikeRatio = 1.5, kSpikeExtraMs = 2.0;

static HANDLE g_mainHandle;
static void RefreshModules();
static DWORD WINAPI WatchThread(LPVOID);

static bool g_running;
static DWORD g_padsNext = kUpdatePads; // the frame profiler times UpdatePads through it (call chain)
static bool g_padsHooked;
static LONGLONG g_inputAt;

static float g_ms[kRing], g_input[kRing];
static int g_head, g_count;

static LONGLONG g_startQpc, g_lastEndQpc, g_beginQpc;
static ULONG64 g_lastEndCyc, g_beginCyc, g_lastEndTsc, g_beginTsc;
static DWORD g_lastStream, g_lastFaults;
static bool g_haveLast;

static double g_baseMs, g_baseCpu, g_baseWait, g_basePresent; // the normal frame: eased over frames that are not spikes
static double g_sumMs, g_sumCpu, g_sumWait, g_sumPresent;
static int g_frames, g_spikes;
static float g_worst;
static int g_causeCount[SPIKE_CAUSES];
static float g_causeMs[SPIKE_CAUSES];

static std::vector<SpikeRow> g_rows;
static SpikeSummary g_summary;

static LONGLONG g_tPrev[kMaxTimers], g_tNow[kMaxTimers];
static float g_tBase[kMaxTimers];
static int g_tN;
static bool g_tHave;

static void __cdecl hkUpdatePads()
{
	if (g_running) g_inputAt = Qpc();
	((void(__cdecl *)())g_padsNext)();
}

void SpikeProfilerInit()
{
	if (!IsGta10US()) return;
	BYTE *site = (BYTE *)kUpdatePadsSite;
	if (!IsCallTo(site, kUpdatePads) || !RedirectCall(site, (void *)hkUpdatePads)) {
		Log("SpikeProfiler: the UpdatePads call is not original - input lag is not measured");
		return;
	}
	AddCallChain(site, &g_padsNext);
	g_padsHooked = true;
}

bool SpikeProfilerRunning() { return g_running; }

void SpikeProfilerReset()
{
	g_head = g_count = 0;
	g_haveLast = g_tHave = false;
	g_baseMs = g_baseCpu = g_baseWait = g_basePresent = 0;
	g_sumMs = g_sumCpu = g_sumWait = g_sumPresent = 0;
	g_frames = g_spikes = 0;
	g_worst = 0;
	memset(g_causeCount, 0, sizeof(g_causeCount));
	memset(g_causeMs, 0, sizeof(g_causeMs));
	g_rows.clear();
	g_summary = {};
	g_startQpc = Qpc();
	g_inputAt = 0;
}

void SpikeProfilerStart()
{
	if (g_running) return;
	SpikeProfilerReset();
	static bool watching;
	if (!watching) {
		watching = true;
		RefreshModules();
		g_mainHandle = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, g_mainThread);
		if (g_mainHandle) CloseHandle(CreateThread(nullptr, 0, WatchThread, nullptr, 0, nullptr));
		else Log("SpikeProfiler: the main thread cannot be opened (%lu) - no stacks of long frames", GetLastError());
	}
	g_running = true;
	Log("SpikeProfiler: started");
}

void SpikeProfilerStop() { g_running = false; }

static DWORD PageFaults()
{
	PROCESS_MEMORY_COUNTERS pmc = {};
	pmc.cb = sizeof(pmc);
	return GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)) ? pmc.PageFaultCount : 0;
}

void SpikePresentBegin()
{
	if (!g_running) return;
	g_beginQpc = Qpc();
	g_beginTsc = __rdtsc();
	QueryThreadCycleTime(GetCurrentThread(), &g_beginCyc);
}

// the functions whose own time in this frame is the furthest above their normal one
static void TopTimers(std::string &out, bool spike)
{
	if (!SampProfilerRunning()) {
		g_tHave = false;
		return;
	}
	int n = SampProfilerSelfTotals(g_tNow, kMaxTimers);
	if (!g_tHave || n != g_tN) {
		memcpy(g_tPrev, g_tNow, sizeof(LONGLONG) * n);
		if (n != g_tN) memset(g_tBase, 0, sizeof(g_tBase));
		g_tN = n;
		g_tHave = true;
		return;
	}
	const float toMs = float(1000.0 / QpcFreq());
	int best[3] = { -1, -1, -1 };
	float bestMs[3] = { 0.2f, 0.2f, 0.2f }, bestOwn[3] = {}; // growth under 0.2 ms is not a cause
	for (int i = 0; i < n; i++) {
		float d = float(g_tNow[i] - g_tPrev[i]) * toMs;
		g_tPrev[i] = g_tNow[i];
		if (!spike) {
			g_tBase[i] += (d - g_tBase[i]) * (1.0f / 64);
			continue;
		}
		float extra = d - g_tBase[i];
		for (int k = 0; k < 3; k++)
			if (extra > bestMs[k]) {
				for (int j = 2; j > k; j--) best[j] = best[j - 1], bestMs[j] = bestMs[j - 1], bestOwn[j] = bestOwn[j - 1];
				best[k] = i, bestMs[k] = extra, bestOwn[k] = d;
				break;
			}
	}
	for (int k = 0; k < 3 && best[k] >= 0; k++) {
		char buf[160];
		_snprintf_s(buf, _TRUNCATE, "%s%s %.1f мс", k ? ", " : "", SampProfilerTimerLabel(best[k]), bestOwn[k]);
		out += buf;
	}
}

// ---------------------------------------------------------------- where the game is during a long frame
//
// A watch thread wakes every half millisecond; once the frame in progress has run past the spike limit
// it suspends the main thread, copies where it is (its instruction pointer and the return addresses on
// its stack) and lets it go. Nothing is allocated, logged or locked while the thread is suspended (it
// may hold the heap lock). The samples are turned into names on the main thread when the frame ends.

#include "gta_names_gen.h"

static const int kMaxSamples = 64, kDepth = 32;
struct Sample { DWORD pc[kDepth]; int n; };
static Sample g_samples[kMaxSamples];
static volatile LONG g_sampleN, g_sampleSeq = -1; // written only by the watch thread
static volatile LONG g_frameSeq;                  // the frame in progress
static volatile LONGLONG g_frameStart;            // its start, QPC
static volatile LONG g_limitTicks;                // sampled once it has run this long; 0 = not yet known
static volatile LONG g_menuOpen;

struct ModRange { DWORD base, end; };
static ModRange g_mods[2][512];
static volatile LONG g_modN[2], g_modSet;

static void RefreshModules()
{
	static HMODULE list[512];
	DWORD need = 0;
	if (!EnumProcessModules(GetCurrentProcess(), list, sizeof(list), &need)) return;
	int n = (int)min(need / sizeof(HMODULE), (DWORD)512), set = g_modSet ^ 1, k = 0;
	for (int i = 0; i < n; i++) {
		MODULEINFO mi = {};
		if (GetModuleInformation(GetCurrentProcess(), list[i], &mi, sizeof(mi))) g_mods[set][k++] = { (DWORD)mi.lpBaseOfDll, (DWORD)mi.lpBaseOfDll + mi.SizeOfImage };
	}
	g_modN[set] = k;
	g_modSet = set;
}

// v is the address after a call instruction inside a loaded module
static bool IsReturnAddress(DWORD v, const ModRange *mods, int n)
{
	for (int i = 0; i < n; i++) {
		if (v < mods[i].base + 0x1008 || v >= mods[i].end) continue;
		const BYTE *b = (const BYTE *)v;
		if (b[-5] == 0xE8) return true;                               // call rel32
		for (int len = 2; len <= 7; len++)                            // call r/m32: FF /2
			if (b[-len] == 0xFF && (b[-len + 1] & 0x38) == 0x10) return true;
		return false;
	}
	return false;
}

// The game's thread is stopped only for as long as copying the top of its stack takes (microseconds): picking
// the return addresses out of the copy is done after it runs again. Looking through the stack while it was
// stopped took a millisecond or two a sample and made a long frame twice as long.
static const int kStackCopy = 1024;

static void TakeSample()
{
	static DWORD raw[kStackCopy];
	int rawN = 0;
	if (SuspendThread(g_mainHandle) == (DWORD)-1) return;
	CONTEXT ctx;
	ctx.ContextFlags = CONTEXT_CONTROL;
	LONG seq = g_frameSeq;
	bool got = GetThreadContext(g_mainHandle, &ctx) != 0;
	if (got) {
		MEMORY_BASIC_INFORMATION mbi;
		if (VirtualQuery((void *)ctx.Esp, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_GUARD)) {
			const DWORD *sp = (const DWORD *)(ctx.Esp & ~3u), *end = (const DWORD *)((BYTE *)mbi.BaseAddress + mbi.RegionSize);
			rawN = (int)min((LONG_PTR)(end - sp), (LONG_PTR)kStackCopy);
			memcpy(raw, sp, rawN * sizeof(DWORD));
		}
	}
	ResumeThread(g_mainHandle);
	if (!got) return;

	if (g_sampleSeq != seq) g_sampleSeq = seq, g_sampleN = 0;
	if (g_sampleN >= kMaxSamples) return;
	Sample &s = g_samples[g_sampleN];
	s.n = 0;
	s.pc[s.n++] = ctx.Eip;
	int set = g_modSet;
	const ModRange *mods = g_mods[set];
	int modN = g_modN[set];
	DWORD lo = 0xFFFFFFFF, hi = 0;
	for (int i = 0; i < modN; i++) lo = min(lo, mods[i].base), hi = max(hi, mods[i].end);
	for (int i = 0; i < rawN && s.n < kDepth; i++)
		if (raw[i] >= lo && raw[i] < hi && IsReturnAddress(raw[i], mods, modN)) s.pc[s.n++] = raw[i];
	g_sampleN++;
}

static DWORD WINAPI WatchThread(LPVOID)
{
	// a high-resolution timer does not change the system timer period the game's own Sleep calls see
	HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, 0x2 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */, TIMER_ALL_ACCESS);
	for (;;) {
		if (!g_running || g_menuOpen || !g_limitTicks) {
			Sleep(50);
			continue;
		}
		// asleep until the frame has run as long as a freeze would, then a sample every 0.5 ms: a frame of the
		// usual length costs one wake-up, not a dozen
		LONGLONG begun = InterlockedCompareExchange64(&g_frameStart, 0, 0);
		LONGLONG left = begun ? (begun + g_limitTicks - Qpc()) * 10000000 / (LONGLONG)QpcFreq() : 0;
		LARGE_INTEGER due;
		due.QuadPart = -(left > 5000 ? left : 5000);
		static LONGLONG modsAt;
		if (!modsAt || double(Qpc() - modsAt) / QpcFreq() > 5.0) {
			modsAt = Qpc();
			RefreshModules();
		}
		if (timer && SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(timer, 100);
		else Sleep(1);
		LONGLONG start = InterlockedCompareExchange64(&g_frameStart, 0, 0);
		if (start && Qpc() - start > g_limitTicks) TakeSample();
	}
}

static const char *BaseName(const char *path)
{
	const char *s = strrchr(path, '\\');
	return s ? s + 1 : path;
}

// "CStreaming::LoadRequestedModels+0x41", "samp.dll CObjectPool::Process+0x10", "d3d9.dll+0x12345"
static void NameOf(DWORD pc, char *out, size_t size)
{
	HMODULE mod = nullptr;
	char path[MAX_PATH] = "";
	if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)pc, &mod) || !mod) {
		_snprintf_s(out, size, _TRUNCATE, "%08lX", pc);
		return;
	}
	GetModuleFileNameA(mod, path, MAX_PATH);
	const char *name = BaseName(path);
	if (mod == GetModuleHandleA(nullptr) && IsGta10US()) {
		int lo = 0, hi = sizeof(kGtaNames) / sizeof(kGtaNames[0]) - 1, at = -1;
		while (lo <= hi) {
			int mid = (lo + hi) / 2;
			if (kGtaNames[mid].addr <= pc) at = mid, lo = mid + 1;
			else hi = mid - 1;
		}
		// a named function far below is most likely another, unnamed one
		if (at >= 0 && pc - kGtaNames[at].addr < 0x1000) _snprintf_s(out, size, _TRUNCATE, "%s+0x%lX", kGtaNames[at].name, pc - kGtaNames[at].addr);
		else _snprintf_s(out, size, _TRUNCATE, "gta_sa %06lX", pc);
		return;
	}
	DWORD off = 0;
	if (const char *label = SampProfilerNameOf(pc, &off)) {
		_snprintf_s(out, size, _TRUNCATE, "samp %s+0x%lX", label, off);
		return;
	}
	_snprintf_s(out, size, _TRUNCATE, "%s+0x%lX", name, pc - (DWORD)mod);
}

// The samples of the frame that just ended, as lines "N of M: where <- called from <- ...", the most
// frequent place first. Same place = same instruction pointer function and same first caller.
// "gdi32full.dll+0x55D03" -> the length of "gdi32full.dll" when it is a module of Windows or of the video
// driver, whose inner frames say nothing: a run of them is shown as its first one, so that the line gets
// as far as the code that called into it
static size_t SystemModuleLen(const char *part)
{
	static const char *const mods[] = { "ntdll.dll", "KERNELBASE.dll", "KERNEL32.DLL", "USER32.dll", "win32u.dll", "GDI32.dll", "gdi32full.dll",
		                                "TextShaping.dll", "ucrtbase.dll", "mswsock.dll", "WS2_32.dll", "MSCTF.dll", "d3d9.dll", "d3dx9_25.dll",
		                                "nvoglv32.dll", "nvgpucomp32.dll", "VCRUNTIME140.dll", "windows.storage.dll", "apphelp.dll", "AUDIOSES.DLL" };
	for (const char *m : mods) {
		size_t len = strlen(m);
		if (!_strnicmp(part, m, len) && part[len] == '+') return len;
	}
	return 0;
}

static void DescribeSamples(std::string &out)
{
	if (g_sampleSeq != g_frameSeq) return;
	int n = g_sampleN;
	if (n <= 0) return;
	if (n > kMaxSamples) n = kMaxSamples;
	static char lines[kMaxSamples][420];
	static char keys[kMaxSamples][200];
	static int count[kMaxSamples];
	int kinds = 0;
	for (int i = 0; i < n; i++) {
		const Sample &s = g_samples[i];
		char line[420] = "", key[200] = "", part[128];
		char lastSystem[40] = "";
		int shownFrames = 0;
		for (int k = 0; k < s.n && shownFrames < 7; k++) {
			NameOf(s.pc[k], part, sizeof(part));
			size_t sys = SystemModuleLen(part);
			if (sys && k && !_strnicmp(lastSystem, part, sys) && lastSystem[sys] == 0) continue;
			strncpy_s(lastSystem, part, sys < sizeof(lastSystem) ? sys : 0);
			shownFrames++;
			// the place is told apart by function, not by the exact instruction
			if (shownFrames <= 2) {
				char fn[128];
				strcpy_s(fn, part);
				if (char *plus = strrchr(fn, '+')) *plus = 0;
				strcat_s(key, fn);
				strcat_s(key, "|");
			}
			if (strlen(line) + strlen(part) + 6 >= sizeof(line)) break;
			if (shownFrames > 1) strcat_s(line, " <- ");
			strcat_s(line, part);
		}
		int j = 0;
		for (; j < kinds; j++)
			if (!strcmp(keys[j], key)) break;
		if (j == kinds) {
			strcpy_s(keys[j], key);
			strcpy_s(lines[j], line);
			count[j] = 0;
			kinds++;
		}
		count[j]++;
	}
	for (int shown = 0; shown < 4; shown++) {
		int best = -1;
		for (int j = 0; j < kinds; j++)
			if (count[j] > 0 && (best < 0 || count[j] > count[best])) best = j;
		if (best < 0) break;
		char head[48];
		sprintf_s(head, "%s%d из %d: ", shown ? "\n" : "", count[best], n);
		out += head;
		out += lines[best];
		count[best] = 0;
	}
}

void SpikePresentEnd(bool menuOpen)
{
	if (!g_running) return;
	LONGLONG endQpc = Qpc();
	ULONG64 endTsc = __rdtsc(), endCyc = 0;
	QueryThreadCycleTime(GetCurrentThread(), &endCyc);
	DWORD stream = IsGta10US() ? *(DWORD *)kStreamMemoryUsed : 0;
	DWORD faults = PageFaults();
	bool usable = g_haveLast && !menuOpen && g_beginQpc > g_lastEndQpc;
	if (usable) {
		const double toMs = 1000.0 / QpcFreq();
		double ms = double(endQpc - g_lastEndQpc) * toMs;
		double gameMs = double(g_beginQpc - g_lastEndQpc) * toMs, presentMs = double(endQpc - g_beginQpc) * toMs;
		// the share of the wall time the thread was running: its cycles against the processor's
		double gameTsc = double(g_beginTsc - g_lastEndTsc);
		double busy = gameTsc > 0 ? double(g_beginCyc - g_lastEndCyc) / gameTsc : 1.0;
		if (busy > 1.0) busy = 1.0;
		double cpuMs = gameMs * busy, waitMs = gameMs - cpuMs;

		g_ms[g_head] = (float)ms;
		g_input[g_head] = g_inputAt > g_lastEndQpc ? float(double(endQpc - g_inputAt) * toMs) : 0.0f;
		g_head = (g_head + 1) % kRing;
		if (g_count < kRing) g_count++;
		g_frames++;
		g_sumMs += ms, g_sumCpu += cpuMs, g_sumWait += waitMs, g_sumPresent += presentMs;
		if (ms > g_worst) g_worst = (float)ms;

		bool warm = g_frames > 120;
		bool spike = warm && ms > g_baseMs * kSpikeRatio && ms - g_baseMs > kSpikeExtraMs;
		std::string top;
		TopTimers(top, spike);
		if (spike) {
			std::string stack;
			DescribeSamples(stack);
			double extra[SPIKE_CAUSES] = { cpuMs - g_baseCpu, waitMs - g_baseWait, presentMs - g_basePresent };
			int cause = 0;
			for (int k = 1; k < SPIKE_CAUSES; k++)
				if (extra[k] > extra[cause]) cause = k;
			g_spikes++;
			g_causeCount[cause]++;
			g_causeMs[cause] += float(ms - g_baseMs);
			SpikeRow r;
			r.at = double(endQpc - g_startQpc) / QpcFreq();
			r.ms = (float)ms, r.normal = (float)g_baseMs;
			r.cpu = (float)cpuMs, r.wait = (float)waitMs, r.present = (float)presentMs;
			r.streamKb = int(stream - g_lastStream) / 1024;
			r.faults = faults - g_lastFaults;
			r.cause = cause;
			r.top = top;
			r.stack = stack;
			if ((int)g_rows.size() >= kMaxRows) g_rows.erase(g_rows.begin());
			g_rows.push_back(r);
		} else {
			double a = g_frames < 64 ? 1.0 / g_frames : 1.0 / 64;
			g_baseMs += (ms - g_baseMs) * a;
			g_baseCpu += (cpuMs - g_baseCpu) * a;
			g_baseWait += (waitMs - g_baseWait) * a;
			g_basePresent += (presentMs - g_basePresent) * a;
		}
	} else {
		g_tHave = false; // the timers' frame deltas would span the menu
	}
	g_lastEndQpc = endQpc, g_lastEndCyc = endCyc, g_lastEndTsc = endTsc;
	g_lastStream = stream, g_lastFaults = faults;
	g_haveLast = !menuOpen;

	// the next frame for the watch thread: it is sampled once it has run as long as a spike would
	g_menuOpen = menuOpen;
	InterlockedIncrement(&g_frameSeq);
	InterlockedExchange64(&g_frameStart, endQpc);
	g_limitTicks = g_frames > 120 ? LONG(max(g_baseMs * kSpikeRatio, g_baseMs + kSpikeExtraMs) / 1000.0 * QpcFreq()) : 0;
}

// the average of the worst share of the frames, as "1% low" counters do
static float WorstAvg(const float *sorted, int n, double share)
{
	int k = int(n * share);
	if (k < 1) k = 1;
	double sum = 0;
	for (int i = 0; i < k; i++) sum += sorted[n - 1 - i];
	return float(sum / k);
}

const SpikeSummary &SpikeProfilerSummary()
{
	static LONGLONG at;
	LONGLONG now = Qpc();
	if (at && double(now - at) / QpcFreq() < 0.5) return g_summary;
	at = now;
	SpikeSummary &s = g_summary;
	s = {};
	s.seconds = g_sumMs / 1000.0;
	s.frames = g_frames;
	s.spikes = g_spikes;
	s.worstMs = g_worst;
	s.inputMeasured = g_padsHooked;
	if (!g_count) return s;
	static float sorted[kRing];
	memcpy(sorted, g_ms, sizeof(float) * g_count);
	std::sort(sorted, sorted + g_count);
	s.avgMs = float(g_sumMs / g_frames);
	s.low1Ms = WorstAvg(sorted, g_count, 0.01);
	s.low01Ms = WorstAvg(sorted, g_count, 0.001);
	int n = 0;
	double sum = 0;
	for (int i = 0; i < g_count; i++)
		if (g_input[i] > 0) sorted[n++] = g_input[i], sum += g_input[i];
	if (n) {
		std::sort(sorted, sorted + n);
		s.inputMs = float(sum / n);
		s.inputWorstMs = WorstAvg(sorted, n, 0.01);
	}
	s.cpuShare = float(g_sumCpu / g_sumMs), s.waitShare = float(g_sumWait / g_sumMs), s.presentShare = float(g_sumPresent / g_sumMs);
	for (int k = 0; k < SPIKE_CAUSES; k++) s.causeCount[k] = g_causeCount[k], s.causeMs[k] = g_causeMs[k];
	return s;
}

const std::vector<SpikeRow> &SpikeProfilerRows() { return g_rows; }

int SpikeProfilerFrames(float *out, int max)
{
	int n = g_count < max ? g_count : max;
	for (int i = 0; i < n; i++) out[i] = g_ms[(g_head - n + i + kRing) % kRing];
	return n;
}

const char *SpikeCauseName(int cause)
{
	static const char *const names[SPIKE_CAUSES] = { "код игры и плагинов", "ожидание: диск, блокировка, Sleep", "Present: видеокарта, DXVK, лимит FPS" };
	return cause >= 0 && cause < SPIKE_CAUSES ? names[cause] : "";
}

std::string SpikeProfilerText()
{
	const SpikeSummary &s = SpikeProfilerSummary();
	std::string out;
	char buf[1024];
	auto fps = [](float ms) { return ms > 0 ? 1000.0f / ms : 0.0f; };
	sprintf_s(buf, "Замер %.0f с, %d кадров. Средний %.2f мс (%.0f FPS), 1%% low %.2f мс (%.0f FPS), 0.1%% low %.2f мс (%.0f FPS), худший %.1f мс\n",
	          s.seconds, s.frames, s.avgMs, fps(s.avgMs), s.low1Ms, fps(s.low1Ms), s.low01Ms, fps(s.low01Ms), s.worstMs);
	out += buf;
	sprintf_s(buf, "Кадр в среднем: код %.0f%%, ожидание %.0f%%, Present %.0f%%. Ввод -> конец Present: %.2f мс, 1%% худших %.2f мс\n",
	          s.cpuShare * 100, s.waitShare * 100, s.presentShare * 100, s.inputMs, s.inputWorstMs);
	out += buf;
	sprintf_s(buf, "Фризов %d (%.1f в минуту):\n", s.spikes, s.seconds > 0 ? s.spikes * 60.0 / s.seconds : 0.0);
	out += buf;
	for (int k = 0; k < SPIKE_CAUSES; k++) {
		sprintf_s(buf, "  %s: %d, лишних %.0f мс\n", SpikeCauseName(k), s.causeCount[k], s.causeMs[k]);
		out += buf;
	}
	out += "время\tкадр мс\tобычный\tкод\tожидание\tPresent\tмодели КБ\tстр.ошибки\tпричина\tфункции\n";
	for (const SpikeRow &r : g_rows) {
		sprintf_s(buf, "%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%d\t%lu\t%s\t", r.at, r.ms, r.normal, r.cpu, r.wait, r.present, r.streamKb, r.faults,
		          SpikeCauseName(r.cause));
		out += buf;
		out += r.top;
		out += "\n";
		if (!r.stack.empty()) {
			out += "  где стояла игра (снимки стека):\n  ";
			for (char ch : r.stack) {
				out += ch;
				if (ch == '\n') out += "  ";
			}
			out += "\n";
		}
	}
	return out;
}
