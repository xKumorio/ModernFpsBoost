// ModernFpsBoost - UI language selection and small localization helper.
#pragma once

#include <string>
#include <unordered_map>
#include <utility>

#include "imgui.h"
#include "ui.h"

inline ImFont *UiFont(ImFont *font)
{
	return g_ui.language == 1 && g_fonts.body ? g_fonts.body : font;
}

// Chinese translation literals contain CP936 bytes. ImGui takes UTF-8.
inline const char *UiChinese(const char *gbk, const char *fallback)
{
	if (!gbk || !*gbk) return gbk ? "" : fallback;
	static std::unordered_map<std::string, std::string> cache;
	auto it = cache.find(gbk);
	if (it != cache.end()) return it->second.c_str();

	int count = MultiByteToWideChar(936, 0, gbk, -1, nullptr, 0);
	if (!count) return fallback;
	std::wstring wide(count, L'\0');
	if (!MultiByteToWideChar(936, 0, gbk, -1, wide.data(), count)) return fallback;
	for (int i = 0; i < count - 1; ++i)
		if (wide[i] > 0x7F && (!g_fonts.body || !g_fonts.body->FindGlyphNoFallback(wide[i]))) return fallback;
	int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.data(), count - 1, nullptr, 0, nullptr, nullptr);
	if (!bytes) return fallback;
	std::string utf8(bytes, '\0');
	if (!WideCharToMultiByte(CP_UTF8, 0, wide.data(), count - 1, utf8.data(), bytes, nullptr, nullptr)) return fallback;
	return cache.emplace(gbk, std::move(utf8)).first->second.c_str();
}

inline const char *UiText(const char *ru, const char *zh, const char *en)
{
	const char *fallback = ru ? ru : "";
	switch (g_ui.language) {
	case 1: return UiChinese(zh, en ? en : fallback);
	case 2: return en ? en : fallback;
	default: return fallback;
	}
}
