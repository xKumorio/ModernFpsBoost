// ModernFpsBoost - UI localization catalog for fix metadata.
#pragma once

// Text selected for the current UI language (g_ui.language).
struct FixText {
	const char *title;
	const char *gain;
	const char *summary;
	const char *details;
};

// Returns localized metadata for a stable Fix.section. Unknown sections return empty strings.
FixText GetFixText(const char *section);

// Returns a localized label for section + Param.key, or the Russian source label when the
// pair is not in the catalog.
const char *GetParamLabel(const char *section, const char *key, const char *fallback);

// Coverage helpers for diagnostics/tests; ordinary registered fixes are expected to be 59.
int FixTextCount();
int FixParamLabelCount();
