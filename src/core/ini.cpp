// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
#include "ini.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "plugin.h"
#include "../../res/resource.h"

Ini g_ini;
bool g_english;

struct IniEntry {
	std::string section;
	std::string key;
	std::string value;
};

static std::string Trim(const char *s, size_t len)
{
	size_t a = 0;
	while (a < len && (unsigned char)s[a] <= ' ') a++;
	while (len > a && (unsigned char)s[len - 1] <= ' ') len--;
	return std::string(s + a, len - a);
}

static bool ReadFileText(const char *path, std::string &out)
{
	FILE *f = nullptr;
	if (fopen_s(&f, path, "rb") != 0 || !f) return false;
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (sz > 0 && sz < 4 * 1024 * 1024) {
		out.resize((size_t)sz);
		size_t got = fread(&out[0], 1, (size_t)sz, f);
		out.resize(got);
	}
	fclose(f);
	return true;
}

static bool WriteResource(const char *path, int resId)
{
	HRSRC r = FindResourceA(g_module, MAKEINTRESOURCEA(resId), MAKEINTRESOURCEA(10) /* RT_RCDATA */);
	HGLOBAL h = r ? LoadResource(g_module, r) : nullptr;
	const void *data = h ? LockResource(h) : nullptr;
	if (!data) return false;
	FILE *f = nullptr;
	if (fopen_s(&f, path, "wb") != 0 || !f) return false;
	fwrite(data, 1, SizeofResource(g_module, r), f);
	fclose(f);
	return true;
}

// Reads english=0/1 from [Language], or from top-of-file lines before the first [section] if the user
// wrote `english=1` right at the start of the file. Returns -1 if not specified.
static int DetectEnglishFlag(const char *path, const std::string &text)
{
	int v = GetPrivateProfileIntA("Language", "english", -1, path);
	if (v != -1) return v != 0 ? 1 : 0;

	size_t pos = 0;
	while (pos < text.size()) {
		size_t nl = text.find('\n', pos);
		if (nl == std::string::npos) nl = text.size();
		std::string line = Trim(text.data() + pos, nl - pos);
		pos = nl + 1;
		if (line.empty() || line[0] == ';' || line[0] == '#') continue;
		if (line[0] == '[') break;
		size_t eq = line.find('=');
		if (eq != std::string::npos) {
			std::string k = Trim(line.data(), eq);
			if (!_stricmp(k.c_str(), "english")) {
				std::string val = Trim(line.data() + eq + 1, line.size() - eq - 1);
				return atoi(val.c_str()) != 0 ? 1 : 0;
			}
		}
	}
	return -1;
}

static std::vector<IniEntry> ParseEntries(const std::string &text)
{
	std::vector<IniEntry> entries;
	std::string sec;
	size_t pos = 0;
	while (pos < text.size()) {
		size_t nl = text.find('\n', pos);
		if (nl == std::string::npos) nl = text.size();
		std::string line = Trim(text.data() + pos, nl - pos);
		pos = nl + 1;
		if (line.empty() || line[0] == ';' || line[0] == '#') continue;
		if (line[0] == '[') {
			size_t rb = line.find(']');
			if (rb != std::string::npos) sec = Trim(line.data() + 1, rb - 1);
			continue;
		}
		if (sec.empty()) continue;
		size_t eq = line.find('=');
		if (eq == std::string::npos) continue;
		std::string key = Trim(line.data(), eq);
		std::string val = Trim(line.data() + eq + 1, line.size() - eq - 1);
		if (!key.empty()) entries.push_back({ std::move(sec), std::move(key), std::move(val) });
	}
	return entries;
}

static bool RewriteWithLanguage(const char *path, bool english)
{
	std::string existing;
	std::vector<IniEntry> saved;
	if (ReadFileText(path, existing))
		saved = ParseEntries(existing);

	if (!WriteResource(path, english ? IDR_DEFAULT_INI_EN : IDR_DEFAULT_INI))
		return false;

	for (const IniEntry &e : saved) {
		if (!_stricmp(e.section.c_str(), "Language") && !_stricmp(e.key.c_str(), "english"))
			continue;
		WritePrivateProfileStringA(e.section.c_str(), e.key.c_str(), e.value.c_str(), path);
	}
	WritePrivateProfileStringA("Language", "english", english ? "1" : "0", path);
	return true;
}

void Ini::Open()
{
	sprintf_s(path_, "%s\\" PLUGIN_NAME ".ini", g_dir);
	if (GetFileAttributesA(path_) == INVALID_FILE_ATTRIBUTES) {
		char old[MAX_PATH];
		sprintf_s(old, "%s\\" PLUGIN_OLD_NAME ".ini", g_dir);
		if (!MoveFileA(old, path_)) {
			WriteResource(path_, IDR_DEFAULT_INI);
			g_english = false;
			return;
		}
	}

	std::string text;
	if (!ReadFileText(path_, text)) return;
	int flag = DetectEnglishFlag(path_, text);
	bool wantEnglish = (flag == 1);
	g_english = wantEnglish;

	bool hasLangSection = text.find("[Language]") != std::string::npos;
	bool hasTargetComments = wantEnglish
		? (text.find("FPS optimization for GTA SA") != std::string::npos)
		: (text.find("оптимизация FPS для GTA SA") != std::string::npos);

	if (!hasLangSection || !hasTargetComments) {
		if (RewriteWithLanguage(path_, wantEnglish))
			Log("Ini: rewritten in %s (settings preserved)", wantEnglish ? "English" : "Russian");
	}
}

bool Ini::SetLanguage(bool english) const
{
	g_english = english;
	if (!RewriteWithLanguage(path_, english)) return false;
	Log("Ini: language switched to %s", english ? "English" : "Russian");
	return true;
}

int Ini::Int(const char *section, const char *key, int def) const
{
	return GetPrivateProfileIntA(section, key, def, path_);
}

float Ini::Float(const char *section, const char *key, float def) const
{
	char d[32], b[32];
	sprintf_s(d, "%g", def);
	GetPrivateProfileStringA(section, key, d, b, sizeof(b), path_);
	return (float)atof(b);
}

void Ini::SetInt(const char *section, const char *key, int value) const
{
	char b[32];
	sprintf_s(b, "%d", value);
	WritePrivateProfileStringA(section, key, b, path_);
}

void Ini::SetFloat(const char *section, const char *key, float value) const
{
	char b[32];
	sprintf_s(b, "%g", value);
	WritePrivateProfileStringA(section, key, b, path_);
}