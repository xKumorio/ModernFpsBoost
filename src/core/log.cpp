// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
#include "plugin.h"

#include <mutex>
#include <share.h>
#include <stdarg.h>

HMODULE g_module;
char    g_dir[MAX_PATH];
DWORD   g_mainThread;
BYTE   *g_samp;

static FILE *g_log;
static std::mutex g_logLock;
static std::vector<std::string> g_lines; // for the menu
static size_t g_dropped;                 // lines dropped from the front of g_lines
static const size_t kMaxLines = 1000;

void Log(const char *fmt, ...)
{
	char text[1024];
	SYSTEMTIME t;
	GetLocalTime(&t);
	int n = sprintf_s(text, "[%02d:%02d:%02d] ", t.wHour, t.wMinute, t.wSecond);
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(text + n, sizeof(text) - n, fmt, ap);
	va_end(ap);

	std::lock_guard<std::mutex> lock(g_logLock);
	if (g_lines.size() >= kMaxLines) {
		g_lines.erase(g_lines.begin(), g_lines.begin() + kMaxLines / 4);
		g_dropped += kMaxLines / 4;
	}
	g_lines.push_back(text);
	if (!g_log) {
		char path[MAX_PATH];
		sprintf_s(path, "%s\\" PLUGIN_NAME ".log", g_dir);
		g_log = _fsopen(path, "w", _SH_DENYWR);
		if (!g_log) return;
	}
	fputs(text, g_log);
	fputc('\n', g_log);
	fflush(g_log);
}

void LogTail(std::vector<std::string> &out, size_t &seen)
{
	std::lock_guard<std::mutex> lock(g_logLock);
	size_t from = seen > g_dropped ? seen - g_dropped : 0;
	for (size_t i = from; i < g_lines.size(); i++) out.push_back(g_lines[i]);
	seen = g_dropped + g_lines.size();
}

void Appendf(std::string &out, const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (n > 0) out.append(buf, n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1);
}
