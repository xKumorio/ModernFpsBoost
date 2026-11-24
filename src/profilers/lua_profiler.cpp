// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [LuaProfiler] per-script CPU time of MoonLoader scripts.
//
// MoonLoader runs every script in its own LuaJIT state and enters Lua only through lua_pcall,
// lua_call and lua_resume (script coroutines, events, callbacks). Those three imports of
// MoonLoader.asi are redirected to timers; the outermost call on the game thread is charged to the
// script that owns the state. Scripts are told apart by the state's global_State (LuaJIT 2.1 x86:
// lua_State+8, shared by a state and its coroutines) and named from the source of the Lua function
// being entered (lua_getinfo ">S" / the suspended coroutine's frame).
// Also times CTextDrawPool::Draw (samp textdraws) into the same report.
// Lines: for one script picked in the menu, LuaJIT's own sampling profiler (luaJIT_profile_start,
// 1 ms, works in JIT-compiled code too) says which line runs; samples are kept per file:line.
// LuaJIT runs one such profiler per process, so one script at a time.
// Report: ModernFpsBoost_lua.log every interval seconds.

#include <math.h>
#include <algorithm>
#include <map>
#include <string.h>

#include "../fixes/registry.h"
#include "profilers.h"
#include "qpc.h"

struct lua_Debug51 {
	int event;
	const char *name, *namewhat, *what, *source;
	int currentline, nups, linedefined, lastlinedefined;
	char short_src[60];
	int i_ci;
};

typedef int(__cdecl *lua_pcall_t)(void *, int, int, int);
typedef void(__cdecl *lua_call_t)(void *, int, int);
typedef int(__cdecl *lua_resume_t)(void *, int);
typedef int(__cdecl *lua_getinfo_t)(void *, const char *, lua_Debug51 *);
typedef int(__cdecl *lua_getstack_t)(void *, int, lua_Debug51 *);
typedef void(__cdecl *lua_pushvalue_t)(void *, int);
typedef int(__cdecl *lua_status_t)(void *);
typedef int(__cdecl *lua_type_t)(void *, int);

static lua_pcall_t o_lua_pcall;
static lua_call_t o_lua_call;
static lua_resume_t o_lua_resume;
static lua_getinfo_t p_lua_getinfo;
static lua_getstack_t p_lua_getstack;
static lua_pushvalue_t p_lua_pushvalue;
static lua_status_t p_lua_status;
static lua_type_t p_lua_type;

typedef void(__cdecl *prof_cb_t)(void *data, void *L, int samples, int vmstate);
typedef void(__cdecl *prof_start_t)(void *L, const char *mode, prof_cb_t cb, void *data);
typedef void(__cdecl *prof_stop_t)(void *L);
typedef const char *(__cdecl *prof_dump_t)(void *L, const char *fmt, int depth, size_t *len);
static prof_start_t p_profStart;
static prof_stop_t p_profStop;
static prof_dump_t p_profDump;

static int   g_luaInterval = 10;
static int   g_luaDepth;
static LONGLONG g_luaWindowStart, g_luaFileStart;
static bool  g_luaRunning;
static std::vector<ProfRow> g_luaRows;
static double g_luaTotalPct;

struct LuaStat {
	void *G;
	char name[96];
	bool named;
	int tries;
	LONGLONG ticks, fileTicks; // this second / this file interval
	DWORD calls, fileCalls;
	struct Part { DWORD key; char label[72]; LONGLONG ticks, fileTicks; DWORD calls, fileCalls; } parts[48];
	int nparts;
};

// What a single entry into Lua ran: the entry function (pcall/call) or the coroutine's bottom frame.
struct LuaEntry { DWORD key; char label[72]; };

static LuaStat g_lua[256];
static int g_luaN;

static void LuaDescribe(lua_Debug51 &ar, LuaEntry &e)
{
	const char *tail = ar.short_src;
	for (const char *c = ar.short_src; *c; c++)
		if (*c == '\\' || *c == '/') tail = c + 1;
	e.key = (DWORD)ar.linedefined * 2654435761u;
	for (const char *c = tail; *c; c++) e.key = e.key * 31 + (BYTE)*c;
	sprintf_s(e.label, "%s:%d", tail, ar.linedefined);
}

static void LuaEntryOf(void *L, int idx, bool coroutine, LuaEntry &e)
{
	static struct { void *L; LuaEntry e; } coCache[512];
	e.key = 0;
	strcpy_s(e.label, "?");
	lua_Debug51 ar = {};
	if (coroutine && p_lua_status(L) == 1) { // suspended coroutine: its bottom frame is the thread function
		auto &c = coCache[((DWORD)L >> 4) & 511];
		if (c.L == L) { e = c.e; return; }
		int last = -1;
		for (int level = 0; level < 64 && p_lua_getstack(L, level, &ar); level++) last = level;
		if (last >= 0 && p_lua_getstack(L, last, &ar) && p_lua_getinfo(L, "S", &ar)) {
			LuaDescribe(ar, e);
			strcat_s(e.label, " (thread)");
			c.L = L;
			c.e = e;
		}
		return;
	}
	if (p_lua_type(L, idx) != 6) return;
	p_lua_pushvalue(L, idx);
	if (p_lua_getinfo(L, ">S", &ar)) LuaDescribe(ar, e);
}

static void LuaCharge(LuaStat *s, const LuaEntry &e, LONGLONG ticks)
{
	int i = 0;
	for (; i < s->nparts; i++)
		if (s->parts[i].key == e.key) break;
	if (i == s->nparts) {
		if (s->nparts == 48) {
			i = 47; // overflow bucket
		} else {
			s->nparts++;
			s->parts[i] = {};
			s->parts[i].key = e.key;
			strcpy_s(s->parts[i].label, e.label);
		}
	}
	s->parts[i].ticks += ticks;
	s->parts[i].calls++;
}

static LuaStat *LuaStatFor(void *L)
{
	if (!L) return nullptr;
	void *G = *(void **)((BYTE *)L + 8);
	for (int i = 0; i < g_luaN; i++)
		if (g_lua[i].G == G) return &g_lua[i];
	if (g_luaN >= 256) return nullptr;
	LuaStat *s = &g_lua[g_luaN++];
	memset(s, 0, sizeof(*s));
	s->G = G;
	sprintf_s(s->name, "state %p", G);
	return s;
}

static LuaStat *NamedStat(void *key, const char *name)
{
	for (int i = 0; i < g_luaN; i++)
		if (g_lua[i].G == key) return &g_lua[i];
	if (g_luaN >= 256) return nullptr;
	LuaStat *s = &g_lua[g_luaN++];
	memset(s, 0, sizeof(*s));
	s->G = key;
	strncpy_s(s->name, name, _TRUNCATE);
	s->named = true;
	return s;
}

static void LuaName(LuaStat *s, const char *source)
{
	if (!source || source[0] != '@') return;
	const char *p = source + 1;
	char low[MAX_PATH];
	strncpy_s(low, p, _TRUNCATE);
	_strlwr_s(low);
	const char *ml = strstr(low, "\\moonloader\\");
	if (!ml) ml = strstr(low, "/moonloader/");
	if (!ml) return;
	const char *rel = p + (ml - low) + 12;
	if (!_strnicmp(rel, "lib\\", 4) || !_strnicmp(rel, "lib/", 4)) {
		if (!s->named && s->tries > 400) sprintf_s(s->name, "? (seen in lib\\%s)", rel + 4);
		return;
	}
	strncpy_s(s->name, rel, _TRUNCATE);
	s->named = true;
}

// name from the function about to run (at stack index idx) or from a suspended coroutine's frame
static void LuaLearnName(LuaStat *s, void *L, int idx, bool coroutine)
{
	if (!s || s->named || s->tries > 2000) return;
	s->tries++;
	lua_Debug51 ar = {};
	if (coroutine && p_lua_status(L) == 1) { // LUA_YIELD: look at the frames of the suspended coroutine
		for (int level = 0; level < 8 && p_lua_getstack(L, level, &ar); level++) {
			if (p_lua_getinfo(L, "S", &ar)) LuaName(s, ar.source);
			if (s->named) return;
		}
		return;
	}
	if (p_lua_type(L, idx) != 6) // LUA_TFUNCTION; getinfo(">S") on anything else reads garbage
		return;
	p_lua_pushvalue(L, idx);
	if (p_lua_getinfo(L, ">S", &ar)) LuaName(s, ar.source);
}

// ---------------------------------------------------------------- lines of one script

struct LineAcc { unsigned hash; char key[232]; double samples; };
static LineAcc g_la[4096];
static int g_laN;
static double g_laTotal;
static char g_lwName[96];
static void *g_lwWantG, *g_lwActiveG; // the script asked for / the VM the profiler runs on
static DWORD g_lwActiveSeen;          // last entry into the active VM (ms); lua_close stops LuaJIT's profiler itself
static LONGLONG g_lwStart, g_lwScript, g_lwInside, g_lwEnter; // QPC: start, time in the script, inside since the last sample
static bool g_lwIn;
static std::vector<LineRow> g_lineRows;
static double g_lineSamples, g_lineSecs;

static void LineAdd(const char *key, size_t n, double samples)
{
	n = min(n, sizeof(g_la[0].key) - 1);
	unsigned h = 2166136261u;
	for (size_t k = 0; k < n; k++) h = (h ^ (BYTE)key[k]) * 16777619u;
	for (unsigned i = h & 4095;; i = (i + 1) & 4095) {
		LineAcc &a = g_la[i];
		if (!a.key[0]) {
			if (g_laN > 3500) return; // full: rare lines are dropped
			a.hash = h;
			memcpy(a.key, key, n);
			a.key[n] = 0;
			g_laN++;
		} else if (a.hash != h || strncmp(a.key, key, n) || a.key[n]) {
			continue;
		}
		a.samples += samples;
		g_laTotal += samples;
		return;
	}
}

// Runs on the game thread inside the VM. LuaJIT counts timer ticks since its last call, ticks while
// the script did not run at all included (they would land on the line it resumes at): the credit
// is capped by the time the game thread really spent inside the script since then.
static void __cdecl LineSample(void *, void *L, int samples, int)
{
	LONGLONG now = Qpc();
	double inside = double(g_lwInside + (g_lwIn ? now - g_lwEnter : 0));
	g_lwInside = 0;
	if (g_lwIn) g_lwEnter = now;
	double credit = min((double)samples, max(1.0, floor(inside * 1000.0 / QpcFreq() + 0.5)));
	size_t len = 0;
	const char *st = p_profDump(L, "pl|", 4, &len); // innermost first: "path:line|path:line|"
	for (size_t a = 0; st && a < len;) {
		size_t b = a;
		while (b < len && st[b] != '|') b++;
		size_t c = b; // a Lua frame ends in :digits; C functions and builtins do not
		while (c > a && st[c - 1] >= '0' && st[c - 1] <= '9') c--;
		if (c < b && c > a + 1 && st[c - 1] == ':' && st[a] != '[') {
			if (st[a] == '@') a++;
			LineAdd(st + a, b - a, credit);
			return;
		}
		a = b + 1;
	}
}

static void LinesClear()
{
	memset(g_la, 0, sizeof(g_la));
	g_laN = 0;
	g_laTotal = 0;
	g_lwScript = g_lwInside = 0;
	g_lwStart = Qpc();
}

// game thread, at an entry into Lua of s (before it runs): start / stop LuaJIT's profiler
static void LineSync(void *L, LuaStat *s)
{
	if (!s || !p_profStart) return;
	if (g_lwActiveG && g_lwActiveG != g_lwWantG && s->G == g_lwActiveG) {
		p_profStop(L);
		g_lwActiveG = nullptr;
	}
	if (g_lwActiveG && TimeMs() - g_lwActiveSeen > 3000) g_lwActiveG = nullptr; // closed (or idle): free for another
	if (!g_lwActiveG && g_lwWantG && s->G == g_lwWantG) {
		LinesClear();
		p_profStart(L, "li1", LineSample, nullptr);
		g_lwActiveG = g_lwWantG;
	}
	if (s->G == g_lwActiveG) g_lwActiveSeen = TimeMs();
}

static void LinesEnter(LuaStat *s, LONGLONG t0)
{
	if (s && s->G == g_lwActiveG) {
		g_lwIn = true;
		g_lwEnter = t0;
	}
}

static void LinesLeave(LuaStat *s, LONGLONG t0, LONGLONG t1)
{
	if (s && s->G == g_lwActiveG && g_lwIn) {
		g_lwInside += t1 - g_lwEnter;
		g_lwScript += t1 - t0;
		g_lwIn = false;
	}
}

static void LinesReport()
{
	if (!g_lwWantG) {
		g_lineRows.clear();
		return;
	}
	for (int i = g_luaN - 1; i >= 0; i--) // a reloaded script is a new VM with the same name: follow it
		if (g_lua[i].G != (void *)1 && !strcmp(g_lua[i].name, g_lwName)) {
			g_lwWantG = g_lua[i].G;
			break;
		}
	const double q = QpcFreq();
	g_lineSecs = double(Qpc() - g_lwStart) / q;
	g_lineSamples = g_laTotal;
	double scriptPct = g_lineSecs > 0 ? 100 * double(g_lwScript) / q / g_lineSecs : 0;
	g_lineRows.clear();
	for (const LineAcc &a : g_la) {
		if (!a.key[0] || g_laTotal <= 0) continue;
		const char *colon = strrchr(a.key, ':');
		if (!colon) continue;
		LineRow r;
		r.path.assign(a.key, colon);
		r.line = atoi(colon + 1);
		size_t slash = r.path.find_last_of("\\/");
		r.file = slash == std::string::npos ? r.path : r.path.substr(slash + 1);
		r.samples = a.samples;
		r.share = a.samples / g_laTotal;
		r.pct = scriptPct * r.share;
		g_lineRows.push_back(std::move(r));
	}
	std::sort(g_lineRows.begin(), g_lineRows.end(), [](const LineRow &a, const LineRow &b) { return a.samples > b.samples; });
	if (g_lineRows.size() > 300) g_lineRows.resize(300);
}

void LuaLinesWatch(const char *script)
{
	g_lwWantG = nullptr;
	g_lwName[0] = 0;
	g_lineRows.clear();
	g_lineSamples = g_lineSecs = 0;
	if (!script) return;
	for (int i = g_luaN - 1; i >= 0; i--)
		if (g_lua[i].G != (void *)1 && !strcmp(g_lua[i].name, script)) {
			strcpy_s(g_lwName, g_lua[i].name);
			g_lwWantG = g_lua[i].G;
			break;
		}
}

const char *LuaLinesScript() { return g_lwWantG ? g_lwName : nullptr; }

const std::vector<LineRow> &LuaLinesRows(double &samples, double &seconds)
{
	samples = g_lineSamples;
	seconds = g_lineSecs;
	return g_lineRows;
}

void LuaLinesReset()
{
	if (g_lwActiveG) LinesClear();
	g_lineRows.clear();
	g_lineSamples = g_lineSecs = 0;
}

const std::vector<std::string> &LuaSource(const std::string &path)
{
	static std::map<std::string, std::vector<std::string>> cache;
	auto it = cache.find(path);
	if (it != cache.end()) return it->second;
	std::vector<std::string> &lines = cache[path];
	std::string full = path;
	if (full.size() < 2 || full[1] != ':') full = std::string(g_dir) + "\\" + path; // relative to the game folder
	FILE *f = nullptr;
	if (fopen_s(&f, full.c_str(), "rb") || !f) return lines;
	std::string text;
	char buf[8192];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
	fclose(f);
	if (text.size() >= 3 && (BYTE)text[0] == 0xEF && (BYTE)text[1] == 0xBB && (BYTE)text[2] == 0xBF) text.erase(0, 3);
	bool utf8 = text.empty() || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), (int)text.size(), nullptr, 0) > 0;
	for (size_t a = 0; a <= text.size();) {
		size_t b = text.find('\n', a);
		if (b == std::string::npos) b = text.size();
		std::string out;
		for (size_t k = a; k < b; k++) {
			if (text[k] == '\r') continue;
			if (text[k] == '\t') out += "    ";
			else out += text[k];
		}
		if (!utf8 && !out.empty()) { // scripts saved in cp1251
			std::wstring w(out.size(), L'\0');
			int k = MultiByteToWideChar(1251, 0, out.data(), (int)out.size(), &w[0], (int)w.size());
			std::string u(k * 3, '\0');
			int m = WideCharToMultiByte(CP_UTF8, 0, w.data(), k, &u[0], (int)u.size(), nullptr, nullptr);
			out.assign(u.data(), m);
		}
		lines.push_back(std::move(out));
		a = b + 1;
	}
	return lines;
}

// The file report covers the last interval seconds (file* counters).
static void WriteLuaFile(double secs)
{
	const double q = QpcFreq();
	char path[MAX_PATH];
	sprintf_s(path, "%s\\" PLUGIN_NAME "_lua.log", g_dir);
	FILE *f = nullptr;
	if (fopen_s(&f, path, "w") || !f) return;
	int order[256];
	for (int i = 0; i < g_luaN; i++) order[i] = i;
	std::sort(order, order + g_luaN, [](int a, int b) { return g_lua[a].fileTicks > g_lua[b].fileTicks; });
	double total = 0;
	for (int i = 0; i < g_luaN; i++) total += double(g_lua[i].fileTicks) / q;
	SYSTEMTIME t;
	GetLocalTime(&t);
	fprintf(f, "Lua scripts, last %.0f s (%02d:%02d:%02d). %% = share of the game's main thread time.\n", secs, t.wHour, t.wMinute, t.wSecond);
	fprintf(f, "All scripts together: %.2f ms per second = %.1f%%\n\n", 1000 * total / secs, 100 * total / secs);
	fprintf(f, "%7s  %9s  %9s  %s\n", "%", "ms/sec", "calls/sec", "script");
	for (int k = 0; k < g_luaN; k++) {
		LuaStat &s = g_lua[order[k]];
		double sec = double(s.fileTicks) / q;
		fprintf(f, "%6.2f%%  %9.2f  %9.0f  %s\n", 100 * sec / secs, 1000 * sec / secs, s.fileCalls / secs, s.name);
		std::sort(s.parts, s.parts + s.nparts, [](const LuaStat::Part &a, const LuaStat::Part &b) { return a.fileTicks > b.fileTicks; });
		for (int j = 0; j < s.nparts; j++) {
			double ps = double(s.parts[j].fileTicks) / q;
			if (100 * ps / secs >= 0.02)
				fprintf(f, "  %6.2f%%  %9.2f  %9.0f      %s\n", 100 * ps / secs, 1000 * ps / secs, s.parts[j].fileCalls / secs, s.parts[j].label);
			s.parts[j].fileTicks = 0;
			s.parts[j].fileCalls = 0;
		}
		s.fileTicks = 0;
		s.fileCalls = 0;
	}
	fclose(f);
}

// Every second: the menu's table from the last second; every interval seconds: the file.
static void LuaReport()
{
	LONGLONG now = Qpc();
	const double q = QpcFreq();
	double secs = double(now - g_luaWindowStart) / q;
	if (secs < 1.0) return;
	g_luaWindowStart = now;

	g_luaRows.clear();
	g_luaTotalPct = 0;
	for (int i = 0; i < g_luaN; i++) {
		LuaStat &s = g_lua[i];
		ProfRow row;
		row.name = s.name;
		row.pct = 100 * double(s.ticks) / q / secs;
		row.ms = 1000 * double(s.ticks) / q / secs;
		row.calls = s.calls / secs;
		g_luaTotalPct += row.pct;
		for (int j = 0; j < s.nparts; j++) {
			LuaStat::Part &part = s.parts[j];
			double pct = 100 * double(part.ticks) / q / secs;
			if (pct >= 0.01) row.parts.push_back({ part.label, pct, 1000 * double(part.ticks) / q / secs, part.calls / secs });
			part.fileTicks += part.ticks;
			part.fileCalls += part.calls;
			part.ticks = 0;
			part.calls = 0;
		}
		std::sort(row.parts.begin(), row.parts.end(), [](const ProfRow &a, const ProfRow &b) { return a.pct > b.pct; });
		g_luaRows.push_back(std::move(row));
		s.fileTicks += s.ticks;
		s.fileCalls += s.calls;
		s.ticks = 0;
		s.calls = 0;
	}
	std::sort(g_luaRows.begin(), g_luaRows.end(), [](const ProfRow &a, const ProfRow &b) { return a.pct > b.pct; });
	LinesReport();

	double fileSecs = double(now - g_luaFileStart) / q;
	if (fileSecs >= g_luaInterval) {
		g_luaFileStart = now;
		WriteLuaFile(fileSecs);
	}
}

static void LuaLeave(LuaStat *s, LONGLONG t0, const LuaEntry *e = nullptr)
{
	LONGLONG t1 = Qpc();
	LinesLeave(s, t0, t1);
	if (s) {
		s->ticks += t1 - t0;
		s->calls++;
		if (e) LuaCharge(s, *e, t1 - t0);
	}
	LuaReport();
}

static int __cdecl hk_lua_pcall(void *L, int nargs, int nresults, int errfunc)
{
	if (GetCurrentThreadId() != g_mainThread || g_luaDepth)
		return o_lua_pcall(L, nargs, nresults, errfunc);
	LuaStat *s = LuaStatFor(L);
	LuaLearnName(s, L, -(nargs + 1), false);
	LuaEntry e;
	LuaEntryOf(L, -(nargs + 1), false, e);
	LineSync(L, s);
	LONGLONG t0 = Qpc();
	LinesEnter(s, t0);
	g_luaDepth++;
	int r = o_lua_pcall(L, nargs, nresults, errfunc);
	g_luaDepth--;
	LuaLeave(s, t0, &e);
	return r;
}

static int __cdecl hk_lua_resume(void *L, int narg)
{
	if (GetCurrentThreadId() != g_mainThread || g_luaDepth)
		return o_lua_resume(L, narg);
	LuaStat *s = LuaStatFor(L);
	LuaLearnName(s, L, -(narg + 1), true);
	LuaEntry e;
	LuaEntryOf(L, -(narg + 1), true, e);
	LineSync(L, s);
	LONGLONG t0 = Qpc();
	LinesEnter(s, t0);
	g_luaDepth++;
	int r = o_lua_resume(L, narg);
	g_luaDepth--;
	LuaLeave(s, t0, &e);
	return r;
}

// lua_call can raise a Lua error that unwinds through this frame: keep the depth right with SEH.
static void LuaCallGuarded(void *L, int nargs, int nresults)
{
	__try {
		o_lua_call(L, nargs, nresults);
	} __finally {
		g_luaDepth--;
	}
}

static void __cdecl hk_lua_call(void *L, int nargs, int nresults)
{
	if (GetCurrentThreadId() != g_mainThread || g_luaDepth) {
		o_lua_call(L, nargs, nresults);
		return;
	}
	LuaStat *s = LuaStatFor(L);
	LuaLearnName(s, L, -(nargs + 1), false);
	LuaEntry e;
	LuaEntryOf(L, -(nargs + 1), false, e);
	LineSync(L, s);
	LONGLONG t0 = Qpc();
	LinesEnter(s, t0);
	g_luaDepth++;
	LuaCallGuarded(L, nargs, nresults);
	LuaLeave(s, t0, &e);
}

static bool g_luaPatched;

static void PatchMoonLoader()
{
	if (g_luaPatched) return;
	HMODULE ml = GetModuleHandleA("MoonLoader.asi");
	HMODULE lua = GetModuleHandleA("lua51.dll");
	if (!ml || !lua) return;
	g_luaPatched = true;
	p_lua_getinfo = (lua_getinfo_t)GetProcAddress(lua, "lua_getinfo");
	p_lua_getstack = (lua_getstack_t)GetProcAddress(lua, "lua_getstack");
	p_lua_pushvalue = (lua_pushvalue_t)GetProcAddress(lua, "lua_pushvalue");
	p_lua_status = (lua_status_t)GetProcAddress(lua, "lua_status");
	p_lua_type = (lua_type_t)GetProcAddress(lua, "lua_type");
	p_profStart = (prof_start_t)GetProcAddress(lua, "luaJIT_profile_start");
	p_profStop = (prof_stop_t)GetProcAddress(lua, "luaJIT_profile_stop");
	p_profDump = (prof_dump_t)GetProcAddress(lua, "luaJIT_profile_dumpstack");
	if (!p_profStop || !p_profDump) p_profStart = nullptr; // no lines; the rest works
	if (!p_lua_getinfo || !p_lua_getstack || !p_lua_pushvalue || !p_lua_status || !p_lua_type) {
		Log("LuaProfiler: lua51.dll exports missing - skipped");
		return;
	}
	int n = 0;
	n += PatchIat(ml, "lua51.dll", "lua_pcall", (void *)hk_lua_pcall, (void **)&o_lua_pcall);
	n += PatchIat(ml, "lua51.dll", "lua_call", (void *)hk_lua_call, (void **)&o_lua_call);
	n += PatchIat(ml, "lua51.dll", "lua_resume", (void *)hk_lua_resume, (void **)&o_lua_resume);
	g_luaRunning = n > 0;
	g_fixLuaProfiler.active = g_luaRunning;
	Log("LuaProfiler: %d/3 MoonLoader entry points timed, report every %d s in " PLUGIN_NAME "_lua.log", n, g_luaInterval);
}

static DWORD WINAPI LuaProfilerThread(LPVOID)
{
	for (int i = 0; i < 600 && !g_luaPatched; i++) { // up to 5 minutes
		Sleep(500);
		PatchMoonLoader();
	}
	return 0;
}

// ---------------------------------------------------------------- textdraw timer
// The only call of CTextDrawPool::Draw (R1 samp+0x9D929 -> samp+0x1AD40), which draws every
// textdraw through GTA's CFont each frame; it is timed into the same report as "[samp] textdraws".
// The hook calls on through g_tdPoolDraw (whatever the call went to: the function, or the frame
// profiler's timer if it got there first), registered as a call chain so the frame profiler can
// still time it when it starts later.

static DWORD g_tdPoolDraw;

static void __fastcall hkTextdrawPoolDraw(void *pool, void *)
{
	static LuaStat *s = NamedStat((void *)1, "[samp] textdraws (CTextDrawPool::Draw)");
	LONGLONG t0 = Qpc();
	((void(__thiscall *)(void *))g_tdPoolDraw)(pool);
	LuaLeave(s, t0);
}

// ---------------------------------------------------------------- CLEO / GTA script timer
// CTheScripts::Process runs every active script (CLEO .cs files and SA-MP's main.scr) through
// CRunningScript::Process at its one call 0x46A21B; each script is timed by its name (+8, char[8])
// as a "[cleo] name" row, so a script spinning on wait 0 shows next to the Lua scripts.

static DWORD g_scriptProcess;

static void __fastcall hkScriptProcess(BYTE *script, void *)
{
	static char names[64][9];
	static LuaStat *stats[64];
	static int n;
	if (GetCurrentThreadId() != g_mainThread) return ((void(__thiscall *)(void *))g_scriptProcess)(script);
	char name[9] = {};
	memcpy(name, script + 8, 8);
	LuaStat *st = nullptr;
	for (int k = 0; k < n; k++)
		if (!strcmp(names[k], name)) st = stats[k];
	if (!st && n < 64) {
		char label[48];
		sprintf_s(label, "[cleo] %s", name[0] ? name : "?");
		strcpy_s(names[n], name);
		st = stats[n] = NamedStat((void *)(uintptr_t)(16 + n), label);
		n++;
	}
	LONGLONG t0 = Qpc();
	((void(__thiscall *)(void *))g_scriptProcess)(script);
	LuaLeave(st, t0);
}

static void ApplyScriptTimer()
{
	static bool done;
	if (done) return;
	done = true;
	BYTE *site = (BYTE *)0x46A21B;
	if (site[0] != 0xE8) {
		Log("script timer: the CRunningScript::Process call is not a call - skipped");
		return;
	}
	g_scriptProcess = (DWORD)(site + 5) + *(int32_t *)(site + 1); // the function, or whatever hooked the call
	AddCallChain(site, &g_scriptProcess);
	if (RedirectCall(site, hkScriptProcess)) Log("script timer: CLEO / GTA scripts timed");
}

static bool ApplyTextdrawTimer(BYTE *samp)
{
	ApplyScriptTimer();
	static bool done;
	if (done) return true;
	done = true;
	BYTE *site = SampAt(S_TEXTDRAW_SITE);
	DWORD fn = (DWORD)SampAt(S_TEXTDRAW_POOL_DRAW);
	if (!site || !fn || site[0] != 0xE8) {
		Log("textdraw timer: the CTextDrawPool::Draw call is not original - skipped");
		return true;
	}
	g_tdPoolDraw = (DWORD)(site + 5) + *(int32_t *)(site + 1); // the function, or the frame profiler's timer
	if (!IsCallTo(site, fn) && !InSampProfilerStubs(g_tdPoolDraw)) {
		Log("textdraw timer: the CTextDrawPool::Draw call is not original - skipped");
		return true;
	}
	AddCallChain(site, &g_tdPoolDraw);
	int32_t rel = Rel32(site, hkTextdrawPoolDraw);
	PatchBytes((DWORD)site + 1, nullptr, &rel, 4);
	Log("textdraw timer: CTextDrawPool::Draw timed");
	return true;
}

// ---------------------------------------------------------------- fix

static void Load(const Ini &ini)
{
	g_luaInterval = max(1, ini.Int("LuaProfiler", "interval", 10));
}

static void Start()
{
	g_luaWindowStart = g_luaFileStart = Qpc();
	CloseHandle(CreateThread(nullptr, 0, LuaProfilerThread, nullptr, 0, nullptr));
}

bool LuaProfilerRunning() { return g_luaRunning; }

void LuaProfilerStartNow()
{
	if (g_luaRunning) return;
	g_luaWindowStart = g_luaFileStart = Qpc();
	PatchMoonLoader();
	if (g_samp) ApplyTextdrawTimer(g_samp);
}

const std::vector<ProfRow> &LuaProfilerRows(double &totalPct)
{
	totalPct = g_luaTotalPct;
	return g_luaRows;
}

static const Param kParams[] = {
	{ "interval", "Файл отчёта раз в, с", &g_luaInterval, nullptr, 1, 120, true },
};

Fix g_fixLuaProfiler = {
	.section = "LuaProfiler",
	.title = "Lua профайлер",
	.summary = "Сколько ест каждый скрипт MoonLoader.",
	.details = "Замер времени на каждом входе MoonLoader в Lua (lua_pcall/lua_call/lua_resume), по скриптам и их функциям. "
	           "Нагрузка два замера таймера на вызов, незаметно.",
	.defaultOn = false,
	.load = Load,
	.atStart = Start,
	.atSamp = ApplyTextdrawTimer,
	.params = kParams,
	.paramCount = Count(kParams),
};
