// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
#include "theme.h"

#include <math.h>
#include <stdio.h>
#include <string>

#include "imgui_internal.h"

#include "../../res/resource.h"
#include "../core/plugin.h"
#include "icons.h"
#include "language.h"
#include "ui.h"

// ---------------------------------------------------------------- color / animation

ImU32 Alpha(ImU32 c, float a)
{
	float ca = ((c >> IM_COL32_A_SHIFT) & 0xFF) * ImClamp(a, 0.0f, 1.0f);
	return (c & ~IM_COL32_A_MASK) | ((ImU32)(ca + 0.5f) << IM_COL32_A_SHIFT);
}

ImU32 Mix(ImU32 a, ImU32 b, float t)
{
	t = ImClamp(t, 0.0f, 1.0f);
	ImU32 r = 0;
	for (int s = 0; s < 32; s += 8) {
		float x = ((a >> s) & 0xFF) + (((int)((b >> s) & 0xFF) - (int)((a >> s) & 0xFF)) * t);
		r |= (ImU32)(x + 0.5f) << s;
	}
	return r;
}

ImU32 Col(ImU32 c) { return Alpha(c, ImGui::GetStyle().Alpha); }

float EaseOut(float t)
{
	t = ImClamp(t, 0.0f, 1.0f);
	float u = 1.0f - t;
	return 1.0f - u * u * u;
}

float Anim(ImGuiID id, float target, float speed)
{
	ImGuiStorage *st = ImGui::GetStateStorage();
	float *v = st->GetFloatRef(id, target);
	*v += (target - *v) * ImMin(1.0f, ImGui::GetIO().DeltaTime * speed);
	if (fabsf(target - *v) < 0.001f) *v = target;
	return *v;
}

float Anim(const char *key, float target, float speed) { return Anim(ImGui::GetID(key), target, speed); }

// ---------------------------------------------------------------- fonts

static const ImWchar *TextRanges()
{
	static const ImWchar ranges[] = {
		0x0020, 0x00FF, // Latin, Latin-1 («» ·)
		0x0400, 0x045F, // Cyrillic
		0x2013, 0x2014, // – —
		0x2022, 0x2022, // •
		0x2026, 0x2026, // …
		0x2116, 0x2116, // №
		0x2212, 0x2212, // −
		0,
	};
	return ranges;
}

static void MergeChineseGlyphs(ImFont *dst, float size)
{
	ImGuiIO &io = ImGui::GetIO();
	static ImVector<ImWchar> ranges;
	if (ranges.empty()) {
		ImFontGlyphRangesBuilder builder;
		builder.AddRanges(io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
		builder.AddText("函坠屏帧擎昵栈渲溢烁眩瞄聊迭锥频骼");
		builder.BuildRanges(&ranges);
	}
	char dir[MAX_PATH];
	GetWindowsDirectoryA(dir, MAX_PATH);
	ImFontConfig cfg;
	cfg.MergeMode = true;
	cfg.DstFont = dst;
	cfg.OversampleH = 1;
	cfg.OversampleV = 1;
	for (const char *name : { "msyh.ttc", "simsun.ttc", "Deng.ttf" }) {
		std::string path = std::string(dir) + "\\Fonts\\" + name;
		if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
		if (io.Fonts->AddFontFromFileTTF(path.c_str(), size, &cfg, ranges.Data)) break;
	}
}

static ImFont *AddTextFont(const char *file, float size, bool chinese = false)
{
	ImGuiIO &io = ImGui::GetIO();
	char dir[MAX_PATH];
	GetWindowsDirectoryA(dir, MAX_PATH);
	ImFontConfig cfg;
	cfg.OversampleH = 3;
	cfg.OversampleV = 1;
	cfg.PixelSnapH = true;
	ImFont *font = nullptr;
	for (const char *name : { file, "segoeui.ttf", "arial.ttf" }) {
		std::string path = std::string(dir) + "\\Fonts\\" + name;
		if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
		if ((font = io.Fonts->AddFontFromFileTTF(path.c_str(), size, &cfg, TextRanges()))) break;
	}
	if (!font) font = io.Fonts->AddFontDefault();
	if (chinese) MergeChineseGlyphs(font, size);
	return font;
}

static ImFont *AddIconFont(float size)
{
	HRSRC r = FindResourceA(g_module, MAKEINTRESOURCEA(IDR_ICONS), MAKEINTRESOURCEA(10) /* RT_RCDATA */);
	HGLOBAL h = r ? LoadResource(g_module, r) : nullptr;
	void *data = h ? LockResource(h) : nullptr;
	if (!data) return nullptr;
	ImFontConfig cfg;
	cfg.FontDataOwnedByAtlas = false;
	cfg.OversampleH = 3;
	cfg.OversampleV = 2;
	return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(data, (int)SizeofResource(g_module, r), size, &cfg, (const ImWchar *)kIconRanges);
}

void LoadFonts()
{
	g_fonts.body = AddTextFont("segoeui.ttf", 17.0f, g_ui.language == 1);
	// UiFont selects body for Chinese text, including after a language switch.
	g_fonts.medium = AddTextFont("seguisb.ttf", 17.0f);
	g_fonts.title = AddTextFont("seguisb.ttf", 24.0f);
	g_fonts.number = AddTextFont("seguisb.ttf", 34.0f);
	g_fonts.tiny = AddTextFont("segoeui.ttf", 15.0f);
	g_fonts.mono = AddTextFont("consola.ttf", 15.0f);
	g_fonts.icon14 = AddIconFont(14.0f);
	g_fonts.icon16 = AddIconFont(16.0f);
	g_fonts.icon20 = AddIconFont(20.0f);
	g_fonts.icon28 = AddIconFont(26.0f);
	if (!g_fonts.icon14) g_fonts.icon14 = g_fonts.icon16 = g_fonts.icon20 = g_fonts.icon28 = g_fonts.body;
	ImGui::GetIO().FontDefault = g_fonts.body;
}

// ---------------------------------------------------------------- style

void ApplyTheme()
{
	ImGuiStyle &s = ImGui::GetStyle();
	ImGui::StyleColorsDark(&s);
	s.WindowRounding = 10.0f;
	s.ChildRounding = 8.0f;
	s.FrameRounding = 6.0f;
	s.GrabRounding = 6.0f;
	s.PopupRounding = 8.0f;
	s.ScrollbarRounding = 4.0f;
	s.WindowBorderSize = 0.0f;
	s.ChildBorderSize = 0.0f;
	s.PopupBorderSize = 1.0f;
	s.WindowPadding = ImVec2(0, 0);
	s.FramePadding = ImVec2(10, 6);
	s.ItemSpacing = ImVec2(8, 8);
	s.ItemInnerSpacing = ImVec2(8, 6);
	s.ScrollbarSize = 6.0f;
	s.CellPadding = ImVec2(10, 7);
	s.AntiAliasedLines = true;
	s.AntiAliasedFill = true;
	s.CurveTessellationTol = 0.6f;
	s.CircleTessellationMaxError = 0.2f;

	ImVec4 *c = s.Colors;
	auto v = [](ImU32 col) { return ImGui::ColorConvertU32ToFloat4(col); };
	c[ImGuiCol_Text] = v(pal::Text);
	c[ImGuiCol_TextDisabled] = v(pal::Text3);
	c[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0);
	c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
	c[ImGuiCol_PopupBg] = v(IM_COL32(24, 21, 27, 250));
	c[ImGuiCol_Border] = v(pal::LineStrong);
	c[ImGuiCol_FrameBg] = v(IM_COL32(218, 198, 168, 14));
	c[ImGuiCol_FrameBgHovered] = v(IM_COL32(218, 198, 168, 22));
	c[ImGuiCol_FrameBgActive] = v(IM_COL32(218, 198, 168, 30));
	c[ImGuiCol_Header] = v(Alpha(pal::Accent, 0.22f));
	c[ImGuiCol_HeaderHovered] = v(IM_COL32(218, 198, 168, 14));
	c[ImGuiCol_HeaderActive] = v(IM_COL32(218, 198, 168, 22));
	c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
	c[ImGuiCol_ScrollbarGrab] = v(IM_COL32(218, 198, 168, 34));
	c[ImGuiCol_ScrollbarGrabHovered] = v(IM_COL32(218, 198, 168, 60));
	c[ImGuiCol_ScrollbarGrabActive] = v(Alpha(pal::Accent2, 0.85f));
	c[ImGuiCol_Separator] = v(pal::Line);
	c[ImGuiCol_TableHeaderBg] = ImVec4(0, 0, 0, 0);
	c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
	c[ImGuiCol_TableRowBgAlt] = v(IM_COL32(218, 198, 168, 7));
	c[ImGuiCol_TableBorderLight] = v(pal::Line);
	c[ImGuiCol_TableBorderStrong] = v(pal::Line);
	c[ImGuiCol_TextSelectedBg] = v(Alpha(pal::Accent, 0.30f));
	c[ImGuiCol_NavHighlight] = ImVec4(0, 0, 0, 0);
}

// ---------------------------------------------------------------- drawing

static void ShadeAdded(ImDrawList *dl, int start, ImVec2 p0, ImVec2 p1, ImU32 c0, ImU32 c1)
{
	ImVec2 d = ImVec2(p1.x - p0.x, p1.y - p0.y);
	float len2 = ImMax(d.x * d.x + d.y * d.y, 0.0001f);
	float sa = ImGui::GetStyle().Alpha;
	for (int i = start; i < dl->VtxBuffer.Size; i++) {
		ImDrawVert &vtx = dl->VtxBuffer[i];
		float t = ImClamp(((vtx.pos.x - p0.x) * d.x + (vtx.pos.y - p0.y) * d.y) / len2, 0.0f, 1.0f);
		ImU32 c = Mix(c0, c1, t);
		float a = ((c >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f * ((vtx.col >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f * sa;
		vtx.col = (c & ~IM_COL32_A_MASK) | ((ImU32)(a * 255.0f + 0.5f) << IM_COL32_A_SHIFT);
	}
}

void GradientRect(ImDrawList *dl, ImVec2 a, ImVec2 b, ImU32 c0, ImU32 c1, float rounding, bool vertical)
{
	int start = dl->VtxBuffer.Size;
	dl->AddRectFilled(a, b, IM_COL32_WHITE, rounding);
	ShadeAdded(dl, start, a, vertical ? ImVec2(a.x, b.y) : ImVec2(b.x, a.y), c0, c1);
}

void Shadow(ImDrawList *dl, ImVec2 a, ImVec2 b, float rounding, float size, float strength)
{
	const int steps = 8;
	for (int i = 1; i <= steps; i++) {
		float g = size * i / steps;
		float k = 1.0f - (float)i / (steps + 1);
		dl->AddRect(ImVec2(a.x - g, a.y - g + size * 0.30f), ImVec2(b.x + g, b.y + g + size * 0.30f),
		            Col(IM_COL32(6, 4, 8, (int)(255 * strength * k * k / steps * 2.1f))), rounding + g, 0, size / steps + 1.0f);
	}
}

void Surface(ImDrawList *dl, ImVec2 a, ImVec2 b, float rounding, ImU32 fill, ImU32 line)
{
	a = Snap(a);
	b = Snap(b);
	dl->AddRectFilled(a, b, Col(fill), rounding);
	dl->AddRect(ImVec2(a.x + 0.5f, a.y + 0.5f), ImVec2(b.x - 0.5f, b.y - 0.5f), Col(line), rounding, 0, 1.0f);
}

void Icon(ImDrawList *dl, ImVec2 c, const char *icon, ImFont *font, ImU32 col)
{
	unsigned int cp = 0;
	ImTextCharFromUtf8(&cp, icon, nullptr);
	const ImFontGlyph *g = font->FindGlyphNoFallback((ImWchar)cp);
	if (!g) return;
	ImVec2 p(c.x - (g->X0 + g->X1) * 0.5f, c.y - (g->Y0 + g->Y1) * 0.5f);
	dl->AddText(font, font->FontSize, Snap(p), Col(col), icon);
}

// Smoked velvet icon square with antique gold-ivory hairline
void IconTile(ImDrawList *dl, ImVec2 a, float size, const char *icon, ImFont *font, ImU32 tint)
{
	a = Snap(a);
	ImVec2 b(a.x + size, a.y + size);
	dl->AddRectFilled(a, b, Col(IM_COL32(31, 27, 35, 240)), 6.0f);
	dl->AddRect(ImVec2(a.x + 0.5f, a.y + 0.5f), ImVec2(b.x - 0.5f, b.y - 0.5f), Col(IM_COL32(218, 198, 168, 28)), 6.0f);
	Icon(dl, ImVec2(a.x + size * 0.5f, a.y + size * 0.5f), icon, font, tint);
}

// ---------------------------------------------------------------- widgets

bool Toggle(const char *id, bool *v)
{
	const ImVec2 size(36, 20);
	ImVec2 p = Snap(ImGui::GetCursorScreenPos());
	ImGui::InvisibleButton(id, size);
	bool changed = false;
	if (ImGui::IsItemClicked()) {
		*v = !*v;
		changed = true;
	}
	ImGuiID gid = ImGui::GetItemID();
	float t = EaseOut(Anim(gid, *v ? 1.0f : 0.0f, 14.0f));
	float hover = Anim(gid + 1, ImGui::IsItemHovered() ? 1.0f : 0.0f, 16.0f);

	ImDrawList *dl = ImGui::GetWindowDrawList();
	ImVec2 b(p.x + size.x, p.y + size.y);
	float r = size.y * 0.5f;
	ImU32 offTrack = Mix(IM_COL32(44, 39, 49, 255), IM_COL32(56, 50, 62, 255), hover);
	ImU32 onTrack  = Mix(IM_COL32(186, 52, 70, 255), IM_COL32(208, 66, 86, 255), hover);
	dl->AddRectFilled(p, b, Col(Mix(offTrack, onTrack, t)), r);
	dl->AddRect(ImVec2(p.x + 0.5f, p.y + 0.5f), ImVec2(b.x - 0.5f, b.y - 0.5f),
	            Col(Alpha(IM_COL32(218, 198, 168, 34), 1.0f - t * 0.5f)), r);

	float knobD = size.y - 6.0f;
	float x0 = p.x + 3.0f + t * (size.x - 6.0f - knobD);
	ImVec2 ka(x0, p.y + 3.0f), kb(x0 + knobD, b.y - 3.0f);
	dl->AddRectFilled(ka, kb, Col(Mix(IM_COL32(158, 148, 138, 255), IM_COL32(246, 239, 228, 255), t)), r);
	return changed;
}

bool Button(const char *id, const char *icon, const char *label, bool primary, float width, ImFont *font)
{
	ImGui::PushFont(font ? font : UiFont(g_fonts.medium));
	ImVec2 ts = ImGui::CalcTextSize(label);
	float iconW = icon ? 20.0f : 0.0f;
	ImVec2 size(width > 0 ? width : ts.x + iconW + 26, 32);
	ImVec2 p = Snap(ImGui::GetCursorScreenPos());
	bool pressed = ImGui::InvisibleButton(id, size);
	ImGuiID gid = ImGui::GetItemID();
	float hover = Anim(gid, ImGui::IsItemHovered() ? 1.0f : 0.0f, 16.0f);
	float held = Anim(gid + 1, ImGui::IsItemActive() ? 1.0f : 0.0f, 24.0f);
	ImDrawList *dl = ImGui::GetWindowDrawList();
	ImVec2 b(p.x + size.x, p.y + size.y);
	ImU32 fg;
	if (primary) {
		ImU32 bg = Mix(IM_COL32(178, 48, 66, 255), IM_COL32(204, 64, 82, 255), hover * (1.0f - held * 0.5f));
		if (held > 0.01f) bg = Mix(bg, IM_COL32(146, 36, 52, 255), held);
		dl->AddRectFilled(p, b, Col(bg), 6.0f);
		dl->AddRect(ImVec2(p.x + 0.5f, p.y + 0.5f), ImVec2(b.x - 0.5f, b.y - 0.5f), Col(IM_COL32(238, 214, 184, 52)), 6.0f);
		fg = IM_COL32(248, 242, 232, 255);
	} else {
		dl->AddRectFilled(p, b, Col(Mix(IM_COL32(31, 27, 35, 240), IM_COL32(43, 38, 48, 248), hover)), 6.0f);
		dl->AddRect(ImVec2(p.x + 0.5f, p.y + 0.5f), ImVec2(b.x - 0.5f, b.y - 0.5f),
		            Col(Mix(pal::Line, Alpha(pal::Accent2, 0.42f), hover)), 6.0f);
		fg = Mix(pal::Text2, pal::Text, hover);
	}
	float cx = p.x + (size.x - ts.x - iconW) * 0.5f;
	if (icon) Icon(dl, ImVec2(cx + 7, p.y + size.y * 0.5f), icon, g_fonts.icon14, primary ? fg : Mix(pal::Text3, pal::Accent2, hover));
	dl->AddText(Snap(ImVec2(cx + iconW, p.y + (size.y - ts.y) * 0.5f)), Col(fg), label);
	ImGui::PopFont();
	return pressed;
}

bool Segmented(const char *id, const char *const *items, int count, int *current)
{
	ImGui::PushID(id);
	ImGui::PushFont(UiFont(g_fonts.medium));
	float widths[8], total = 6;
	for (int i = 0; i < count; i++) total += widths[i] = ImGui::CalcTextSize(items[i]).x + 24;
	ImVec2 p = Snap(ImGui::GetCursorScreenPos());
	ImVec2 b(p.x + total, p.y + 32);
	ImDrawList *dl = ImGui::GetWindowDrawList();
	dl->AddRectFilled(p, b, Col(IM_COL32(18, 16, 21, 224)), 7.0f);
	dl->AddRect(ImVec2(p.x + 0.5f, p.y + 0.5f), ImVec2(b.x - 0.5f, b.y - 0.5f), Col(pal::Line), 7.0f);

	float x = p.x + 3, selX = 0, selW = 0;
	for (int i = 0; i < count; i++) {
		if (i == *current) selX = x, selW = widths[i];
		x += widths[i];
	}
	float ax = Anim("sel_x", selX, 20.0f), aw = Anim("sel_w", selW, 20.0f);
	dl->AddRectFilled(ImVec2(ax, p.y + 3), ImVec2(ax + aw, b.y - 3), Col(IM_COL32(43, 37, 47, 252)), 5.0f);
	dl->AddRect(ImVec2(ax + 0.5f, p.y + 3.5f), ImVec2(ax + aw - 0.5f, b.y - 3.5f), Col(Alpha(pal::Accent2, 0.34f)), 5.0f);

	bool changed = false;
	x = p.x + 3;
	for (int i = 0; i < count; i++) {
		ImGui::SetCursorScreenPos(ImVec2(x, p.y + 3));
		ImGui::PushID(i);
		if (ImGui::InvisibleButton("seg", ImVec2(widths[i], 26)) && *current != i) *current = i, changed = true;
		float hover = Anim(ImGui::GetItemID(), ImGui::IsItemHovered() || *current == i ? 1.0f : 0.0f, 16.0f);
		ImGui::PopID();
		ImVec2 ts = ImGui::CalcTextSize(items[i]);
		dl->AddText(Snap(ImVec2(x + (widths[i] - ts.x) * 0.5f, p.y + (32 - ts.y) * 0.5f)), Col(Mix(pal::Text3, pal::Text, hover)), items[i]);
		x += widths[i];
	}
	ImGui::SetCursorScreenPos(p);
	ImGui::Dummy(ImVec2(total, 32));
	ImGui::PopFont();
	ImGui::PopID();
	return changed;
}

// Amano fader: antique gold track fill, warm ivory knob
static bool SliderBehavior(const char *id, float *t, float width)
{
	ImVec2 p = Snap(ImGui::GetCursorScreenPos());
	const float h = 20;
	ImGui::InvisibleButton(id, ImVec2(width, h));
	ImGuiID gid = ImGui::GetItemID();
	bool active = ImGui::IsItemActive();
	bool changed = false;
	if (active) {
		float nt = ImClamp((ImGui::GetIO().MousePos.x - p.x - 6) / (width - 12), 0.0f, 1.0f);
		if (nt != *t) *t = nt, changed = true;
	}
	float hover = Anim(gid, ImGui::IsItemHovered() || active ? 1.0f : 0.0f, 16.0f);
	float shown = Anim(gid + 1, *t, 24.0f);
	ImDrawList *dl = ImGui::GetWindowDrawList();
	float cy = p.y + h * 0.5f;
	ImVec2 ta(p.x + 6, cy - 1.5f), tb(p.x + width - 6, cy + 1.5f);
	dl->AddRectFilled(ta, tb, Col(IM_COL32(52, 46, 56, 255)), 1.5f);
	float kx = ta.x + (tb.x - ta.x) * shown;
	if (kx - ta.x > 1.0f) dl->AddRectFilled(ta, ImVec2(kx, tb.y), Col(pal::Accent2), 1.5f);
	float kr = 5.2f + hover * 1.0f;
	dl->AddCircleFilled(ImVec2(kx, cy), kr + 1.5f, Col(IM_COL32(14, 12, 16, 230)), 28);
	dl->AddCircleFilled(ImVec2(kx, cy), kr, Col(IM_COL32(244, 236, 224, 255)), 28);
	return changed;
}

static bool SliderRow(const char *id, float *t, const char *value, float w)
{
	ImGui::PushFont(UiFont(g_fonts.mono));
	const float vw = 56.0f;
	ImVec2 p = ImGui::GetCursorScreenPos();
	bool changed = SliderBehavior(id, t, w - vw);
	ImVec2 ts = ImGui::CalcTextSize(value);
	ImGui::GetWindowDrawList()->AddText(Snap(ImVec2(p.x + w - ts.x, p.y + (20 - ts.y) * 0.5f)), Col(pal::Text2), value);
	ImGui::PopFont();
	return changed;
}

bool SliderInt(const char *id, int *v, int min, int max, const char *fmt, float width)
{
	float t = max > min ? float(*v - min) / (max - min) : 0;
	char value[32];
	sprintf_s(value, fmt, *v);
	if (!SliderRow(id, &t, value, width)) return false;
	int nv = min + (int)(t * (max - min) + 0.5f);
	if (nv == *v) return false;
	*v = nv;
	return true;
}

bool SliderFloat(const char *id, float *v, float min, float max, const char *fmt, float width)
{
	float t = max > min ? (*v - min) / (max - min) : 0;
	char value[32];
	sprintf_s(value, fmt, *v);
	if (!SliderRow(id, &t, value, width)) return false;
	float nv = floorf(min + t * (max - min) + 0.5f);
	if (nv == *v) return false;
	*v = nv;
	return true;
}

void Bar(ImVec2 size, float frac)
{
	ImVec2 p = Snap(ImGui::GetCursorScreenPos());
	ImDrawList *dl = ImGui::GetWindowDrawList();
	float r = size.y * 0.5f;
	dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), Col(IM_COL32(218, 198, 168, 22)), r);
	frac = ImClamp(frac, 0.0f, 1.0f);
	ImU32 fill = frac >= 0.85f ? pal::Accent : pal::Accent2;
	if (frac > 0.002f) dl->AddRectFilled(p, ImVec2(p.x + ImMax(size.y, size.x * frac), p.y + size.y), Col(fill), r);
	ImGui::Dummy(size);
}

void Sparkline(ImVec2 size)
{
	ImVec2 p = Snap(ImGui::GetCursorScreenPos());
	ImDrawList *dl = ImGui::GetWindowDrawList();
	ImVec2 b(p.x + size.x, p.y + size.y);
	ImGui::Dummy(size);
	int shown = ImMin(g_frames.count, FrameStats::kHistory);
	if (shown < 2) return;

	float peak = 0;
	for (int i = 1; i <= shown; i++) peak = ImMax(peak, g_frames.frameMs[(g_frames.head - i + FrameStats::kHistory) % FrameStats::kHistory]);
	float top = Anim("spark_top", ImMax(peak * 1.2f, 8.0f), 4.0f);

	for (int g = 1; g <= 2; g++) {
		float y = (float)(int)(p.y + size.y * g / 3.0f) + 0.5f;
		dl->AddLine(ImVec2(p.x, y), ImVec2(b.x, y), Col(IM_COL32(218, 198, 168, 14)));
	}
	ImVec2 pts[FrameStats::kHistory];
	for (int i = 0; i < shown; i++) {
		float ms = g_frames.frameMs[(g_frames.head - shown + i + FrameStats::kHistory) % FrameStats::kHistory];
		pts[i] = ImVec2(p.x + size.x * i / (shown - 1), b.y - 1 - (size.y - 2) * ImClamp(ms / top, 0.0f, 1.0f));
	}
	int start = dl->VtxBuffer.Size;
	ImDrawListFlags flags = dl->Flags;
	dl->Flags &= ~ImDrawListFlags_AntiAliasedFill;
	for (int i = 0; i + 1 < shown; i++)
		dl->AddQuadFilled(pts[i], pts[i + 1], ImVec2(pts[i + 1].x, b.y), ImVec2(pts[i].x, b.y), IM_COL32_WHITE);
	dl->Flags = flags;
	ShadeAdded(dl, start, ImVec2(p.x, p.y), ImVec2(p.x, b.y), Alpha(pal::Accent, 0.22f), Alpha(pal::Accent, 0.0f));
	dl->AddPolyline(pts, shown, Col(pal::Accent2), 0, 1.35f);
}

void TextColored(ImU32 col, const char *text)
{
	ImGui::PushStyleColor(ImGuiCol_Text, col);
	ImGui::TextUnformatted(text);
	ImGui::PopStyleColor();
}

void TextWrapped(ImU32 col, const char *text, float width)
{
	ImGui::PushStyleColor(ImGuiCol_Text, col);
	ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
	ImGui::TextUnformatted(text);
	ImGui::PopTextWrapPos();
	ImGui::PopStyleColor();
}

void Tooltip(const char *title, const char *text)
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 12));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
	if (ImGui::BeginTooltip()) {
		if (title) {
			ImGui::PushFont(UiFont(g_fonts.medium));
			ImGui::TextUnformatted(title);
			ImGui::PopFont();
		}
		ImGui::PushFont(UiFont(g_fonts.body));
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 400);
		ImGui::PushStyleColor(ImGuiCol_Text, pal::Text2);
		ImGui::TextUnformatted(text);
		ImGui::PopStyleColor();
		ImGui::PopTextWrapPos();
		ImGui::PopFont();
		ImGui::EndTooltip();
	}
	ImGui::PopStyleVar(2);
}

bool SearchBox(const char *id, char *buf, int size, float width, const char *hint)
{
	ImVec2 p = Snap(ImGui::GetCursorScreenPos());
	const float h = 32;
	ImDrawList *dl = ImGui::GetWindowDrawList();
	ImGuiID gid = ImGui::GetID(id);
	float focus = Anim(gid + 7, ImGui::GetFocusID() == gid ? 1.0f : 0.0f, 16.0f);
	dl->AddRectFilled(p, ImVec2(p.x + width, p.y + h), Col(IM_COL32(20, 17, 23, 230)), 6.0f);
	dl->AddRect(ImVec2(p.x + 0.5f, p.y + 0.5f), ImVec2(p.x + width - 0.5f, p.y + h - 0.5f),
	            Col(Mix(pal::Line, Alpha(pal::Accent2, 0.65f), focus)), 6.0f);
	Icon(dl, ImVec2(p.x + 15, p.y + h * 0.5f), ICON_SEARCH, g_fonts.icon14, Mix(pal::Text3, pal::Accent2, focus));
	ImGui::SetCursorScreenPos(ImVec2(p.x + 28, p.y + (h - ImGui::GetFontSize()) * 0.5f - 3));
	ImGui::PushFont(UiFont(g_fonts.body));
	ImGui::PushStyleColor(ImGuiCol_FrameBg, 0);
	ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, 0);
	ImGui::PushStyleColor(ImGuiCol_FrameBgActive, 0);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 3));
	ImGui::SetNextItemWidth(width - 36);
	bool changed = ImGui::InputTextWithHint(id, hint, buf, size);
	ImGui::PopStyleVar();
	ImGui::PopStyleColor(3);
	ImGui::PopFont();
	ImGui::SetCursorScreenPos(p);
	ImGui::Dummy(ImVec2(width, h));
	return changed;
}

bool MenuItem(const char *icon, const char *label)
{
	ImVec2 p = ImGui::GetCursorScreenPos();
	const float w = 230, h = 30;
	ImGui::PushID(icon ? icon : label);
	bool pressed = ImGui::InvisibleButton("##mi", ImVec2(w, h));
	bool hover = ImGui::IsItemHovered();
	ImGui::PopID();
	ImDrawList *dl = ImGui::GetWindowDrawList();
	if (hover) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), Col(Alpha(pal::Accent, 0.20f)), 5.0f);
	if (icon) Icon(dl, ImVec2(p.x + 15, p.y + h * 0.5f), icon, g_fonts.icon14, hover ? pal::Accent2 : pal::Text3);
	ImGui::PushFont(UiFont(g_fonts.body));
	dl->AddText(Snap(ImVec2(p.x + 32, p.y + (h - ImGui::GetFontSize()) * 0.5f - 1)), Col(hover ? pal::Text : pal::Text2), label);
	ImGui::PopFont();
	if (pressed) ImGui::CloseCurrentPopup();
	return pressed;
}

static char g_toast[96];
static double g_toastAt = -100;

void ShowToast(const char *text)
{
	strcpy_s(g_toast, text);
	g_toastAt = ImGui::GetTime();
}

void DrawToast(ImVec2 a, ImVec2 b)
{
	float age = float(ImGui::GetTime() - g_toastAt);
	if (age > 1.8f) return;
	float in = EaseOut(age / 0.16f), out = age > 1.5f ? 1 - (age - 1.5f) / 0.3f : 1;
	float k = in * out;
	ImDrawList *dl = ImGui::GetForegroundDrawList();
	ImGui::PushFont(UiFont(g_fonts.medium));
	ImVec2 ts = ImGui::CalcTextSize(g_toast);
	ImVec2 size(ts.x + 48, 36);
	ImVec2 p = Snap(ImVec2((a.x + b.x - size.x) * 0.5f, b.y - 54));
	dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), Alpha(IM_COL32(26, 22, 30, 248), k), 7.0f);
	dl->AddRect(ImVec2(p.x + 0.5f, p.y + 0.5f), ImVec2(p.x + size.x - 0.5f, p.y + size.y - 0.5f), Alpha(Alpha(pal::Accent2, 0.45f), k), 7.0f);
	Icon(dl, ImVec2(p.x + 20, p.y + 18), ICON_CIRCLE_CHECK, g_fonts.icon14, Alpha(pal::Ok, k / ImMax(0.01f, ImGui::GetStyle().Alpha)));
	dl->AddText(Snap(ImVec2(p.x + 36, p.y + (size.y - ts.y) * 0.5f)), Alpha(pal::Text, k), g_toast);
	ImGui::PopFont();
}
