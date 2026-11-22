// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
#include "ini.h"

#include <stdlib.h>

#include "plugin.h"
#include "../../res/resource.h"

Ini g_ini;

static void WriteDefault(const char *path)
{
	HRSRC r = FindResourceA(g_module, MAKEINTRESOURCEA(IDR_DEFAULT_INI), MAKEINTRESOURCEA(10) /* RT_RCDATA */);
	HGLOBAL h = r ? LoadResource(g_module, r) : nullptr;
	const void *data = h ? LockResource(h) : nullptr;
	if (!data) return;
	FILE *f = nullptr;
	if (fopen_s(&f, path, "wb") == 0 && f) {
		fwrite(data, 1, SizeofResource(g_module, r), f);
		fclose(f);
	}
}

void Ini::Open()
{
	sprintf_s(path_, "%s\\" PLUGIN_NAME ".ini", g_dir);
	if (GetFileAttributesA(path_) != INVALID_FILE_ATTRIBUTES) return;
	char old[MAX_PATH];
	sprintf_s(old, "%s\\" PLUGIN_OLD_NAME ".ini", g_dir);
	if (!MoveFileA(old, path_)) WriteDefault(path_); // the old name's settings are kept
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
