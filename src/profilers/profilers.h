// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// Profiler results for the menu: refreshed every second on the game's main thread (read them there).
#pragma once

#include <windows.h>
#include <string>
#include <vector>

struct ProfRow {
	std::string name;
	double pct = 0, ms = 0, calls = 0; // % of the main thread, ms per second, calls per second
	std::vector<ProfRow> parts;        // Lua: entry functions of the script
};

struct TimerRow {
	int id, parent;         // call tree node; the node it was called from (-1 = top level)
	DWORD addr;
	const char *label;
	bool gta;               // gta_sa.exe function / entity virtual, else samp.dll
	double incl, self, calls; // % of the main thread with / without the timed functions it calls, calls per second
};

struct OpRow { WORD op; DWORD site; double incl, calls; };

// [LuaProfiler]
bool LuaProfilerRunning();
void LuaProfilerStartNow(); // MoonLoader is loaded: patch now
const std::vector<ProfRow> &LuaProfilerRows(double &totalPct);

// Lines of one script that take its time: LuaJIT's sampling profiler (1 ms) on that script's VM.
struct LineRow {
	std::string path;  // full path of the .lua file
	std::string file;  // name shown
	int line = 0;
	double share = 0;  // of the samples in this script
	double pct = 0;    // of the frame: the script's own share times this line's
	double samples = 0;
};
void LuaLinesWatch(const char *script);   // nullptr: stop
const char *LuaLinesScript();             // the script being sampled, nullptr = none
const std::vector<LineRow> &LuaLinesRows(double &samples, double &seconds);
void LuaLinesReset();
const std::vector<std::string> &LuaSource(const std::string &path); // file lines, cached

// [SampProfiler]
bool SampProfilerRunning();
bool InSampProfilerStubs(DWORD p); // p is one of the frame profiler's timers
bool SampProfilerStarting();
void SampProfilerStartNow(); // patches from its own thread (the main thread gets suspended)
const std::vector<TimerRow> &SampProfilerRows();
const std::vector<OpRow> &SampProfilerOps();
int SampProfilerSelfTotals(LONGLONG *out, int max); // own time of every timer since the start, QPC ticks; returns their count
const char *SampProfilerTimerLabel(int i);
const char *SampProfilerNameOf(DWORD pc, DWORD *off); // the named samp.dll function pc is in, nullptr = none

// [SpikeProfiler] long frames and input lag (spike_profiler.cpp)
enum { SPIKE_CPU, SPIKE_WAIT, SPIKE_PRESENT, SPIKE_CAUSES };
struct SpikeRow {
	double at = 0;                                    // seconds since the start
	float ms = 0, normal = 0, cpu = 0, wait = 0, present = 0; // the frame, the normal frame then, its parts
	int streamKb = 0;                                 // model memory streamed in during it
	unsigned long faults = 0;                         // page faults during it
	int cause = 0;                                    // the part that grew the most
	std::string top;                                  // functions whose own time grew (frame profiler running)
	std::string stack;                                // where the main thread was during it: lines "N из M: f <- caller"
};
struct SpikeSummary {
	double seconds = 0;
	int frames = 0, spikes = 0;
	float avgMs = 0, low1Ms = 0, low01Ms = 0, worstMs = 0;
	float inputMs = 0, inputWorstMs = 0; // input read -> end of Present: average, worst 1%
	bool inputMeasured = false;
	float cpuShare = 0, waitShare = 0, presentShare = 0;
	int causeCount[SPIKE_CAUSES] = {};
	float causeMs[SPIKE_CAUSES] = {};
};
void SpikeProfilerInit();               // at start: hooks the input read
bool SpikeProfilerRunning();
void SpikeProfilerStart();
void SpikeProfilerStop();
void SpikeProfilerReset();
void SpikePresentBegin();               // around the real Present
void SpikePresentEnd(bool menuOpen);
const SpikeSummary &SpikeProfilerSummary();
const std::vector<SpikeRow> &SpikeProfilerRows();
int SpikeProfilerFrames(float *out, int max); // the last frames' ms, oldest first
const char *SpikeCauseName(int cause);
std::string SpikeProfilerText();
