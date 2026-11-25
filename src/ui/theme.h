// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// Look of the menu: Yoshitaka Amano gothic ink & watercolor palette — smoked velvet charcoal,
// warm aged ivory typography, deep crimson silk & antique pale gold hairlines. Eye-friendly, zero AI slop.
#pragma once

#include "imgui.h"

namespace pal {
constexpr ImU32 Accent       = IM_COL32(204, 66, 84, 255);   // Amano crimson silk
constexpr ImU32 Accent2      = IM_COL32(202, 174, 122, 255); // antique pale gold
constexpr ImU32 Text         = IM_COL32(238, 230, 218, 255); // warm aged ivory (soft on eyes)
constexpr ImU32 Text2        = IM_COL32(174, 164, 154, 255); // smoked ash parchment
constexpr ImU32 Text3        = IM_COL32(122, 114, 110, 255); // muted ink gray
constexpr ImU32 Canvas       = IM_COL32(19, 17, 21, 240);    // smoked velvet ink
constexpr ImU32 Rail         = IM_COL32(14, 12, 16, 224);    // translucent deep ink rail
constexpr ImU32 Surface      = IM_COL32(27, 24, 30, 212);    // smoked charcoal-plum card
constexpr ImU32 SurfaceHover = IM_COL32(36, 32, 40, 236);    // elevated velvet hover/open
constexpr ImU32 Panel        = IM_COL32(23, 20, 26, 218);    // elevated container
constexpr ImU32 Line         = IM_COL32(218, 198, 168, 24);  // antique gold-ivory hairline
constexpr ImU32 LineStrong   = IM_COL32(218, 198, 168, 48);  // active antique hairline
constexpr ImU32 Ok           = IM_COL32(114, 194, 152, 255); // painterly sage-jade
constexpr ImU32 Warn         = IM_COL32(214, 174, 104, 255); // burnished antique gold
constexpr ImU32 Bad          = IM_COL32(218, 76, 92, 255);   // crimson rose
}

ImU32 Alpha(ImU32 c, float a);           // c with its alpha * a
ImU32 Mix(ImU32 a, ImU32 b, float t);    // a..b
ImU32 Col(ImU32 c);                      // c * style alpha (fade of the whole menu)
inline ImVec2 Snap(ImVec2 p) { return ImVec2((float)(int)(p.x + 0.5f), (float)(int)(p.y + 0.5f)); }

float EaseOut(float t);                  // cubic, t in 0..1
float Anim(ImGuiID id, float target, float speed = 14.0f); // eases a stored value toward target
float Anim(const char *key, float target, float speed = 14.0f);

void LoadFonts();
void ApplyTheme();

void GradientRect(ImDrawList *dl, ImVec2 a, ImVec2 b, ImU32 c0, ImU32 c1, float rounding, bool vertical = false);
void Shadow(ImDrawList *dl, ImVec2 a, ImVec2 b, float rounding, float size, float strength);
void Surface(ImDrawList *dl, ImVec2 a, ImVec2 b, float rounding = 8.0f, ImU32 fill = pal::Surface, ImU32 line = pal::Line);

// Lucide icon centered on c (its own font, centered by the glyph's box: crisp at integer pixels)
void Icon(ImDrawList *dl, ImVec2 c, const char *icon, ImFont *font, ImU32 col);
void IconTile(ImDrawList *dl, ImVec2 a, float size, const char *icon, ImFont *font, ImU32 tint);

bool Toggle(const char *id, bool *v);
bool Button(const char *id, const char *icon, const char *label, bool primary, float width = 0);
bool Segmented(const char *id, const char *const *items, int count, int *current);
bool SliderInt(const char *id, int *v, int min, int max, const char *fmt, float width);
bool SliderFloat(const char *id, float *v, float min, float max, const char *fmt, float width);
bool SearchBox(const char *id, char *buf, int size, float width, const char *hint = "Search");
bool MenuItem(const char *icon, const char *label); // in a context menu
void ShowToast(const char *text);                     // subtle status confirmation at bottom
void DrawToast(ImVec2 windowMin, ImVec2 windowMax);
void Bar(ImVec2 size, float frac);       // at the cursor
void Sparkline(ImVec2 size);             // frame times from g_frames
void TextColored(ImU32 col, const char *text);
void TextWrapped(ImU32 col, const char *text, float width);
void Tooltip(const char *title, const char *text);
