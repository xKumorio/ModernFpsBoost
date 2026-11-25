// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// One fix = one ini section. Every fix fills a Fix and is listed in registry.cpp.
#pragma once

#include <windows.h>
#include <stdio.h>
#include <string>

#include "../core/game.h"
#include "../core/ini.h"
#include "../core/patch.h"
#include "../core/plugin.h"

// A setting of a fix, shown in the menu as a slider and saved to the ini.
struct Param {
	const char *key;       // ini key
	const char *label;     // menu text
	int *i;                // the setting (one of i / f)
	float *f;
	float min, max;
	bool live;             // takes effect at once; otherwise after a restart
};

struct Fix {
	const char *section;                    // ini section ([section] enabled=1/0) and name in the log
	const char *title = "";                 // menu: short Russian name
	const char *gain = "";                  // menu: what it saves, "-7% кадра"
	const char *summary = "";               // menu: one or two lines on the card
	const char *details = "";               // menu: why it is slow and how it is fixed (tooltip)
	bool defaultOn = true;                  // enabled when the ini has no value
	void (*load)(const Ini &) = nullptr;    // reads the section's settings; only for enabled fixes
	bool gta = false;                       // atStart patches gta_sa.exe: only with 1.0 US
	void (*atStart)() = nullptr;            // in DllMain, before the game initializes
	bool (*atSamp)(BYTE *samp) = nullptr;   // once samp.dll 0.3.7 R1 is loaded; false = not ready yet, retried every 250 ms
	void (*report)(std::string &out, double secs) = nullptr; // stats lines, counters reset
	void (*changed)() = nullptr;            // menu: a live setting was moved
	const Param *params = nullptr;
	int paramCount = 0;

	bool enabled = false;  // [section] enabled= at start
	bool attached = false; // atSamp has finished
	bool active = false;   // the patch is in
};

template <size_t N>
constexpr int Count(const Param (&)[N]) { return (int)N; }

const char *FixTitle(const Fix *f);
const char *FixGain(const Fix *f);
const char *FixSummary(const Fix *f);
const char *FixDetails(const Fix *f);
const char *ParamLabel(const Fix *f, const Param &p);
