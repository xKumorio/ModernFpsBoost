// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// ModernFpsBoost.ini next to the .asi; written from the embedded default (res/) when missing.
#pragma once

class Ini {
public:
	void Open(); // creates the file from the default if it does not exist
	int   Int(const char *section, const char *key, int def) const;
	bool  Bool(const char *section, const char *key, bool def) const { return Int(section, key, def ? 1 : 0) != 0; }
	float Float(const char *section, const char *key, float def) const;
	void  SetInt(const char *section, const char *key, int value) const;
	void  SetFloat(const char *section, const char *key, float value) const;

private:
	char path_[260] = {};
};

extern Ini g_ini;
