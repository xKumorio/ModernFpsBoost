// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// The menu: one window, a sidebar (sections + performance monitor) and the section pages.

#include <windows.h>
#include <math.h>
#include <shellapi.h>
#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"

#include "../core/game.h"
#include "../core/ini.h"
#include "../core/plugin.h"
#include "../fixes/registry.h"
#include "../profilers/profilers.h"
#include "icons.h"
#include "theme.h"
#include "ui.h"

enum Tab { TAB_FIXES, TAB_LUA, TAB_PROF, TAB_LAG, TAB_LOGS, TAB_SETTINGS, TAB_COUNT };

struct TabInfo { const char *icon, *nameRu, *nameEn, *subRu, *subEn; };
static const TabInfo kTabs[TAB_COUNT] = {
	{ ICON_ZAP, "Оптимизации", "Optimizations", "Что ускоряет каждый фикс. Нажми на строку — подробности и настройки.", "What each fix speeds up. Click a row for details and settings." },
	{ ICON_BRACES, "Lua профайлер", "Lua profiler", "Сколько кадра забирает каждый скрипт MoonLoader, его функции и строки кода. ПКМ — копировать.", "Frame time spent by each MoonLoader script, its functions, and lines of code. Right-click to copy." },
	{ ICON_ACTIVITY, "Профайлер", "Profiler", "Весь кадр одним деревом: GTA и SA-MP по реальным вызовам. Раскрой строку — что внутри; «своё» — без размеченных вызовов. ПКМ — копировать.", "Whole frame in one tree: GTA and SA-MP by real calls. Expand a row to see inside; 'self' excludes child timers. Right-click to copy." },
	{ ICON_TIMER, "Фризы и инпутлаг", "Stutters & input lag", "Каждый долгий кадр и на что он ушёл, 1% и 0.1% low, задержка от клавиш и мыши до кадра. Считает, пока меню закрыто.", "Every long frame and its cause, 1% and 0.1% lows, key/mouse-to-frame latency. Records while the menu is closed." },
	{ ICON_SCROLL_TEXT, "Журнал", "Log", "Что плагин сделал при запуске игры и что не встало.", "What the plugin patched on startup and anything that was skipped." },
	{ ICON_SETTINGS, "Настройки", "Settings", "Язык, клавиша меню, фон и счётчик FPS.", "Language, menu key, background, and FPS counter." },
};

struct FixIcon { const char *section, *icon; };
static const FixIcon kFixIcons[] = {
	{ "TextCache", ICON_TYPE }, { "ColPlaneCache", ICON_BOX }, { "RemotePedPhysics", ICON_PERSON_STANDING },
	{ "RemoteInAir", ICON_ARROW_DOWN_TO_LINE }, { "RefPool", ICON_LINK },
	{ "PedDrawDistance", ICON_EYE_OFF }, { "VehicleLod", ICON_CAR }, { "DrawDistance", ICON_BUILDING_2 }, { "StreamBudget", ICON_LAYERS }, { "StreamMemBoost", ICON_CPU }, { "ModelNameLookup", ICON_SEARCH }, { "SampModelWait", ICON_BOX }, { "SampObjectCull", ICON_EYE_OFF }, { "MatrixSSE", ICON_GRID_3X3 }, { "CMatrixSSE", ICON_GRID_3X3 }, { "OcclusionOff", ICON_BUILDING_2 }, { "FtolSSE3", ICON_CALCULATOR }, { "FloorSSE4", ICON_CALCULATOR }, { "FrustumSSE", ICON_SCAN_EYE }, { "ColTriSSE", ICON_CUBOID }, { "ColBoxSSE", ICON_BOX }, { "ColLinesSSE", ICON_BOX }, { "FastAtan2SSE", ICON_CALCULATOR }, { "FastStricmpSSE", ICON_SEARCH }, { "MsgKeysFast", ICON_MESSAGE_SQUARE_TEXT }, { "FxParticlesSSE", ICON_SPARKLES }, { "EnvMapCache", ICON_SPARKLES }, { "CursorCenterFix", ICON_GAUGE }, { "MessagePumpFix", ICON_GAUGE },
	{ "AnimMathSSE", ICON_BONE }, { "AnimBlendSSE", ICON_BONE }, { "FrameSyncFast", ICON_GRID_3X3 }, { "SkinMatrixSSE", ICON_BONE }, { "NametagLOSCache", ICON_SCAN_EYE }, { "ObjectSkip", ICON_BUILDING_2 },
	{ "PlayerLoop", ICON_REPEAT }, { "PlayerTagState", ICON_HEART }, { "SampStateBlockFix", ICON_LAYERS }, { "HealthBarBatch", ICON_HEART }, { "ObjectDrawLastFast", ICON_LAYERS }, { "ObjectMatTextFast", ICON_SIGNPOST }, { "SampKeyPollCache", ICON_GAUGE }, { "ObjectFindCache", ICON_SEARCH }, { "UnprotectCache", ICON_SHIELD_OFF }, { "LuaMemProtectCache", ICON_SHIELD_OFF }, { "AsiMemProtectCache", ICON_SHIELD_OFF }, { "SilentPatchObjectFix", ICON_PLUG_ZAP },
	{ "ObjectPrefetch", ICON_TAG }, { "SampfuncsDrawFast", ICON_PLUG_ZAP }, { "SearchLightCull", ICON_EYE_OFF }, { "ColLoadedCache", ICON_LAYERS },
	{ "SampTextMemset", ICON_MESSAGE_SQUARE_TEXT }, { "AccessoryFindCache", ICON_GLASSES },
	{ "MaterialFontCache", ICON_SIGNPOST }, { "GangZoneCull", ICON_MAP }, { "RadarBatch", ICON_LAYERS }, { "AudioSleepFix", ICON_VOLUME_2 }, { "SkyboxLogOff", ICON_CLOUD_MOON },
};

static int g_tab;
static bool g_previewOpen; // tools/preview: rows opened for the screenshot
static double g_tabSince; // ImGui time of the last tab switch: pages fade in
static ImVec2 g_pos(-1, -1);
float g_menuAnim;

static const float kTitleH = 48.0f;
static const float kSidebarW = 242.0f;
static const float kPad = 16.0f;

// ---------------------------------------------------------------- helpers

static const char *FixIconOf(const Fix *f)
{
	for (const FixIcon &i : kFixIcons)
		if (!strcmp(i.section, f->section)) return i.icon;
	return ICON_PUZZLE;
}

// the fixes page lists them by how much they give, most first; unlisted ones go last
static const char *const kFixOrder[] = {
	"DrawDistance", "VehicleLod", "TextCache", "OcclusionOff", "MessagePumpFix", "StreamMemBoost", "RemotePedPhysics", "ColTriSSE", "ColBoxSSE", "CMatrixSSE", "SampfuncsDrawFast", "SilentPatchObjectFix", "FxParticlesSSE", "PlayerTagState", "SampStateBlockFix", "HealthBarBatch", "LuaMemProtectCache", "NametagLOSCache", "AnimBlendSSE", "ObjectDrawLastFast", "FrustumSSE", "FrameSyncFast", "ColLinesSSE", "SampKeyPollCache", "SkinMatrixSSE", "AsiMemProtectCache", "FastAtan2SSE", "ObjectMatTextFast", "FastStricmpSSE", "MsgKeysFast", "SearchLightCull", "SampObjectCull", "SampModelWait", "StreamBudget", "ModelNameLookup", "RemoteInAir", "RefPool", "PedDrawDistance", "ObjectSkip", "RadarBatch", "GangZoneCull",
	"MatrixSSE", "AnimMathSSE", "EnvMapCache", "CursorCenterFix", "PlayerLoop", "ColPlaneCache", "UnprotectCache", "ObjectFindCache", "AccessoryFindCache", "FtolSSE3", "FloorSSE4",
	"ObjectPrefetch", "SampTextMemset", "ColLoadedCache", "AudioSleepFix", "MaterialFontCache", "SkyboxLogOff",
};

static int FixRank(const Fix *f)
{
	for (int k = 0; k < (int)(sizeof(kFixOrder) / sizeof(kFixOrder[0])); k++)
		if (!strcmp(kFixOrder[k], f->section)) return k;
	return 1000;
}

static bool IsProfiler(const Fix *f) { return f == &g_fixLuaProfiler || f == &g_fixSampProfiler; }

enum Group { GROUP_ALL, GROUP_GTA, GROUP_SAMP, GROUP_OTHER };

static Group GroupOf(const Fix *f)
{
	if (f->gta) return GROUP_GTA;
	if (f == &g_fixSampfuncsDrawFast || f == &g_fixLuaMemProtectCache || f == &g_fixAsiMemProtectCache || f == &g_fixSilentPatchObjectFix || f == &g_fixAudioSleepFix || f == &g_fixSkyboxLogOff) return GROUP_OTHER;
	return GROUP_SAMP;
}

static float PageIn(float delay = 0.0f, float duration = 0.18f)
{
	return EaseOut(float((ImGui::GetTime() - g_tabSince - delay) / duration));
}

static void PageHeader(const TabInfo &t)
{
	ImGui::PushFont(g_fonts.title);
	TextColored(pal::Text, Tr(t.nameRu, t.nameEn));
	ImGui::PopFont();
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 4);
	TextWrapped(pal::Text2, Tr(t.subRu, t.subEn), ImGui::GetContentRegionAvail().x);
	ImGui::Dummy(ImVec2(0, 4));
}

static void Dot(ImVec2 c, ImU32 col, bool /*pulse*/)
{
	ImGui::GetWindowDrawList()->AddCircleFilled(Snap(c), 3.0f, Col(col), 16);
}

// where text of the current font looks centered: between the middles of capitals and of lowercase
static float TextMidY()
{
	ImFont *f = ImGui::GetFont();
	const ImFontGlyph *cap = f->FindGlyph(0x0417), *low = f->FindGlyph(0x0430); // З, а
	if (!cap || !low) return ImGui::GetTextLineHeight() * 0.5f;
	return ((cap->Y0 + cap->Y1) * 0.5f + (low->Y0 + low->Y1) * 0.5f) * 0.5f;
}

// an icon in front of text on the same line, centered on the text
static void InlineIcon(const char *icon, ImFont *font, ImU32 col, float size)
{
	ImVec2 p = ImGui::GetCursorScreenPos();
	ImGui::Dummy(ImVec2(size, ImGui::GetTextLineHeight()));
	Icon(ImGui::GetWindowDrawList(), ImVec2(p.x + size * 0.5f, p.y + TextMidY()), icon, font, col);
}

// "<prefix>Softik Inquisitor": the name is a link to the author's page
static void AuthorLink(ImVec2 p, const char *prefix, ImU32 col)
{
	ImDrawList *dl = ImGui::GetWindowDrawList();
	p = Snap(p);
	dl->AddText(p, Col(col), prefix);
	ImVec2 at(p.x + ImGui::CalcTextSize(prefix).x, p.y);
	ImVec2 sz = ImGui::CalcTextSize(PLUGIN_AUTHOR);
	ImVec2 back = ImGui::GetCursorScreenPos();
	ImGui::SetCursorScreenPos(at);
	ImGui::PushID(prefix);
	if (ImGui::InvisibleButton("##author", sz)) ShellExecuteA(nullptr, "open", PLUGIN_AUTHOR_URL, nullptr, nullptr, SW_SHOWNORMAL);
	bool hov = ImGui::IsItemHovered();
	float h = Anim(ImGui::GetItemID(), hov ? 1.0f : 0.0f, 16.0f);
	ImGui::PopID();
	ImGui::SetCursorScreenPos(back);
	if (hov) {
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
		ImGui::SetTooltip("%s", PLUGIN_AUTHOR_URL);
	}
	ImU32 c = Mix(pal::Text2, pal::Text, h);
	dl->AddText(at, Col(c), PLUGIN_AUTHOR);
	if (h > 0.01f) dl->AddLine(ImVec2(at.x, at.y + sz.y - 1.5f), ImVec2(at.x + sz.x, at.y + sz.y - 1.5f), Col(Alpha(pal::Text2, h * 0.6f)));
}

// text cut to width with an ellipsis (current font)
static std::string Fit(const char *text, float width)
{
	if (ImGui::CalcTextSize(text).x <= width) return text;
	std::string s = text;
	while (!s.empty()) {
		while (!s.empty() && ((unsigned char)s.back() & 0xC0) == 0x80) s.pop_back(); // whole UTF-8 characters
		if (!s.empty()) s.pop_back();
		if (ImGui::CalcTextSize((s + "…").c_str()).x <= width) return s + "…";
	}
	return "…";
}

static std::wstring LowerW(const char *utf8)
{
	wchar_t w[512];
	int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, 512);
	if (n <= 0) return L"";
	CharLowerBuffW(w, n);
	return w;
}

static bool Matches(const char *text, const std::wstring &needle)
{
	return needle.empty() || LowerW(text).find(needle) != std::wstring::npos;
}

// ---------------------------------------------------------------- title bar & sidebar

static void DrawTopBar(ImVec2 a, ImVec2 b, bool &open)
{
	ImDrawList *dl = ImGui::GetWindowDrawList();
	ImGui::SetCursorScreenPos(a);
	ImGui::SetNextItemAllowOverlap();
	ImGui::InvisibleButton("##drag", ImVec2(b.x - a.x - 48, kTitleH));
	if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0)) {
		g_pos.x += ImGui::GetIO().MouseDelta.x;
		g_pos.y += ImGui::GetIO().MouseDelta.y;
	}

	ImVec2 cp = Snap(ImVec2(b.x - 40, a.y + (kTitleH - 28) * 0.5f));
	ImGui::SetCursorScreenPos(cp);
	if (ImGui::InvisibleButton("##close", ImVec2(28, 28))) open = false;
	float hov = Anim("close_hover", ImGui::IsItemHovered() ? 1.0f : 0.0f, 16.0f);
	if (hov > 0.01f)
		dl->AddRectFilled(cp, ImVec2(cp.x + 28, cp.y + 28), Col(Alpha(IM_COL32(218, 198, 168, 22), hov)), 6.0f);
	Icon(dl, ImVec2(cp.x + 14, cp.y + 14), ICON_X, g_fonts.icon14, Mix(pal::Text3, pal::Text, hov));
}

static void DrawMonitor(ImVec2 a, ImVec2 b)
{
	ImDrawList *dl = ImGui::GetWindowDrawList();
	dl->AddLine(ImVec2(a.x, a.y + 0.5f), ImVec2(b.x, a.y + 0.5f), Col(pal::Line));

	float w = b.x - a.x - 32;
	ImGui::SetCursorScreenPos(Snap(ImVec2(a.x + 16, a.y + 14)));
	ImGui::BeginGroup();

	// Latched readable FPS readout (updates at calm overlay_refresh_ms cadence, never flickers)
	char fps[16];
	sprintf_s(fps, "%.0f", g_frames.dispFps);
	ImGui::PushFont(g_fonts.number);
	TextColored(pal::Text, fps);
	ImGui::PopFont();
	ImGui::SameLine(0, 6);
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 14);
	ImGui::PushFont(g_fonts.tiny);
	TextColored(pal::Text3, "FPS");
	ImGui::PopFont();

	ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2);
	Sparkline(ImVec2(w, 36));
	ImGui::Dummy(ImVec2(0, 4));

	auto row = [&](const char *k, const char *v, ImU32 vcol = pal::Text) {
		float y = ImGui::GetCursorPosY(), x = ImGui::GetCursorPosX();
		ImGui::PushFont(g_fonts.tiny);
		TextColored(pal::Text3, k);
		ImGui::PopFont();
		ImGui::PushFont(g_fonts.mono);
		ImGui::SetCursorPos(ImVec2(x + w - ImGui::CalcTextSize(v).x, y + 1));
		TextColored(vcol, v);
		ImGui::PopFont();
		ImGui::SetCursorPos(ImVec2(x, y + 21));
	};

	char buf[32];
	sprintf_s(buf, "%.2f ms", g_frames.dispMs);
	row(Tr("Кадр", "Frame"), buf, pal::Text2);
	sprintf_s(buf, "%.0f", g_frames.dispLow1);
	row("1% low", buf, pal::Text2);
	sprintf_s(buf, "%.0f%%", g_frames.sysCpu);
	row("CPU", buf, g_frames.sysCpu >= 85.0f ? pal::Warn : pal::Text);
	if (g_frames.gpu >= 0.0f) sprintf_s(buf, "%.0f%%", g_frames.gpu);
	else strcpy_s(buf, "—");
	row("GPU", buf, g_frames.gpu >= 90.0f ? pal::Warn : pal::Text);
	sprintf_s(buf, "%.0f%%", g_frames.cpu);
	row(Tr("Поток игры", "Game thread"), buf, g_frames.cpu >= 92.0f ? pal::Warn : pal::Text2);

	ImGui::Dummy(ImVec2(0, 2));
	Bar(ImVec2(w, 3), g_frames.cpu / 100.0f);
	ImGui::EndGroup();
}

static void DrawSidebar(ImVec2 a, ImVec2 b)
{
	ImDrawList *dl = ImGui::GetWindowDrawList();

	// Minimalist brand header at top of rail (no gradient sheen, no neon)
	ImGui::SetCursorScreenPos(a);
	ImGui::SetNextItemAllowOverlap();
	ImGui::InvisibleButton("##drag_rail", ImVec2(b.x - a.x, 60));
	if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0)) {
		g_pos.x += ImGui::GetIO().MouseDelta.x;
		g_pos.y += ImGui::GetIO().MouseDelta.y;
	}

	ImVec2 la = Snap(ImVec2(a.x + 16, a.y + 16)), lb(la.x + 28, la.y + 28);
	dl->AddRectFilled(la, lb, Col(IM_COL32(28, 24, 32, 245)), 6.0f);
	dl->AddRect(ImVec2(la.x + 0.5f, la.y + 0.5f), ImVec2(lb.x - 0.5f, lb.y - 0.5f), Col(Alpha(pal::Accent2, 0.36f)), 6.0f);
	Icon(dl, ImVec2(la.x + 14, la.y + 14), ICON_ZAP, g_fonts.icon14, pal::Accent);

	ImGui::PushFont(g_fonts.medium);
	dl->AddText(Snap(ImVec2(lb.x + 10, a.y + 13)), Col(pal::Text), PLUGIN_NAME);
	ImGui::PopFont();
	ImGui::PushFont(g_fonts.tiny);
	dl->AddText(Snap(ImVec2(lb.x + 10, a.y + 31)), Col(pal::Text3), "v" PLUGIN_VERSION);
	ImGui::PopFont();

	dl->AddLine(ImVec2(a.x, a.y + 60.5f), ImVec2(b.x, a.y + 60.5f), Col(pal::Line));

	// Navigation list: smoked velvet pill with antique gold hairline, ZERO left accent stripe
	const float itemH = 36.0f, gap = 3.0f, top = a.y + 72.0f;
	float barY = Anim("nav_bar", top + g_tab * (itemH + gap), 20.0f);
	ImVec2 sa = Snap(ImVec2(a.x + 10, barY)), sb = Snap(ImVec2(b.x - 10, barY + itemH));
	dl->AddRectFilled(sa, sb, Col(IM_COL32(36, 31, 41, 238)), 6.0f);
	dl->AddRect(ImVec2(sa.x + 0.5f, sa.y + 0.5f), ImVec2(sb.x - 0.5f, sb.y - 0.5f), Col(Alpha(pal::Accent2, 0.32f)), 6.0f);

	for (int i = 0; i < TAB_COUNT; i++) {
		ImVec2 p = Snap(ImVec2(a.x + 10, top + i * (itemH + gap)));
		ImGui::SetCursorScreenPos(p);
		ImGui::PushID(i);
		if (ImGui::InvisibleButton("nav", ImVec2(b.x - a.x - 20, itemH)) && g_tab != i) {
			g_tab = i;
			g_tabSince = ImGui::GetTime();
		}
		float hover = Anim(ImGui::GetItemID(), ImGui::IsItemHovered() && g_tab != i ? 1.0f : 0.0f, 16.0f);
		float active = Anim(ImGui::GetItemID() + 1, g_tab == i ? 1.0f : 0.0f, 16.0f);
		ImGui::PopID();
		if (hover > 0.01f) dl->AddRectFilled(p, ImVec2(b.x - 10, p.y + itemH), Col(Alpha(IM_COL32(218, 198, 168, 12), hover)), 6.0f);
		Icon(dl, ImVec2(p.x + 20, p.y + itemH * 0.5f), kTabs[i].icon, g_fonts.icon14, Mix(pal::Text3, pal::Accent2, ImMax(active, hover * 0.5f)));
		ImGui::PushFont(g_fonts.medium);
		dl->AddText(Snap(ImVec2(p.x + 38, p.y + (itemH - ImGui::GetFontSize()) * 0.5f - 1)), Col(Mix(Mix(pal::Text3, pal::Text2, hover), pal::Text, active)),
		            Tr(kTabs[i].nameRu, kTabs[i].nameEn));
		ImGui::PopFont();
	}

	DrawMonitor(ImVec2(a.x, b.y - 266), ImVec2(b.x, b.y - 34));
	dl->AddLine(ImVec2(a.x, b.y - 34.5f), ImVec2(b.x, b.y - 34.5f), Col(pal::Line));
	ImGui::PushFont(g_fonts.tiny);
	float cw = ImGui::CalcTextSize("by " PLUGIN_AUTHOR).x;
	AuthorLink(ImVec2(a.x + (b.x - a.x - cw) * 0.5f, b.y - 24), "by ", pal::Text3);
	ImGui::PopFont();
}

// ---------------------------------------------------------------- page: fixes

static int g_desired[128];
static bool g_desiredRead;
static int g_group;
static char g_fixSearch[64];

struct Status { const char *text; ImU32 col; bool pulse; };

static Status StatusOf(const Fix *f)
{
	if (f->enabled && f->active) return { Tr("работает", "active"), pal::Ok, true };
	if (f->enabled && f->gta && !IsGta10US()) return { Tr("нужна GTA SA 1.0 US", "needs GTA SA 1.0 US"), pal::Warn, false };
	if (f->enabled && !f->gta && f->atSamp && !g_samp)
		return { GetModuleHandleA("samp.dll") && g_sampVersion < 0 ? Tr("версия SA-MP не та", "unsupported SA-MP") : Tr("ждёт SA-MP", "waiting for SA-MP"), pal::Text3, false };
	if (f->enabled) return { Tr("не встал", "not applied"), pal::Warn, false };
	return { Tr("выключен", "disabled"), pal::Text3, false };
}

static void DrawParams(Fix *f, float w)
{
	for (int k = 0; k < f->paramCount; k++) {
		const Param &pr = f->params[k];
		ImGui::PushID(k);
		ImGui::PushFont(g_fonts.tiny);
		TextColored(pal::Text2, ParamLabel(f, pr));
		if (!pr.live) {
			ImGui::SameLine(0, 6);
			TextColored(pal::Warn, Tr("· после перезапуска", "· after restart"));
		}
		ImGui::PopFont();
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2);
		if (pr.i) {
			SliderInt("##v", pr.i, (int)pr.min, (int)pr.max, "%d", w);
			if (ImGui::IsItemEdited() && f->changed) f->changed();
			if (ImGui::IsItemDeactivated()) g_ini.SetInt(f->section, pr.key, *pr.i);
		} else {
			SliderFloat("##v", pr.f, pr.min, pr.max, "%.0f", w);
			if (ImGui::IsItemEdited() && f->changed) f->changed();
			if (ImGui::IsItemDeactivated()) g_ini.SetFloat(f->section, pr.key, *pr.f);
		}
		ImGui::Dummy(ImVec2(0, 2));
		ImGui::PopID();
	}
}

static void DrawFixRow(Fix *f, int index, float w)
{
	const float headH = 70.0f;
	ImDrawList *dl = ImGui::GetWindowDrawList();
	ImGui::PushID(index);
	ImGuiStorage *st = ImGui::GetStateStorage();
	ImGuiID sid = ImGui::GetID("state");
	bool *open = st->GetBoolRef(sid, false);
	if (g_previewOpen && index == 1) *open = true;
	float *bodyH = st->GetFloatRef(sid + 1, 200.0f);
	float t = EaseOut(Anim(sid + 2, *open ? 1.0f : 0.0f, 12.0f));
	float h = headH + t * *bodyH;

	ImVec2 p = Snap(ImGui::GetCursorScreenPos());
	ImVec2 b(p.x + w, (float)(int)(p.y + h));
	bool rowHover = ImGui::IsMouseHoveringRect(p, ImVec2(b.x, p.y + headH)) && ImGui::IsWindowHovered();
	float hover = Anim(sid + 3, rowHover ? 1.0f : 0.0f, 14.0f);
	Surface(dl, p, b, 8.0f, Mix(pal::Surface, pal::SurfaceHover, ImMax(hover, t)), Mix(pal::Line, Alpha(pal::Accent2, 0.36f), ImMax(hover * 0.7f, t)));

	bool desired = g_desired[index] != 0;
	float on = Anim(sid + 4, desired ? 1.0f : 0.0f, 12.0f);
	IconTile(dl, ImVec2(p.x + 14, p.y + (headH - 34) * 0.5f), 34, FixIconOf(f), g_fonts.icon14, Mix(pal::Text3, pal::Accent2, on));

	// right cluster, from the right: toggle, chevron, status, gain
	const float toggleX = b.x - 16 - 36;
	const float chevX = toggleX - 24;
	const float cy = p.y + headH * 0.5f;
	Status stt = StatusOf(f);
	ImGui::PushFont(g_fonts.tiny);
	bool pending = desired != f->enabled;
	const char *statusText = pending ? (desired ? Tr("включится после перезапуска", "enables on restart") : Tr("выключится после перезапуска", "disables on restart")) : stt.text;
	ImU32 statusCol = pending ? pal::Warn : stt.col;
	float sw = ImGui::CalcTextSize(statusText).x;
	float sx = chevX - 14 - sw;
	Dot(ImVec2(sx - 10, cy), statusCol, false);
	dl->AddText(Snap(ImVec2(sx, cy - ImGui::GetFontSize() * 0.5f - 1)), Col(statusCol), statusText);
	float gx = sx - 26;
	const char *gain = FixGain(f);
	if (gain[0] && w > 640) {
		ImGui::PushFont(g_fonts.mono);
		ImVec2 gs = ImGui::CalcTextSize(gain);
		ImVec2 ga = Snap(ImVec2(gx - gs.x - 16, cy - 11)), gb(gx, (float)(int)(cy + 11));
		dl->AddRectFilled(ga, gb, Col(Alpha(pal::Accent2, 0.11f)), 5.0f);
		dl->AddRect(ImVec2(ga.x + 0.5f, ga.y + 0.5f), ImVec2(gb.x - 0.5f, gb.y - 0.5f), Col(Alpha(pal::Accent2, 0.28f)), 5.0f);
		dl->AddText(Snap(ImVec2(ga.x + 8, cy - gs.y * 0.5f)), Col(pal::Accent2), gain);
		ImGui::PopFont();
		gx = ga.x;
	}
	ImGui::PopFont();
	Icon(dl, ImVec2(chevX + 8, cy), t > 0.5f ? ICON_CHEVRON_DOWN : ICON_CHEVRON_RIGHT, g_fonts.icon14, Mix(pal::Text3, pal::Text, ImMax(hover, t)));

	// title + one line of summary
	float textX = p.x + 60, textW = gx - 16 - textX;
	const char *title = FixTitle(f);
	const char *summary = FixSummary(f);
	ImGui::PushFont(g_fonts.medium);
	dl->AddText(Snap(ImVec2(textX, p.y + 13)), Col(pal::Text), Fit(title, textW).c_str());
	ImGui::PopFont();
	ImGui::PushFont(g_fonts.tiny);
	dl->AddText(Snap(ImVec2(textX, p.y + 38)), Col(pal::Text3), Fit(summary, textW).c_str());
	ImGui::PopFont();

	ImGui::SetCursorScreenPos(p);
	if (ImGui::InvisibleButton("##head", ImVec2(toggleX - 8 - p.x, headH))) *open = !*open;
	ImGui::SetCursorScreenPos(ImVec2(toggleX, (float)(int)(cy - 10)));
	bool v = desired;
	if (Toggle("##on", &v)) {
		g_desired[index] = v;
		g_ini.SetInt(f->section, "enabled", v ? 1 : 0);
	}

	// details, clipped to the animated height
	if (t > 0.002f) {
		ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + headH));
		ImGui::BeginChild("##body", ImVec2(w, ImMax(1.0f, t * *bodyH)), ImGuiChildFlags_None,
		                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
		ImDrawList *cdl = ImGui::GetWindowDrawList();
		ImVec2 c = ImGui::GetCursorScreenPos();
		cdl->AddLine(ImVec2(c.x + 16, c.y + 0.5f), ImVec2(c.x + w - 16, c.y + 0.5f), Col(pal::Line));
		float bw = w - 66 - 24;
		ImGui::SetCursorScreenPos(ImVec2(c.x + 66, c.y + 14));
		ImGui::BeginGroup();
		TextWrapped(pal::Text, FixSummary(f), bw);
		ImGui::Dummy(ImVec2(0, 2));
		TextWrapped(pal::Text2, FixDetails(f), bw);
		if (f->paramCount) {
			ImGui::Dummy(ImVec2(0, 6));
			DrawParams(f, ImMin(bw, 440.0f));
		}
		ImGui::Dummy(ImVec2(0, 2));
		ImGui::PushFont(g_fonts.tiny);
		char where[96];
		sprintf_s(where, Tr("[%s] в " PLUGIN_NAME ".ini", "[%s] in " PLUGIN_NAME ".ini"), f->section);
		TextColored(pal::Text3, where);
		ImGui::PopFont();
		ImGui::EndGroup();
		*bodyH = ImGui::GetItemRectSize().y + 30;
		ImGui::EndChild();
	}
	ImGui::SetCursorScreenPos(ImVec2(p.x, b.y + 10));
	ImGui::Dummy(ImVec2(w, 0));
	ImGui::PopID();
}

static void DrawFixesPage()
{
	int n;
	Fix *const *fixes = FixList(n);
	if (!g_desiredRead) {
		g_desiredRead = true;
		for (int i = 0; i < n && i < 128; i++) g_desired[i] = g_ini.Bool(fixes[i]->section, "enabled", fixes[i]->defaultOn);
	}

	PageHeader(kTabs[TAB_FIXES]);
	float full = ImGui::GetContentRegionAvail().x;
	float x0 = ImGui::GetCursorPosX(), y0 = ImGui::GetCursorPosY();
	static const char *const groupsRu[] = { "Все", "GTA", "SA-MP", "Другие моды" };
	static const char *const groupsEn[] = { "All", "GTA", "SA-MP", "Other mods" };
	if (Segmented("groups", g_english ? groupsEn : groupsRu, 4, &g_group)) g_tabSince = ImGui::GetTime();
	ImGui::SetCursorPos(ImVec2(x0 + full - 260 - 10, y0));
	SearchBox("##fixsearch", g_fixSearch, sizeof(g_fixSearch), 260, Tr("Поиск фикса", "Search fixes"));
	ImGui::SetCursorPos(ImVec2(x0, y0 + 34 + 10));

	int total = 0, working = 0;
	for (int i = 0; i < n; i++)
		if (!IsProfiler(fixes[i])) total++, working += fixes[i]->enabled && fixes[i]->active;
	ImGui::PushFont(g_fonts.tiny);
	ImVec2 dp = ImGui::GetCursorScreenPos();
	Dot(ImVec2(dp.x + 5, dp.y + ImGui::GetFontSize() * 0.5f + 1), working ? pal::Ok : pal::Warn, false);
	ImGui::SetCursorScreenPos(ImVec2(dp.x + 15, dp.y));
	char buf[160];
	sprintf_s(buf, Tr("Работают %d из %d   ·   GTA SA %s   ·   SA-MP %s", "Active %d of %d   ·   GTA SA %s   ·   SA-MP %s"),
	          working, total, IsGta10US() ? "1.0 US" : Tr("не 1.0 US", "not 1.0 US"),
	          SampVersionName() ? SampVersionName() : Tr("не загружен", "not loaded"));
	TextColored(pal::Text2, buf);
	ImGui::PopFont();
	ImGui::Dummy(ImVec2(0, 2));

	std::wstring needle = LowerW(g_fixSearch);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 4));
	ImGui::BeginChild("##fixes", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground);
	ImGui::PopStyleVar();
	float w = (float)(int)(ImGui::GetContentRegionAvail().x - 12);
	int shown = 0;
	std::vector<int> order(n);
	for (int i = 0; i < n; i++) order[i] = i;
	std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return FixRank(fixes[a]) < FixRank(fixes[b]); });
	for (int i : order) {
		Fix *f = fixes[i];
		if (IsProfiler(f) || (g_group != GROUP_ALL && GroupOf(f) != g_group)) continue;
		if (!Matches(FixTitle(f), needle) && !Matches(FixSummary(f), needle) && !Matches(f->section, needle)) continue;
		DrawFixRow(f, i, w);
		shown++;
	}
	if (!shown) TextColored(pal::Text3, Tr("Ничего не найдено.", "Nothing found."));

	const std::string &stats = FixesStats();
	if (!stats.empty()) {
		ImGui::Dummy(ImVec2(0, 6));
		ImGui::PushFont(g_fonts.medium);
		TextColored(pal::Text, Tr("Статистика за последнюю секунду", "Last second stats"));
		ImGui::PopFont();
		ImGui::PushFont(g_fonts.mono);
		TextWrapped(pal::Text2, stats.c_str(), w);
		ImGui::PopFont();
	}
	ImGui::Dummy(ImVec2(0, 8));
	ImGui::EndChild();
}

// ---------------------------------------------------------------- profiler tree

struct TreeNode {
	unsigned key = 0;   // stable between refreshes (open state)
	int parent = -1;    // node index
	std::string label;
	DWORD addr = 0;
	bool gta = false;
	double incl = 0, self = 0, calls = 0, ms = 0;
	std::vector<int> kids;
};

struct TreeView {
	std::vector<TreeNode> nodes;
	std::vector<int> roots;
	std::unordered_set<unsigned> open;
	char search[64] = "";
	bool paused = false;
	int ctx = -1;     // node of the context menu
	bool lua = false; // columns: ms/s instead of self % and address
	bool gtaTab = false; // rows of the other module get a tag
};

static TreeView g_profTree, g_luaTree;
static int g_luaView; // 0 scripts, 1 lines of code

static unsigned Hash(const char *s, unsigned h = 2166136261u)
{
	for (; *s; s++) h = (h ^ (unsigned char)*s) * 16777619u;
	return h;
}

static void SortTree(TreeView &v)
{
	auto byIncl = [&](int a, int b) { return v.nodes[a].incl > v.nodes[b].incl; };
	v.roots.clear();
	for (TreeNode &n : v.nodes) n.kids.clear();
	for (int i = 0; i < (int)v.nodes.size(); i++) {
		int p = v.nodes[i].parent;
		(p >= 0 ? v.nodes[p].kids : v.roots).push_back(i);
	}
	std::sort(v.roots.begin(), v.roots.end(), byIncl);
	for (TreeNode &n : v.nodes) std::sort(n.kids.begin(), n.kids.end(), byIncl);
}

static const char *CleanLabel(const char *label)
{
	return !strncmp(label, "GTA ", 4) ? label + 4 : label;
}

// The whole frame: every timed function, GTA or samp.dll, under the one it really ran inside; samp.dll rows are tagged.
static void BuildTimerTree(TreeView &v)
{
	v.gtaTab = true;
	if (v.paused) return;
	const std::vector<TimerRow> &rows = SampProfilerRows();
	v.nodes.clear();
	int maxId = 0;
	for (const TimerRow &r : rows) maxId = ImMax(maxId, r.id);
	std::vector<int> row(maxId + 1, -1);
	for (int k = 0; k < (int)rows.size(); k++) row[rows[k].id] = k;
	for (const TimerRow &r : rows) {
		TreeNode n;
		n.key = (unsigned)r.id * 2654435761u + 1;
		n.label = CleanLabel(r.label);
		n.addr = r.addr;
		n.gta = r.gta;
		n.incl = r.incl;
		n.self = r.self;
		n.calls = r.calls;
		n.parent = r.parent >= 0 && r.parent <= maxId ? row[r.parent] : -1;
		v.nodes.push_back(n);
	}
	SortTree(v);
	if (g_previewOpen && v.open.empty())
		for (size_t r = 0; r < v.roots.size() && r < 2; r++) {
			v.open.insert(v.nodes[v.roots[r]].key);
			for (int kid : v.nodes[v.roots[r]].kids) v.open.insert(v.nodes[kid].key);
		}
}

static void BuildLuaTree(TreeView &v)
{
	v.lua = true;
	if (v.paused) return;
	double total;
	const std::vector<ProfRow> &rows = LuaProfilerRows(total);
	v.nodes.clear();
	for (const ProfRow &r : rows) {
		int parent = (int)v.nodes.size();
		TreeNode n;
		n.key = Hash(r.name.c_str());
		n.label = r.name;
		n.incl = r.pct;
		n.ms = r.ms;
		n.calls = r.calls;
		v.nodes.push_back(n);
		for (const ProfRow &part : r.parts) {
			TreeNode c;
			c.key = Hash(part.name.c_str(), n.key);
			c.label = part.name;
			c.parent = parent;
			c.incl = part.pct;
			c.ms = part.ms;
			c.calls = part.calls;
			v.nodes.push_back(c);
		}
	}
	SortTree(v);
}

static void NodeLine(const TreeView &v, int i, int depth, std::string &out)
{
	const TreeNode &n = v.nodes[i];
	out.append(depth * 2, ' ');
	if (!v.lua && n.gta != v.gtaTab) out += n.gta ? "[GTA] " : "[SA-MP] ";
	char buf[96];
	if (v.lua) sprintf_s(buf, "\t%.2f%%\t%.2f ms/s\t%.0f/s", n.incl, n.ms, n.calls);
	else sprintf_s(buf, "\t%.2f%%\tself %.2f%%\t%.0f/s\t%s%06lX", n.incl, n.self, n.calls, n.gta ? "" : "samp+", n.addr);
	out += n.label + buf + "\n";
}

static void CopyBranch(const TreeView &v, int i, int depth, std::string &out)
{
	NodeLine(v, i, depth, out);
	for (int k : v.nodes[i].kids) CopyBranch(v, k, depth + 1, out);
}

static void CopyAll(const TreeView &v)
{
	std::string out = v.lua ? Tr("Скрипт / функция\t% кадра\tмс/с\tвызовов/с\n", "Script / function\t% frame\tms/s\tcalls/s\n")
	                        : Tr("Функция\t% кадра\tсвоё\tвызовов/с\tадрес\n", "Function\t% frame\tself\tcalls/s\taddress\n");
	for (int r : v.roots) CopyBranch(v, r, 0, out);
	ImGui::SetClipboardText(out.c_str());
	ShowToast(Tr("Таблица скопирована", "Table copied"));
}

static void PctCell(float x, float y, float w, float h, double pct, double maxPct)
{
	ImDrawList *dl = ImGui::GetWindowDrawList();
	char t[16];
	sprintf_s(t, "%.2f%%", pct);
	ImVec2 ts = ImGui::CalcTextSize(t);
	float numW = ImGui::CalcTextSize("00.00%").x;
	dl->AddText(Snap(ImVec2(x + numW - ts.x, y + (h - ts.y) * 0.5f - 1)), Col(pal::Text), t);
	float bx = x + numW + 12, bw = ImMax(10.0f, w - numW - 16);
	float frac = maxPct > 0 ? float(pct / maxPct) : 0;
	ImVec2 ba = Snap(ImVec2(bx, y + h * 0.5f - 1.5f));
	dl->AddRectFilled(ba, ImVec2(ba.x + bw, ba.y + 3), Col(IM_COL32(218, 198, 168, 22)), 1.5f);
	if (frac > 0.003f) dl->AddRectFilled(ba, ImVec2(ba.x + ImMax(3.0f, bw * frac), ba.y + 3), Col(pal::Accent2), 1.5f);
}

// keep[i]: the node or something below it matches the search
static bool MarkMatches(const TreeView &v, int i, const std::wstring &needle, std::vector<char> &keep)
{
	bool any = Matches(v.nodes[i].label.c_str(), needle);
	for (int k : v.nodes[i].kids) any |= MarkMatches(v, k, needle, keep);
	keep[i] = any;
	return any;
}

static void HeaderRow(int columns)
{
	ImGui::TableNextRow(ImGuiTableRowFlags_Headers, 34);
	for (int c = 0; c < columns; c++) {
		ImGui::TableSetColumnIndex(c);
		ImVec2 hp = ImGui::GetCursorScreenPos();
		ImGui::PushFont(g_fonts.tiny);
		ImGui::GetWindowDrawList()->AddText(Snap(ImVec2(hp.x, hp.y + 9)), Col(pal::Text3), ImGui::TableGetColumnName(c));
		ImGui::PopFont();
	}
}

static void DrawTreeRows(TreeView &v, int i, int depth, double maxPct, const std::vector<char> *keep, bool forceOpen, bool &wantCtx)
{
	TreeNode &n = v.nodes[i];
	if (keep && !(*keep)[i]) return;
	const float rowH = 36.0f;
	bool hasKids = false;
	for (int k : n.kids)
		if (!keep || (*keep)[k]) hasKids = true;
	bool isOpen = hasKids && (forceOpen || v.open.count(n.key));

	ImGui::TableNextRow(ImGuiTableRowFlags_None, rowH);
	ImGui::TableNextColumn();
	ImGui::PushID(i); // the row index: two names can hash to the same key
	ImVec2 c0 = ImGui::GetCursorScreenPos();
	float cellW = ImGui::GetContentRegionAvail().x;
	bool selected = v.ctx == i && ImGui::IsPopupOpen("##treectx");
	ImGui::PushStyleColor(ImGuiCol_Header, Alpha(pal::Accent, 0.16f));
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(218, 198, 168, 14));
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(218, 198, 168, 24));
	if (ImGui::Selectable("##row", selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap, ImVec2(0, rowH)) && hasKids) {
		if (v.open.count(n.key)) v.open.erase(n.key);
		else v.open.insert(n.key);
	}
	ImGui::PopStyleColor(3);
	if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
		v.ctx = i;
		wantCtx = true;
	}
	bool rowHovered = ImGui::IsItemHovered();
	ImGui::PopID();

	ImDrawList *dl = ImGui::GetWindowDrawList();
	float y = c0.y;
	float x = c0.x + depth * 20.0f;
	for (int d = 1; d <= depth; d++) // guide lines of the levels above
		dl->AddLine(ImVec2(c0.x + d * 20.0f - 11.5f, y), ImVec2(c0.x + d * 20.0f - 11.5f, y + rowH), Col(IM_COL32(218, 198, 168, 20)));
	if (hasKids) Icon(dl, ImVec2(x + 8, y + rowH * 0.5f), isOpen ? ICON_CHEVRON_DOWN : ICON_CHEVRON_RIGHT, g_fonts.icon14, isOpen ? pal::Accent : pal::Text3);
	float lx = x + 22;
	if (!v.lua && n.gta != v.gtaTab) { // a samp.dll function
		const char *tag = n.gta ? "GTA" : "SA-MP";
		ImGui::PushFont(g_fonts.tiny);
		ImVec2 ts = ImGui::CalcTextSize(tag);
		ImVec2 ta = Snap(ImVec2(lx, y + rowH * 0.5f - 10)), tb(ta.x + ts.x + 12, ta.y + 20);
		ImU32 tc = n.gta ? pal::Accent2 : pal::Accent;
		dl->AddRectFilled(ta, tb, Col(Alpha(tc, 0.16f)), 6);
		dl->AddText(Snap(ImVec2(ta.x + 6, y + (rowH - ts.y) * 0.5f - 1)), Col(Mix(tc, pal::Text, 0.35f)), tag);
		ImGui::PopFont();
		lx = tb.x + 8;
	}
	ImGui::PushFont(depth ? g_fonts.body : g_fonts.medium);
	std::string label = Fit(n.label.c_str(), c0.x + cellW - lx - 4);
	dl->AddText(Snap(ImVec2(lx, y + (rowH - ImGui::GetFontSize()) * 0.5f - 1)), Col(depth ? pal::Text2 : pal::Text), label.c_str());
	ImGui::PopFont();
	if (rowHovered && label.size() != n.label.size()) ImGui::SetTooltip("%s", n.label.c_str());

	auto cell = [&](const char *text, ImU32 col, bool mono) {
		ImGui::TableNextColumn();
		ImVec2 p = ImGui::GetCursorScreenPos();
		ImGui::PushFont(mono ? g_fonts.mono : g_fonts.body);
		dl->AddText(Snap(ImVec2(p.x, y + (rowH - ImGui::GetFontSize()) * 0.5f - (mono ? 0 : 1))), Col(col), text);
		ImGui::PopFont();
	};
	ImGui::TableNextColumn();
	ImVec2 pc = ImGui::GetCursorScreenPos();
	PctCell(pc.x, y, ImGui::GetContentRegionAvail().x, rowH, n.incl, maxPct);
	char buf[32];
	if (v.lua) {
		sprintf_s(buf, "%.2f", n.ms);
		cell(buf, pal::Text2, false);
	} else {
		sprintf_s(buf, "%.2f%%", n.self);
		cell(buf, n.self >= 1.0 ? pal::Text : pal::Text2, false);
	}
	sprintf_s(buf, "%.0f", n.calls);
	cell(buf, pal::Text2, false);
	if (!v.lua) {
		sprintf_s(buf, n.gta ? "%06lX" : "+%05lX", n.addr);
		cell(buf, pal::Text3, true);
	}
	if (isOpen)
		for (int k : n.kids) DrawTreeRows(v, k, depth + 1, maxPct, keep, forceOpen, wantCtx);
}

static void DrawTree(TreeView &v, const char *id, float height)
{
	ImVec2 p = ImGui::GetCursorScreenPos();
	float w = ImGui::GetContentRegionAvail().x;
	Surface(ImGui::GetWindowDrawList(), p, ImVec2(p.x + w, p.y + height), 12, pal::Panel);
	if (v.nodes.empty()) {
		ImGui::SetCursorScreenPos(ImVec2(p.x + 20, p.y + 20));
		TextColored(pal::Text3, Tr("Пока пусто: данные появятся через секунду после запуска.", "Empty for now: data appears a second after starting."));
		ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + height));
		ImGui::Dummy(ImVec2(w, 0));
		return;
	}
	std::wstring needle = LowerW(v.search);
	std::vector<char> keep;
	if (!needle.empty()) {
		keep.assign(v.nodes.size(), 0);
		for (int r : v.roots) MarkMatches(v, r, needle, keep);
	}
	double maxPct = 0.01;
	for (int r : v.roots) maxPct = ImMax(maxPct, v.nodes[r].incl);

	const int columns = v.lua ? 4 : 5;
	ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(12, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 0));
	ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, IM_COL32(218, 198, 168, 6));
	bool wantCtx = false;
	if (ImGui::BeginTable(id, columns, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_PadOuterX, ImVec2(w, height))) {
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn(v.lua ? Tr("Скрипт / функция", "Script / function") : Tr("Функция", "Function"), ImGuiTableColumnFlags_WidthStretch, 3.0f);
		ImGui::TableSetupColumn(Tr("% кадра", "% frame"), ImGuiTableColumnFlags_WidthFixed, 200);
		ImGui::TableSetupColumn(v.lua ? Tr("мс/с", "ms/s") : Tr("своё", "self"), ImGuiTableColumnFlags_WidthFixed, 78);
		ImGui::TableSetupColumn(Tr("вызовов/с", "calls/s"), ImGuiTableColumnFlags_WidthFixed, 96);
		if (!v.lua) ImGui::TableSetupColumn(Tr("адрес", "address"), ImGuiTableColumnFlags_WidthFixed, 84);
		HeaderRow(columns);
		for (int r : v.roots) DrawTreeRows(v, r, 0, maxPct, keep.empty() ? nullptr : &keep, !needle.empty(), wantCtx);
		ImGui::EndTable();
	}
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(2);

	if (wantCtx) ImGui::OpenPopup("##treectx");
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 2));
	if (ImGui::BeginPopup("##treectx")) {
		if (v.ctx >= 0 && v.ctx < (int)v.nodes.size()) {
			const TreeNode &n = v.nodes[v.ctx];
			ImGui::PushFont(g_fonts.tiny);
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10);
			TextColored(pal::Text3, Fit(n.label.c_str(), 210).c_str());
			ImGui::PopFont();
			ImGui::Dummy(ImVec2(0, 2));
			std::string out;
			if (MenuItem(ICON_COPY, Tr("Копировать строку", "Copy row"))) {
				NodeLine(v, v.ctx, 0, out);
				ImGui::SetClipboardText(out.c_str());
				ShowToast(Tr("Строка скопирована", "Row copied"));
			}
			if (MenuItem(ICON_TYPE, Tr("Копировать название", "Copy name"))) {
				ImGui::SetClipboardText(n.label.c_str());
				ShowToast(Tr("Название скопировано", "Name copied"));
			}
			if (!v.lua && MenuItem(ICON_HASH, Tr("Копировать адрес", "Copy address"))) {
				char a[32];
				sprintf_s(a, n.gta ? "0x%06lX" : "samp.dll+0x%05lX", n.addr);
				ImGui::SetClipboardText(a);
				ShowToast(Tr("Адрес скопирован", "Address copied"));
			}
			if (!n.kids.empty() && MenuItem(ICON_LIST_TREE, Tr("Копировать с вложенными", "Copy with children"))) {
				CopyBranch(v, v.ctx, 0, out);
				ImGui::SetClipboardText(out.c_str());
				ShowToast(Tr("Ветка скопирована", "Branch copied"));
			}
			if (MenuItem(ICON_CLIPBOARD_COPY, Tr("Копировать всю таблицу", "Copy whole table"))) CopyAll(v);
			if (v.lua && n.parent < 0 && strncmp(n.label.c_str(), "[samp]", 6) && MenuItem(ICON_CODE, Tr("Какие строки кода едят", "Inspect hot code lines"))) {
				LuaLinesWatch(n.label.c_str());
				g_luaView = 1;
			}
		}
		ImGui::EndPopup();
	}
	ImGui::PopStyleVar(2);
}

static void AutostartToggle(Fix *autostart, float right, float y)
{
	const char *label = Tr("Запускать с игрой", "Start with game");
	float lw = ImGui::CalcTextSize(label).x;
	ImGui::SetCursorPos(ImVec2(right - 38 - 10 - lw, y + 7));
	TextColored(pal::Text2, label);
	ImGui::SetCursorPos(ImVec2(right - 38, y + 6));
	bool on = g_ini.Bool(autostart->section, "enabled", autostart->defaultOn);
	if (Toggle("##autostart", &on)) g_ini.SetInt(autostart->section, "enabled", on ? 1 : 0);
}

static void TreeToolbar(TreeView &v, Fix *autostart)
{
	float x0 = ImGui::GetCursorPosX(), y0 = ImGui::GetCursorPosY();
	float right = x0 + ImGui::GetContentRegionAvail().x - 6;
	SearchBox("##treesearch", v.search, sizeof(v.search), 240, Tr("Поиск функции", "Search function"));
	ImGui::SameLine(0, 8);
	bool anyOpen = !v.open.empty();
	if (Button("##expand", anyOpen ? ICON_CHEVRONS_DOWN_UP : ICON_CHEVRONS_UP_DOWN, anyOpen ? Tr("Свернуть", "Collapse") : Tr("Раскрыть", "Expand"), false, 128)) {
		if (anyOpen) v.open.clear();
		else
			for (TreeNode &n : v.nodes)
				if (!n.kids.empty()) v.open.insert(n.key);
	}
	ImGui::SameLine(0, 8);
	if (Button("##pause", v.paused ? ICON_PLAY : ICON_PAUSE, v.paused ? Tr("Продолжить", "Resume") : Tr("Пауза", "Pause"), v.paused, 132)) v.paused = !v.paused;
	ImGui::SameLine(0, 8);
	if (Button("##copyall", ICON_CLIPBOARD_COPY, Tr("Копировать", "Copy"), false)) CopyAll(v);
	AutostartToggle(autostart, right, y0);
	ImGui::SetCursorPos(ImVec2(x0, y0 + 34 + 10));
}

static void EmptyState(const char *icon, const char *title, const char *text, const char *button, bool busy, void (*start)(), Fix *autostart,
                       const char *warning)
{
	ImVec2 avail = ImGui::GetContentRegionAvail();
	ImVec2 p = ImGui::GetCursorScreenPos();
	float w = ImMin(avail.x, 540.0f);
	ImVec2 a = Snap(ImVec2(p.x + (avail.x - w) * 0.5f, p.y + 30));
	ImDrawList *dl = ImGui::GetWindowDrawList();
	float h = warning ? 322.0f : 292.0f;
	Surface(dl, a, ImVec2(a.x + w, a.y + h), 8, pal::Panel);
	IconTile(dl, ImVec2(a.x + (w - 52) * 0.5f, a.y + 28), 52, icon, g_fonts.icon28, pal::Text2);

	ImGui::SetCursorScreenPos(ImVec2(a.x + 36, a.y + 102));
	ImGui::BeginGroup();
	float iw = w - 72;
	ImGui::PushFont(g_fonts.title);
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (iw - ImGui::CalcTextSize(title).x) * 0.5f);
	TextColored(pal::Text, title);
	ImGui::PopFont();
	ImVec2 tsz = ImGui::CalcTextSize(text, nullptr, false, iw);
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImMax(0.0f, (iw - tsz.x) * 0.5f));
	TextWrapped(pal::Text2, text, iw);
	if (warning) {
		ImGui::PushFont(g_fonts.tiny);
		float ww = ImGui::CalcTextSize(warning).x + 22;
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (iw - ww) * 0.5f);
		InlineIcon(ICON_CIRCLE_ALERT, g_fonts.icon14, pal::Warn, 15);
		ImGui::SameLine(0, 7);
		TextColored(pal::Warn, warning);
		ImGui::PopFont();
	}
	ImGui::Dummy(ImVec2(0, 8));
	const float bw = 210;
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (iw - bw) * 0.5f);
	if (busy) {
		ImGui::PushFont(g_fonts.medium);
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 56);
		TextColored(pal::Text2, Tr("Запускается…", "Starting..."));
		ImGui::PopFont();
	} else if (button && Button("##start", ICON_PLAY, button, true, bw)) {
		start();
	}
	ImGui::Dummy(ImVec2(0, 8));
	const char *label = Tr("Запускать вместе с игрой", "Start with game");
	float lw = ImGui::CalcTextSize(label).x;
	float lx = ImGui::GetCursorPosX() + (iw - lw - 48) * 0.5f;
	float y = ImGui::GetCursorPosY();
	ImGui::SetCursorPos(ImVec2(lx, y));
	bool on = g_ini.Bool(autostart->section, "enabled", autostart->defaultOn);
	if (Toggle("##autostart", &on)) g_ini.SetInt(autostart->section, "enabled", on ? 1 : 0);
	ImGui::SetCursorPos(ImVec2(lx + 48, y + 1));
	TextColored(pal::Text2, label);
	ImGui::EndGroup();
	ImGui::SetCursorScreenPos(ImVec2(p.x, a.y + h + 10));
	ImGui::Dummy(ImVec2(1, 1));
}

// ---------------------------------------------------------------- Lua: lines of code

static int g_lineOpen = -1; // the line row opened (hash of path:line)

static void CodeLine(ImDrawList *dl, ImVec2 p, float w, int number, const std::string &code, bool hot, float h)
{
	if (hot) dl->AddRectFilled(ImVec2(p.x - 6, p.y), ImVec2(p.x + w, p.y + h), Col(Alpha(pal::Accent, 0.14f)), 4);
	char num[16];
	sprintf_s(num, "%5d", number);
	float ty = p.y + (h - ImGui::GetFontSize()) * 0.5f;
	dl->AddText(Snap(ImVec2(p.x, ty)), Col(hot ? pal::Accent : pal::Text3), num);
	float nx = p.x + ImGui::CalcTextSize("00000").x + 14;
	dl->AddText(Snap(ImVec2(nx, ty)), Col(hot ? pal::Text : pal::Text2), Fit(code.c_str(), p.x + w - nx - 6).c_str());
}

static std::string CodeAt(const LineRow &r, bool trim)
{
	const std::vector<std::string> &src = LuaSource(r.path);
	if (r.line < 1 || r.line > (int)src.size()) return trim ? Tr("(файл не прочитан)", "(file not read)") : "";
	const std::string &code = src[r.line - 1];
	if (!trim) return code;
	size_t lead = code.find_first_not_of(' ');
	return lead == std::string::npos ? std::string() : code.substr(lead);
}

static void ScriptPicker()
{
	const char *cur = LuaLinesScript();
	if (Button("##pick", ICON_FILE_CODE, cur ? cur : Tr("Выбрать скрипт", "Select script"), cur == nullptr, 300)) ImGui::OpenPopup("##scripts");
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 2));
	if (ImGui::BeginPopup("##scripts")) {
		int shown = 0;
		for (int r : g_luaTree.roots) {
			const TreeNode &n = g_luaTree.nodes[r];
			if (!strncmp(n.label.c_str(), "[samp]", 6) || !strncmp(n.label.c_str(), "state ", 6)) continue;
			char item[160];
			sprintf_s(item, "%s   %.2f%%", n.label.c_str(), n.incl);
			ImGui::PushID(r);
			if (MenuItem(ICON_BRACES, item)) LuaLinesWatch(n.label.c_str());
			ImGui::PopID();
			shown++;
		}
		if (!shown) TextColored(pal::Text3, Tr("Скрипты ещё не появились", "No scripts detected yet"));
		ImGui::EndPopup();
	}
	ImGui::PopStyleVar(2);
}

static void CopyLines(const std::vector<LineRow> &rows)
{
	std::string all = Tr("% кадра\t% скрипта\tфайл:строка\tкод\n", "% frame\t% script\tfile:line\tcode\n");
	for (const LineRow &q : rows) {
		char line[400];
		sprintf_s(line, "%.3f%%\t%.1f%%\t%s:%d\t", q.pct, 100 * q.share, q.file.c_str(), q.line);
		all += line + CodeAt(q, true) + "\n";
	}
	ImGui::SetClipboardText(all.c_str());
	ShowToast(Tr("Таблица скопирована", "Table copied"));
}

static void DrawLuaLines()
{
	double samples, secs;
	const std::vector<LineRow> &rows = LuaLinesRows(samples, secs);
	float y0 = ImGui::GetCursorPosY();
	ScriptPicker();
	if (LuaLinesScript()) {
		ImGui::SameLine(0, 8);
		if (Button("##lreset", ICON_REPEAT, Tr("Сначала", "Reset"), false)) LuaLinesReset();
		ImGui::SameLine(0, 8);
		if (Button("##lstop", ICON_PAUSE, Tr("Остановить", "Stop"), false)) LuaLinesWatch(nullptr);
		ImGui::SameLine(0, 16);
		ImGui::SetCursorPosY(y0 + 8);
		char info[128];
		sprintf_s(info, Tr("%.0f замеров за %.0f с", "%.0f samples in %.0f s"), samples, secs);
		ImGui::PushFont(g_fonts.tiny);
		if (samples < 200) TextColored(pal::Warn, Tr("Мало замеров — подожди, цифры уточнятся", "Few samples yet — wait for numbers to settle"));
		else TextColored(pal::Text3, info);
		ImGui::PopFont();
	}
	ImGui::SetCursorPosY(y0 + 34 + 10);
	float height = ImGui::GetContentRegionAvail().y - 2;

	ImVec2 p = ImGui::GetCursorScreenPos();
	float w = ImGui::GetContentRegionAvail().x;
	Surface(ImGui::GetWindowDrawList(), p, ImVec2(p.x + w, p.y + height), 12, pal::Panel);
	if (!LuaLinesScript() || rows.empty()) {
		ImGui::SetCursorScreenPos(ImVec2(p.x + 20, p.y + 18));
		ImGui::BeginGroup();
		TextWrapped(pal::Text2,
		            LuaLinesScript() ? Tr("Собираю замеры… строки появятся, когда скрипт поработает.",
		                                  "Collecting samples... lines will appear once the script runs.")
		                             : Tr("Выбери скрипт: раз в миллисекунду видно, какая строка его кода сейчас выполняется — так находятся "
		                                  "тяжёлые циклы и вызовы. Смотреть можно один скрипт за раз (так устроен LuaJIT).",
		                                  "Pick a script: every millisecond it samples which line of code is executing so you can spot heavy loops "
		                                  "and calls. One script can be watched at a time (LuaJIT limitation)."),
		            w - 40);
		ImGui::EndGroup();
		ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + height));
		ImGui::Dummy(ImVec2(w, 0));
		return;
	}
	double maxPct = 0.0001;
	for (const LineRow &r : rows) maxPct = ImMax(maxPct, r.pct);
	ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(12, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 0));
	ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, IM_COL32(218, 198, 168, 6));
	if (ImGui::BeginTable("##lines", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_PadOuterX, ImVec2(w, height))) {
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn(Tr("% кадра", "% frame"), ImGuiTableColumnFlags_WidthFixed, 190);
		ImGui::TableSetupColumn(Tr("скрипта", "script"), ImGuiTableColumnFlags_WidthFixed, 70);
		ImGui::TableSetupColumn(Tr("строка", "line"), ImGuiTableColumnFlags_WidthFixed, 160);
		ImGui::TableSetupColumn(Tr("код", "code"), ImGuiTableColumnFlags_WidthStretch);
		HeaderRow(4);
		const float rowH = 36;
		for (size_t k = 0; k < rows.size(); k++) {
			const LineRow &r = rows[k];
			if (r.share < 0.002 && k > 30) break;
			std::string code = CodeAt(r, true);
			int id = (int)Hash(r.path.c_str(), (unsigned)r.line);
			if (g_previewOpen && g_lineOpen == -1 && k == 1) g_lineOpen = id;
			bool open = g_lineOpen == id;

			ImGui::TableNextRow(0, rowH);
			ImGui::TableNextColumn();
			ImVec2 c = ImGui::GetCursorScreenPos();
			float y = c.y;
			ImGui::PushID(id);
			ImGui::PushStyleColor(ImGuiCol_Header, Alpha(pal::Accent, 0.16f));
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(218, 198, 168, 14));
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(218, 198, 168, 24));
			if (ImGui::Selectable("##line", open, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap, ImVec2(0, rowH)))
				g_lineOpen = open ? -1 : id;
			ImGui::PopStyleColor(3);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 2));
			if (ImGui::BeginPopupContextItem("##linectx")) {
				char where[300];
				sprintf_s(where, "%s:%d", r.file.c_str(), r.line);
				if (MenuItem(ICON_COPY, Tr("Копировать код строки", "Copy line code"))) {
					ImGui::SetClipboardText(code.c_str());
					ShowToast(Tr("Код скопирован", "Code copied"));
				}
				if (MenuItem(ICON_HASH, Tr("Копировать файл:строку", "Copy file:line"))) {
					ImGui::SetClipboardText(where);
					ShowToast(Tr("Скопировано", "Copied"));
				}
				if (MenuItem(ICON_CLIPBOARD_COPY, Tr("Копировать всю таблицу", "Copy whole table"))) CopyLines(rows);
				ImGui::EndPopup();
			}
			ImGui::PopStyleVar(2);
			ImGui::PopID();

			ImDrawList *dl = ImGui::GetWindowDrawList();
			PctCell(c.x, y, ImGui::GetContentRegionAvail().x, rowH, r.pct, maxPct);
			char buf[64];
			ImGui::TableNextColumn();
			c = ImGui::GetCursorScreenPos();
			sprintf_s(buf, "%.1f%%", 100 * r.share);
			dl->AddText(Snap(ImVec2(c.x, y + (rowH - ImGui::GetFontSize()) * 0.5f - 1)), Col(r.share >= 0.1 ? pal::Text : pal::Text2), buf);

			ImGui::TableNextColumn();
			c = ImGui::GetCursorScreenPos();
			float cw = ImGui::GetContentRegionAvail().x;
			ImGui::PushFont(g_fonts.mono);
			sprintf_s(buf, ":%d", r.line);
			float nw = ImGui::CalcTextSize(buf).x;
			float my = y + (rowH - ImGui::GetFontSize()) * 0.5f;
			ImGui::PushFont(g_fonts.tiny);
			std::string file = Fit(r.file.c_str(), cw - nw - 2);
			float fw = ImGui::CalcTextSize(file.c_str()).x;
			dl->AddText(Snap(ImVec2(c.x, y + (rowH - ImGui::GetFontSize()) * 0.5f - 1)), Col(pal::Text3), file.c_str());
			ImGui::PopFont();
			dl->AddText(Snap(ImVec2(c.x + fw + 1, my)), Col(pal::Accent), buf);
			ImGui::TableNextColumn();
			c = ImGui::GetCursorScreenPos();
			dl->AddText(Snap(ImVec2(c.x, my)), Col(pal::Text), Fit(code.c_str(), ImGui::GetContentRegionAvail().x - 4).c_str());
			ImGui::PopFont();

			if (open) { // the code around it
				const std::vector<std::string> &src = LuaSource(r.path);
				const int ctx = 4;
				const float lh = 24;
				int from = ImMax(1, r.line - ctx), to = ImMin((int)src.size(), r.line + ctx);
				int count = src.empty() ? 1 : to - from + 1;
				float bh = count * lh + 22;
				ImGui::TableNextRow(0, bh);
				ImGui::TableSetColumnIndex(3);
				float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
				ImGui::TableSetColumnIndex(0);
				ImVec2 a = ImGui::GetCursorScreenPos();
				dl = ImGui::GetWindowDrawList();
				// over all columns: the cell's clip would cut it to the first one; the table's own window still bounds it
				ImRect inner = ImGui::GetCurrentWindow()->InnerClipRect;
				dl->PushClipRect(ImVec2(ImMax(a.x - 12, inner.Min.x), ImMax(a.y, inner.Min.y)),
				                 ImVec2(ImMin(right + 12, inner.Max.x), ImMin(a.y + bh, inner.Max.y)), false);
				dl->AddRectFilled(ImVec2(a.x, a.y + 4), ImVec2(right, a.y + bh - 6), Col(IM_COL32(16, 14, 19, 236)), 6);
				dl->AddRect(ImVec2(a.x + 0.5f, a.y + 4.5f), ImVec2(right - 0.5f, a.y + bh - 6.5f), Col(pal::Line), 6);
				ImGui::PushFont(g_fonts.mono);
				if (src.empty()) dl->AddText(ImVec2(a.x + 14, a.y + 12), Col(pal::Text3), Tr("Файл не прочитан", "File not read"));
				for (int ln = from; ln <= to && !src.empty(); ln++)
					CodeLine(dl, ImVec2(a.x + 14, a.y + 10 + (ln - from) * lh), right - a.x - 28, ln, src[ln - 1], ln == r.line, lh);
				ImGui::PopFont();
				dl->PopClipRect();
			}
		}
		ImGui::EndTable();
	}
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(2);
}

static void DrawLuaPage()
{
	PageHeader(kTabs[TAB_LUA]);
	if (!LuaProfilerRunning()) {
		bool ml = GetModuleHandleA("MoonLoader.asi") != nullptr;
		EmptyState(ICON_BRACES, Tr("Профайлер не запущен", "Profiler not running"),
		           ml ? Tr("Покажет, сколько забирает каждый скрипт — главный цикл, события, imgui-окна — и какие строки кода. Сам замер почти ничего не стоит.",
		                   "Shows how much frame time each script uses (main loop, events, ImGui windows) and which lines of code. Negligible overhead.")
		              : Tr("MoonLoader не найден — замерять нечего.", "MoonLoader not found — nothing to profile."),
		           ml ? Tr("Запустить", "Start") : nullptr, false, LuaProfilerStartNow, &g_fixLuaProfiler, nullptr);
		return;
	}
	BuildLuaTree(g_luaTree);
	static const char *const viewsRu[] = { "Скрипты", "Строки кода" };
	static const char *const viewsEn[] = { "Scripts", "Code lines" };
	float y = ImGui::GetCursorPosY();
	Segmented("luaview", g_english ? viewsEn : viewsRu, 2, &g_luaView);
	double total = 0;
	for (int r : g_luaTree.roots) total += g_luaTree.nodes[r].incl;
	char buf[64];
	sprintf_s(buf, Tr("Все скрипты вместе: %.1f%% кадра", "All scripts combined: %.1f%% frame"), total);
	ImGui::SameLine(0, 18);
	ImGui::SetCursorPosY(y + 6);
	ImGui::PushFont(g_fonts.medium);
	TextColored(pal::Accent, buf);
	ImGui::PopFont();
	ImGui::SetCursorPosY(y + 34 + 10);
	if (g_luaView == 1) {
		DrawLuaLines();
		return;
	}
	TreeToolbar(g_luaTree, &g_fixLuaProfiler);
	DrawTree(g_luaTree, "##lua", ImGui::GetContentRegionAvail().y - 2);
}

static bool ProfilerGate(Tab tab)
{
	if (SampProfilerRunning()) return true;
	const char *text = Tr("Покажет весь кадр одним деревом: этапы GTA (мир, физика, рендер, HUD) и функции samp.dll (игроки, объекты, ники, "
	                      "чат, текстдравы) там, где они реально вызываются, — сколько % кадра и вызовов в секунду.",
	                      "Shows the whole frame as one tree: GTA stages (world, physics, render, HUD) and samp.dll functions (players, objects, nametags, "
	                      "chat, textdraws) where they actually run — frame % and calls per second.");
	EmptyState(kTabs[tab].icon, Tr("Профайлер не запущен", "Profiler not running"),
	           g_samp ? text : Tr("SA-MP ещё не загружен.", "SA-MP is not loaded yet."),
	           g_samp ? Tr("Запустить", "Start") : nullptr,
	           SampProfilerStarting(), SampProfilerStartNow, &g_fixSampProfiler,
	           Tr("Сам замер забирает ~6% кадра и работает до перезапуска игры", "Profiling itself costs ~6% frame time and runs until restart"));
	return false;
}

static void CostNote()
{
	// drawn by hand from one point: on the line of the segmented control ImGui would push the text down
	// by that line's frame padding and not the icon
	const char *text = Tr("Замер замедляет игру до её перезапуска; его собственная доля из цифр вычтена",
	                      "Profiling slows the game until restart; its own overhead is subtracted from numbers");
	ImGui::PushFont(g_fonts.tiny);
	ImDrawList *dl = ImGui::GetWindowDrawList();
	ImVec2 p = Snap(ImGui::GetCursorScreenPos());
	Icon(dl, ImVec2(p.x + 7.5f, p.y + TextMidY()), ICON_CIRCLE_ALERT, g_fonts.icon14, pal::Warn);
	dl->AddText(ImVec2(p.x + 22, p.y), Col(pal::Warn), text);
	ImGui::Dummy(ImVec2(22 + ImGui::CalcTextSize(text).x, ImGui::GetTextLineHeight()));
	ImGui::PopFont();
}

static void DrawOps(float height)
{
	const std::vector<OpRow> &ops = SampProfilerOps();
	ImVec2 p = ImGui::GetCursorScreenPos();
	float w = ImGui::GetContentRegionAvail().x;
	Surface(ImGui::GetWindowDrawList(), p, ImVec2(p.x + w, p.y + height), 12, pal::Panel);
	double maxPct = 0.01;
	for (const OpRow &o : ops) maxPct = ImMax(maxPct, o.incl);
	ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(12, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 0));
	ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, IM_COL32(218, 198, 168, 6));
	if (ImGui::BeginTable("##ops", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_PadOuterX, ImVec2(w, height))) {
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn(Tr("опкод", "opcode"), ImGuiTableColumnFlags_WidthFixed, 80);
		ImGui::TableSetupColumn(Tr("вызов из", "called from"), ImGuiTableColumnFlags_WidthFixed, 130);
		ImGui::TableSetupColumn(Tr("% кадра", "% frame"), ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn(Tr("вызовов/с", "calls/s"), ImGuiTableColumnFlags_WidthFixed, 92);
		HeaderRow(4);
		for (size_t k = 0; k < ops.size(); k++) {
			const OpRow &o = ops[k];
			const float rowH = 36;
			ImGui::TableNextRow(0, rowH);
			ImGui::TableNextColumn();
			ImVec2 c = ImGui::GetCursorScreenPos();
			float y = c.y;
			ImGui::PushID((int)k);
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(218, 198, 168, 14));
			ImGui::Selectable("##op", false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap, ImVec2(0, rowH));
			ImGui::PopStyleColor();
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
			if (ImGui::BeginPopupContextItem("##opctx")) {
				char line[96];
				sprintf_s(line, "%04X\tsamp+%05lX\t%.2f%%\t%.0f/s", o.op, o.site, o.incl, o.calls);
				if (MenuItem(ICON_COPY, Tr("Копировать строку", "Copy row"))) {
					ImGui::SetClipboardText(line);
					ShowToast(Tr("Строка скопирована", "Row copied"));
				}
				ImGui::EndPopup();
			}
			ImGui::PopStyleVar();
			ImGui::PopID();
			ImDrawList *dl = ImGui::GetWindowDrawList();
			char buf[32];
			ImGui::PushFont(g_fonts.mono);
			float my = y + (rowH - ImGui::GetFontSize()) * 0.5f - 1;
			sprintf_s(buf, "%04X", o.op);
			dl->AddText(Snap(ImVec2(c.x, my)), Col(pal::Text), buf);
			ImGui::TableNextColumn();
			c = ImGui::GetCursorScreenPos();
			sprintf_s(buf, "samp+%05lX", o.site);
			dl->AddText(Snap(ImVec2(c.x, my)), Col(pal::Text3), buf);
			ImGui::PopFont();
			ImGui::TableNextColumn();
			c = ImGui::GetCursorScreenPos();
			PctCell(c.x, y, ImGui::GetContentRegionAvail().x, rowH, o.incl, maxPct);
			ImGui::TableNextColumn();
			c = ImGui::GetCursorScreenPos();
			sprintf_s(buf, "%.0f", o.calls);
			dl->AddText(Snap(ImVec2(c.x, y + (rowH - ImGui::GetFontSize()) * 0.5f - 1)), Col(pal::Text2), buf);
		}
		ImGui::EndTable();
	}
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(2);
}

static void DrawProfPage()
{
	PageHeader(kTabs[TAB_PROF]);
	if (!ProfilerGate(TAB_PROF)) return;
	BuildTimerTree(g_profTree);
	TreeToolbar(g_profTree, &g_fixSampProfiler);
	static int view;
	static const char *const viewsRu[] = { "Кадр", "Скриптовые опкоды" };
	static const char *const viewsEn[] = { "Frame", "Script opcodes" };
	float y = ImGui::GetCursorPosY();
	Segmented("profview", g_english ? viewsEn : viewsRu, 2, &view);
	ImGui::SameLine(0, 16);
	ImGui::SetCursorPosY(y + 9);
	CostNote();
	ImGui::SetCursorPosY(y + 34 + 10);
	if (view == 0) DrawTree(g_profTree, "##prof", ImGui::GetContentRegionAvail().y - 2);
	else DrawOps(ImGui::GetContentRegionAvail().y - 2);
}

// ---------------------------------------------------------------- page: freezes and input lag

static Fix g_spikeAutostart = { .section = "SpikeProfiler", .defaultOn = false };

static void StatCard(ImDrawList *dl, ImVec2 a, float w, float h, const char *label, const char *value, const char *note, ImU32 col)
{
	Surface(dl, a, ImVec2(a.x + w, a.y + h), 12, pal::Panel);
	ImGui::PushFont(g_fonts.tiny);
	dl->AddText(Snap(ImVec2(a.x + 14, a.y + 10)), Col(pal::Text3), label);
	dl->AddText(Snap(ImVec2(a.x + 14, a.y + h - 24)), Col(pal::Text2), note);
	ImGui::PopFont();
	ImGui::PushFont(g_fonts.title);
	dl->AddText(Snap(ImVec2(a.x + 14, a.y + 27)), Col(col), value);
	ImGui::PopFont();
}

// the last frames as bars: a long frame stands out, its colour tells how far above the usual one it is
static void FrameGraph(ImVec2 a, float w, float h, float normalMs)
{
	static float ms[1024];
	ImDrawList *dl = ImGui::GetWindowDrawList();
	Surface(dl, a, ImVec2(a.x + w, a.y + h), 12, pal::Panel);
	int want = ImMin(1024, (int)((w - 24) / 2));
	int n = SpikeProfilerFrames(ms, want);
	if (n < 2) return;
	float peak = normalMs * 3;
	for (int i = 0; i < n; i++) peak = ImMax(peak, ms[i]);
	float top = Anim("lag_top", peak * 1.1f, 5.0f);
	float x0 = a.x + 12, y1 = a.y + h - 10, span = h - 34;
	for (int i = 0; i < n; i++) {
		float t = ImClamp(ms[i] / top, 0.0f, 1.0f);
		bool spike = normalMs > 0 && ms[i] > normalMs * 2 && ms[i] - normalMs > 3;
		float x = x0 + i * 2.0f;
		dl->AddRectFilled(ImVec2(x, y1 - ImMax(1.0f, span * t)), ImVec2(x + 1.5f, y1), Col(spike ? pal::Bad : Alpha(pal::Accent2, 0.75f)));
	}
	char buf[160]; // UTF-8: two bytes a letter
	sprintf_s(buf, Tr("последние %d кадров, верх шкалы %.0f мс", "last %d frames, top %.0f ms"), n, top);
	ImGui::PushFont(g_fonts.tiny);
	dl->AddText(Snap(ImVec2(a.x + 14, a.y + 8)), Col(pal::Text3), buf);
	ImGui::PopFont();
}

static void DrawLagPage()
{
	PageHeader(kTabs[TAB_LAG]);
	if (!SpikeProfilerRunning()) {
		EmptyState(kTabs[TAB_LAG].icon, Tr("Замер не запущен", "Monitor not running"),
		           Tr("Покажет каждый долгий кадр (фриз) и на что он ушёл: код игры и плагинов, ожидание диска или блокировки, "
		              "Present видеокарты. Плюс 1% и 0.1% low и задержку от чтения клавиш и мыши до отправки кадра.",
		              "Shows every long frame (stutter) and where time went: game/plugin code, disk/lock waits, or GPU Present. "
		              "Also tracks 1% and 0.1% lows plus input-to-frame latency."),
		           Tr("Запустить", "Start"), false, SpikeProfilerStart, &g_spikeAutostart,
		           Tr("Считает, пока меню закрыто: запусти, поиграй и открой снова", "Records while menu is closed: start, play, and reopen"));
		return;
	}
	const SpikeSummary &s = SpikeProfilerSummary();
	ImDrawList *dl = ImGui::GetWindowDrawList();
	char v[64], note[160];
	auto fps = [](float ms) { return ms > 0 ? 1000.0f / ms : 0.0f; };

	float x0 = ImGui::GetCursorPosX(), y0 = ImGui::GetCursorPosY();
	float right = x0 + ImGui::GetContentRegionAvail().x - 6;
	if (Button("##lagreset", ICON_X, Tr("Сбросить", "Reset"), false, 128)) SpikeProfilerReset();
	ImGui::SameLine(0, 8);
	if (Button("##lagstop", ICON_PAUSE, Tr("Остановить", "Stop"), false, 140)) SpikeProfilerStop();
	ImGui::SameLine(0, 8);
	if (Button("##lagcopy", ICON_CLIPBOARD_COPY, Tr("Копировать", "Copy"), false)) {
		ImGui::SetClipboardText(SpikeProfilerText().c_str());
		ShowToast(Tr("Замер скопирован", "Report copied"));
	}
	AutostartToggle(&g_spikeAutostart, right, y0);
	ImGui::SetCursorPos(ImVec2(x0, y0 + 34 + 10));

	float w = ImGui::GetContentRegionAvail().x;
	ImVec2 p = Snap(ImGui::GetCursorScreenPos());
	const float gap = 10, cardH = 84;
	float cw = (float)(int)((w - gap * 4) / 5);
	sprintf_s(v, "%.0f FPS", fps(s.avgMs));
	sprintf_s(note, Tr("кадр %.2f мс", "frame %.2f ms"), s.avgMs);
	StatCard(dl, p, cw, cardH, Tr("Средний", "Average"), v, note, pal::Text);
	sprintf_s(v, "%.0f FPS", fps(s.low1Ms));
	sprintf_s(note, Tr("кадр %.1f мс", "frame %.1f ms"), s.low1Ms);
	StatCard(dl, ImVec2(p.x + (cw + gap), p.y), cw, cardH, "1% low", v, note, s.low1Ms > s.avgMs * 2.5f ? pal::Warn : pal::Text);
	sprintf_s(v, "%.0f FPS", fps(s.low01Ms));
	sprintf_s(note, Tr("худший %.0f мс", "worst %.0f ms"), s.worstMs);
	StatCard(dl, ImVec2(p.x + (cw + gap) * 2, p.y), cw, cardH, "0.1% low", v, note, s.low01Ms > s.avgMs * 4 ? pal::Bad : pal::Text);
	sprintf_s(v, "%d", s.spikes);
	sprintf_s(note, Tr("%.1f в минуту", "%.1f per min"), s.seconds > 0 ? s.spikes * 60.0 / s.seconds : 0.0);
	StatCard(dl, ImVec2(p.x + (cw + gap) * 3, p.y), cw, cardH, Tr("Фризов", "Stutters"), v, note, s.spikes ? pal::Warn : pal::Ok);
	if (s.inputMeasured) {
		sprintf_s(v, Tr("%.1f мс", "%.1f ms"), s.inputMs);
		sprintf_s(note, Tr("1%% худших %.1f мс", "worst 1%% %.1f ms"), s.inputWorstMs);
	} else {
		sprintf_s(v, Tr("нет", "n/a"));
		sprintf_s(note, Tr("нужна GTA SA 1.0 US", "needs GTA SA 1.0 US"));
	}
	StatCard(dl, ImVec2(p.x + (cw + gap) * 4, p.y), w - (cw + gap) * 4, cardH, Tr("Ввод до кадра", "Input latency"), v, note, pal::Accent);

	// what a frame is made of, and what the freezes were
	ImVec2 q(p.x, p.y + cardH + gap);
	const float infoH = 86;
	float half = (float)(int)((w - gap) / 2);
	Surface(dl, q, ImVec2(q.x + half, q.y + infoH), 12, pal::Panel);
	ImGui::PushFont(g_fonts.tiny);
	dl->AddText(Snap(ImVec2(q.x + 14, q.y + 10)), Col(pal::Text3), Tr("Из чего состоит обычный кадр", "Typical frame breakdown"));
	const float shares[SPIKE_CAUSES] = { s.cpuShare, s.waitShare, s.presentShare };
	const ImU32 cols[SPIKE_CAUSES] = { pal::Accent2, pal::Warn, pal::Accent };
	static const char *const shortRu[SPIKE_CAUSES] = { "код", "ожидание", "Present" };
	static const char *const shortEn[SPIKE_CAUSES] = { "code", "wait", "Present" };
	const char *const *shortNames = g_english ? shortEn : shortRu;
	float bx = q.x + 14, bw = half - 28;
	for (int k = 0; k < SPIKE_CAUSES; k++) {
		float seg = bw * ImClamp(shares[k], 0.0f, 1.0f);
		if (seg > 1) dl->AddRectFilled(ImVec2(bx, q.y + 32), ImVec2(bx + seg - 1, q.y + 40), Col(cols[k]), 3);
		bx += seg;
	}
	float tx = q.x + 14;
	for (int k = 0; k < SPIKE_CAUSES; k++) {
		sprintf_s(note, "%s %.0f%%", shortNames[k], shares[k] * 100);
		dl->AddRectFilled(ImVec2(tx, q.y + 55), ImVec2(tx + 8, q.y + 63), Col(cols[k]), 2);
		dl->AddText(Snap(ImVec2(tx + 13, q.y + 51)), Col(pal::Text2), note);
		tx += ImGui::CalcTextSize(note).x + 30;
	}
	ImVec2 q2(q.x + half + gap, q.y);
	Surface(dl, q2, ImVec2(p.x + w, q.y + infoH), 12, pal::Panel);
	dl->AddText(Snap(ImVec2(q2.x + 14, q2.y + 10)), Col(pal::Text3), Tr("Отчего были фризы", "Stutter causes"));
	for (int k = 0; k < SPIKE_CAUSES; k++) {
		sprintf_s(note, "%d", s.causeCount[k]);
		float ry = q2.y + 30 + k * 17.0f;
		dl->AddRectFilled(ImVec2(q2.x + 14, ry + 4), ImVec2(q2.x + 22, ry + 12), Col(cols[k]), 2);
		dl->AddText(Snap(ImVec2(q2.x + 28, ry)), Col(s.causeCount[k] ? pal::Text : pal::Text3), SpikeCauseName(k));
		dl->AddText(Snap(ImVec2(p.x + w - 14 - ImGui::CalcTextSize(note).x, ry)), Col(s.causeCount[k] ? pal::Text : pal::Text3), note);
	}
	ImGui::PopFont();

	ImVec2 g(p.x, q.y + infoH + gap);
	const float graphH = 92;
	FrameGraph(g, w, graphH, s.avgMs);

	ImGui::SetCursorScreenPos(ImVec2(p.x, g.y + graphH + gap));
	float height = ImGui::GetContentRegionAvail().y - 2;
	ImVec2 t = ImGui::GetCursorScreenPos();
	Surface(dl, t, ImVec2(t.x + w, t.y + height), 12, pal::Panel);
	const std::vector<SpikeRow> &rows = SpikeProfilerRows();
	if (rows.empty()) {
		const char *text = s.frames < 200 ? Tr("Закрой меню и поиграй: кадры считаются, пока меню закрыто", "Close the menu and play: frames are recorded while closed")
		                                  : Tr("Фризов не было", "No stutters recorded");
		ImVec2 sz = ImGui::CalcTextSize(text);
		dl->AddText(Snap(ImVec2(t.x + (w - sz.x) * 0.5f, t.y + (height - sz.y) * 0.5f)), Col(pal::Text3), text);
		ImGui::Dummy(ImVec2(w, height));
		return;
	}
	ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(12, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 0));
	ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, IM_COL32(218, 198, 168, 6));
	if (ImGui::BeginTable("##spikes", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_PadOuterX, ImVec2(w, height))) {
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn(Tr("время", "time"), ImGuiTableColumnFlags_WidthFixed, 56);
		ImGui::TableSetupColumn(Tr("кадр", "frame"), ImGuiTableColumnFlags_WidthFixed, 70);
		ImGui::TableSetupColumn(Tr("код", "code"), ImGuiTableColumnFlags_WidthFixed, 56);
		ImGui::TableSetupColumn(Tr("ожид.", "wait"), ImGuiTableColumnFlags_WidthFixed, 56);
		ImGui::TableSetupColumn("Present", ImGuiTableColumnFlags_WidthFixed, 60);
		ImGui::TableSetupColumn(Tr("причина", "cause"), ImGuiTableColumnFlags_WidthStretch);
		HeaderRow(6);
		for (int k = (int)rows.size() - 1; k >= 0; k--) {
			const SpikeRow &r = rows[k];
			const float rowH = 34;
			ImGui::TableNextRow(0, rowH);
			// the table has its own draw list: the page one is not clipped to the scrolled area
			ImDrawList *tdl = ImGui::GetWindowDrawList();
			ImGui::TableNextColumn();
			ImVec2 c = ImGui::GetCursorScreenPos();
			float my = c.y + (rowH - ImGui::GetFontSize()) * 0.5f - 1;
			ImGui::PushID(k);
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(218, 198, 168, 14));
			ImGui::Selectable("##spike", false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap, ImVec2(0, rowH));
			ImGui::PopStyleColor();
			bool hovered = ImGui::IsItemHovered();
			ImGui::PopID();

			std::string cause = SpikeCauseName(r.cause);
			char extra[160];
			if (r.streamKb >= 64) {
				sprintf_s(extra, Tr(" · модели +%d КБ", " · models +%d KB"), r.streamKb);
				cause += extra;
			}
			if (r.faults >= 100) {
				sprintf_s(extra, Tr(" · подкачка %lu стр.", " · pagefaults %lu"), r.faults);
				cause += extra;
			}
			if (!r.top.empty()) cause += " · " + r.top;
			if (!r.stack.empty()) cause += " · " + r.stack.substr(0, r.stack.find('\n'));
			if (hovered) {
				char tip[2048];
				_snprintf_s(tip, _TRUNCATE,
				          Tr("Кадр %.1f мс при обычном %.1f мс.\nКод %.1f мс, ожидание %.1f мс, Present %.1f мс.\nМодели: %+d КБ, страничных ошибок: %lu.%s%s%s%s",
				             "Frame %.1f ms vs normal %.1f ms.\nCode %.1f ms, wait %.1f ms, Present %.1f ms.\nModels: %+d KB, page faults: %lu.%s%s%s%s"),
				          r.ms, r.normal, r.cpu, r.wait, r.present, r.streamKb, r.faults,
				          r.top.empty() ? "" : Tr("\nВыросли: ", "\nSpiked: "), r.top.c_str(),
				          r.stack.empty() ? "" : Tr("\nГде стояла игра:\n", "\nWhere the game stalled:\n"), r.stack.c_str());
				Tooltip(SpikeCauseName(r.cause), tip);
			}
			char buf[32];
			sprintf_s(buf, "%d:%02d", (int)r.at / 60, (int)r.at % 60);
			tdl->AddText(Snap(ImVec2(c.x, my)), Col(pal::Text3), buf);
			const float parts[4] = { r.ms, r.cpu, r.wait, r.present };
			for (int j = 0; j < 4; j++) {
				ImGui::TableNextColumn();
				c = ImGui::GetCursorScreenPos();
				sprintf_s(buf, "%.1f", parts[j]);
				ImU32 col = j == 0 ? pal::Bad : (j - 1 == r.cause ? pal::Text : pal::Text3);
				tdl->AddText(Snap(ImVec2(c.x, my)), Col(col), buf);
			}
			ImGui::TableNextColumn();
			c = ImGui::GetCursorScreenPos();
			tdl->AddText(Snap(ImVec2(c.x, my)), Col(pal::Text2), Fit(cause.c_str(), ImGui::GetContentRegionAvail().x - 4).c_str());
		}
		ImGui::EndTable();
	}
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(2);
}

// ---------------------------------------------------------------- page: log

static void DrawLogPage()
{
	static std::vector<std::string> lines;
	static size_t seen;
	static bool autoscroll = true;
	size_t before = lines.size();
	LogTail(lines, seen);

	PageHeader(kTabs[TAB_LOGS]);
	float y = ImGui::GetCursorPosY();
	if (Button("##copy", ICON_COPY, Tr("Копировать всё", "Copy all"), false)) {
		std::string all;
		for (const std::string &l : lines) all += l + "\n";
		ImGui::SetClipboardText(all.c_str());
		ShowToast(Tr("Журнал скопирован", "Log copied"));
	}
	ImGui::SameLine(0, 8);
	if (Button("##folder", ICON_FOLDER_OPEN, Tr("Папка игры", "Game folder"), false)) ShellExecuteA(nullptr, "open", g_dir, nullptr, nullptr, SW_SHOWNORMAL);
	ImGui::SameLine(0, 20);
	float x = ImGui::GetCursorPosX();
	ImGui::SetCursorPos(ImVec2(x, y + 6));
	Toggle("##autoscroll", &autoscroll);
	ImGui::SetCursorPos(ImVec2(x + 48, y + 7));
	TextColored(pal::Text2, Tr("Автопрокрутка", "Autoscroll"));
	ImGui::SetCursorPosY(y + 34 + 10);

	ImVec2 p = ImGui::GetCursorScreenPos();
	ImVec2 avail = ImGui::GetContentRegionAvail();
	Surface(ImGui::GetWindowDrawList(), p, ImVec2(p.x + avail.x, p.y + avail.y - 2), 12, pal::Panel);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 14));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 5));
	ImGui::BeginChild("##log", ImVec2(avail.x, avail.y - 2), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground);
	ImGui::PushFont(g_fonts.mono);
	for (const std::string &l : lines) {
		ImU32 col = pal::Text2;
		if (l.find("MISMATCH") != std::string::npos || l.find("failed") != std::string::npos) col = pal::Bad;
		else if (l.find("skipped") != std::string::npos || l.find("not ") != std::string::npos || l.find("unknown") != std::string::npos) col = pal::Warn;
		size_t ts = l.size() > 11 && l[0] == '[' ? 11 : 0; // "[hh:mm:ss] "
		if (ts) {
			TextColored(pal::Text3, l.substr(0, ts).c_str());
			ImGui::SameLine(0, 0);
		}
		TextWrapped(col, l.c_str() + ts, ImGui::GetContentRegionAvail().x);
	}
	ImGui::PopFont();
	if (autoscroll && lines.size() != before) ImGui::SetScrollHereY(1.0f);
	ImGui::EndChild();
	ImGui::PopStyleVar(2);
}

// ---------------------------------------------------------------- page: settings

// a settings row of fixed height: icon, title + hint; returns where the control goes
static ImVec2 SettingRow(ImVec2 p, float w, const char *icon, const char *title, const char *hint, float controlW, float controlH)
{
	const float rowH = 72;
	ImDrawList *dl = ImGui::GetWindowDrawList();
	IconTile(dl, ImVec2(p.x, p.y + (rowH - 34) * 0.5f), 34, icon, g_fonts.icon14, pal::Text2);
	float textW = w - 48 - controlW - 22;
	ImGui::PushFont(g_fonts.medium);
	float th = ImGui::GetFontSize();
	ImGui::PopFont();
	ImGui::PushFont(g_fonts.tiny);
	float hh = ImGui::CalcTextSize(hint, nullptr, false, textW).y;
	ImGui::PopFont();
	float ty = (float)(int)(p.y + (rowH - (th + 2 + hh)) * 0.5f);
	ImGui::PushFont(g_fonts.medium);
	dl->AddText(ImVec2(p.x + 48, ty - 1), Col(pal::Text), title);
	ImGui::PopFont();
	ImGui::SetCursorScreenPos(ImVec2(p.x + 48, ty + th + 2));
	ImGui::PushFont(g_fonts.tiny);
	TextWrapped(pal::Text3, hint, textW);
	ImGui::PopFont();
	return Snap(ImVec2(p.x + w - controlW, p.y + (rowH - controlH) * 0.5f));
}

static void InfoRow(const char *k, const char *v, ImU32 col, float x)
{
	float y = ImGui::GetCursorPosY(), x0 = ImGui::GetCursorPosX();
	ImGui::PushFont(g_fonts.tiny);
	TextColored(pal::Text3, k);
	ImGui::SetCursorPos(ImVec2(x, y));
	TextColored(col, v);
	ImGui::PopFont();
	ImGui::SetCursorPos(ImVec2(x0, y + 26));
}

static void DrawSettingsPage()
{
	PageHeader(kTabs[TAB_SETTINGS]);
	float w = ImGui::GetContentRegionAvail().x;
	float left = (float)(int)(w * 0.58f), right = w - left - 14;
	ImVec2 p = Snap(ImGui::GetCursorScreenPos());
	ImDrawList *dl = ImGui::GetWindowDrawList();
	const float rowH = 72, leftH = 5 * rowH + 20, rightH = leftH;
	Surface(dl, p, ImVec2(p.x + left, p.y + leftH), 8.0f, pal::Panel);
	Surface(dl, ImVec2(p.x + left + 14, p.y), ImVec2(p.x + w, p.y + rightH), 8.0f, pal::Panel);

	float iw = left - 40;
	ImVec2 r = ImVec2(p.x + 20, p.y + 10);
	char keyName[64] = "?";
	if (g_captureKey) strcpy_s(keyName, Tr("Нажми клавишу…", "Press a key..."));
	else if (!GetKeyNameTextA(MapVirtualKeyA(g_ui.key, MAPVK_VK_TO_VSC) << 16, keyName, sizeof(keyName))) sprintf_s(keyName, "0x%02X", g_ui.key);
	ImGui::SetCursorScreenPos(SettingRow(r, iw, ICON_KEYBOARD, Tr("Клавиша меню", "Menu key"), Tr("Открывает и закрывает меню. Esc тоже закрывает.", "Opens and closes the menu. Esc also closes."), 144, 32));
	if (Button("##key", nullptr, keyName, g_captureKey, 144)) g_captureKey = !g_captureKey;
	dl->AddLine(ImVec2(r.x, r.y + rowH + 0.5f), ImVec2(r.x + iw, r.y + rowH + 0.5f), Col(pal::Line));

	r.y += rowH;
	ImGui::SetCursorScreenPos(SettingRow(r, iw, ICON_SUN_MOON, Tr("Вуаль фона", "Background dim"), Tr("Насколько плотно дымка приглушает акварельный фон за меню.", "How strongly the dark veil dims the background art."), 200, 20));
	SliderInt("##dim", &g_ui.dim, 0, 100, "%d%%", 200);
	if (ImGui::IsItemDeactivated()) SaveUiConfig();
	dl->AddLine(ImVec2(r.x, r.y + rowH + 0.5f), ImVec2(r.x + iw, r.y + rowH + 0.5f), Col(pal::Line));

	r.y += rowH;
	ImGui::SetCursorScreenPos(SettingRow(r, iw, ICON_MONITOR, Tr("Телеметрия в углу экрана", "Corner telemetry HUD"), Tr("Компактная полоска FPS · ms · CPU · GPU справа сверху.", "Compact FPS · ms · CPU · GPU strip in the top-right corner."), 36, 20));
	if (Toggle("##fpsov", &g_ui.fpsOverlay)) SaveUiConfig();
	dl->AddLine(ImVec2(r.x, r.y + rowH + 0.5f), ImVec2(r.x + iw, r.y + rowH + 0.5f), Col(pal::Line));

	r.y += rowH;
	ImGui::SetCursorScreenPos(SettingRow(r, iw, ICON_TIMER, Tr("Обновление счётчика", "Counter refresh"), Tr("Как часто обновляются цифры FPS, CPU и GPU (читаемость).", "How often FPS, CPU, and GPU readouts update."), 200, 20));
	SliderInt("##hudhz", &g_ui.overlayRefreshMs, 200, 1000, Tr("%d мс", "%d ms"), 200);
	if (ImGui::IsItemDeactivated()) SaveUiConfig();
	dl->AddLine(ImVec2(r.x, r.y + rowH + 0.5f), ImVec2(r.x + iw, r.y + rowH + 0.5f), Col(pal::Line));

	r.y += rowH;
	static const char *const langs[] = { "Русский", "English" };
	int langIdx = g_english ? 1 : 0;
	ImGui::SetCursorScreenPos(SettingRow(r, iw, ICON_TYPE, Tr("Язык / Language", "Language"), Tr("Переводит меню и перезаписывает пояснения в .ini.", "Translates the menu and rewrites .ini comments."), 176, 32));
	if (Segmented("lang", langs, 2, &langIdx)) {
		g_ini.SetLanguage(langIdx == 1);
		ShowToast(langIdx == 1 ? "Switched to English (.ini updated)" : "Язык: Русский (.ini обновлён)");
	}

	ImGui::SetCursorScreenPos(ImVec2(p.x + left + 34, p.y + 20));
	ImGui::BeginGroup();
	ImGui::PushFont(g_fonts.medium);
	TextColored(pal::Text, Tr("О плагине", "About"));
	ImGui::PopFont();
	ImGui::Dummy(ImVec2(0, 6));
	float vx = ImGui::GetCursorPosX() + 118;
	InfoRow(Tr("Плагин", "Plugin"), PLUGIN_NAME " " PLUGIN_VERSION, pal::Text, vx);
	{
		float y = ImGui::GetCursorPosY(), x0 = ImGui::GetCursorPosX();
		ImGui::PushFont(g_fonts.tiny);
		TextColored(pal::Text3, Tr("Разработчик", "Author"));
		ImVec2 at = ImGui::GetWindowPos();
		AuthorLink(ImVec2(at.x + vx - ImGui::GetScrollX(), at.y + y - ImGui::GetScrollY()), "", pal::Text);
		ImGui::PopFont();
		ImGui::SetCursorPos(ImVec2(x0, y + 26));
	}
	InfoRow("GTA SA", IsGta10US() ? "1.0 US" : Tr("не 1.0 US", "not 1.0 US"), IsGta10US() ? pal::Ok : pal::Warn, vx);
	InfoRow("SA-MP", SampVersionName() ? SampVersionName() : (GetModuleHandleA("samp.dll") ? Tr("неизвестная версия", "unknown version") : Tr("не загружен", "not loaded")),
	        g_samp ? pal::Ok : pal::Warn, vx);
	InfoRow("SAMPFUNCS", GetModuleHandleA("SAMPFUNCS.asi") ? Tr("активен", "active") : Tr("нет", "no"), pal::Text2, vx);
	InfoRow("MoonLoader", GetModuleHandleA("MoonLoader.asi") ? Tr("активен", "active") : Tr("нет", "no"), pal::Text2, vx);
	char d3d[MAX_PATH] = "";
	HMODULE d3d9 = GetModuleHandleA("d3d9.dll");
	if (d3d9) GetModuleFileNameA(d3d9, d3d, MAX_PATH);
	bool local = d3d9 && !_strnicmp(d3d, g_dir, strlen(g_dir));
	InfoRow("d3d9.dll", local ? Tr("DXVK (локальный)", "DXVK (local)") : Tr("системный", "system"), pal::Text2, vx);
	ImGui::Dummy(ImVec2(0, 6));
	if (Button("##ini", ICON_FILE_COG, Tr("Открыть " PLUGIN_NAME ".ini", "Open " PLUGIN_NAME ".ini"), false, right - 40)) {
		char ini[MAX_PATH];
		sprintf_s(ini, "%s\\" PLUGIN_NAME ".ini", g_dir);
		ShellExecuteA(nullptr, "open", ini, nullptr, nullptr, SW_SHOWNORMAL);
	}
	ImGui::EndGroup();

	ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + ImMax(leftH, rightH) + 10));
	ImGui::Dummy(ImVec2(w, 1));
}

// ---------------------------------------------------------------- window

void DrawMenu(bool &open)
{
	ImGuiIO &io = ImGui::GetIO();
	ImVec2 disp = io.DisplaySize;
	ImVec2 size = Snap(ImVec2(ImMin(1160.0f, disp.x * 0.94f), ImMin(740.0f, disp.y * 0.92f)));
	if (g_pos.x < 0) {
		g_pos = Snap(ImVec2((disp.x - size.x) * 0.5f, (disp.y - size.y) * 0.5f));
		g_tabSince = ImGui::GetTime();
	}
	g_pos.x = ImClamp(g_pos.x, 40 - size.x, disp.x - 40);
	g_pos.y = ImClamp(g_pos.y, 0.0f, disp.y - 40);

	float e = EaseOut(g_menuAnim);
	ImGuiStyle &style = ImGui::GetStyle();
	style.Alpha = e;
	ImVec2 pos = Snap(ImVec2(g_pos.x, g_pos.y + (1 - e) * 10));

	ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), disp, Col(IM_COL32(6, 5, 8, 125)));

	ImGui::SetNextWindowPos(pos);
	ImGui::SetNextWindowSize(size);
	ImGui::Begin("##modernfpsboost", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
	                                            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse);
	ImDrawList *dl = ImGui::GetWindowDrawList();
	ImVec2 a = pos, b(a.x + size.x, a.y + size.y);
	const float r = 10.0f;

	Shadow(dl, a, b, r, 28, 0.85f);

	// Yoshitaka Amano gothic ink & watercolor background framed with smoked velvet veil
	if (g_background && g_backgroundW > 0 && g_backgroundH > 0) {
		float ia = (float)g_backgroundW / g_backgroundH, wa = size.x / size.y;
		ImVec2 uv0(0, 0), uv1(1, 1);
		if (ia > wa) { float k = wa / ia; uv0.x = (1 - k) * 0.5f; uv1.x = 1 - uv0.x; }
		else { float k = ia / wa; uv0.y = (1 - k) * 0.5f; uv1.y = 1 - uv0.y; }
		dl->AddImageRounded((ImTextureID)(uintptr_t)g_background, a, b, uv0, uv1, Col(IM_COL32_WHITE), r);
	}
	float veil = 0.12f + 0.78f * ImClamp(g_ui.dim / 100.0f, 0.0f, 1.0f);
	dl->AddRectFilled(a, b, Col(IM_COL32(15, 13, 18, (int)(veil * 255))), r);

	// Full-height smoked velvet left rail separated by one antique gold-ivory hairline
	ImVec2 railB(a.x + kSidebarW, b.y);
	dl->AddRectFilled(a, railB, Col(pal::Rail), r, ImDrawFlags_RoundCornersLeft);
	dl->AddLine(ImVec2(railB.x + 0.5f, a.y + 1), ImVec2(railB.x + 0.5f, b.y - 1), Col(pal::Line));

	// Delicate antique gold outer hairline
	dl->AddRect(ImVec2(a.x + 0.5f, a.y + 0.5f), ImVec2(b.x - 0.5f, b.y - 0.5f), Col(IM_COL32(218, 198, 168, 42)), r, 0, 1.0f);

	DrawSidebar(a, railB);

	float pe = PageIn();
	ImVec2 ca = Snap(ImVec2(railB.x + 24, a.y + 18));
	ImGui::SetCursorScreenPos(ca);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
	ImGui::BeginChild("##page", ImVec2(b.x - 24 - ca.x, b.y - 18 - ca.y), ImGuiChildFlags_AlwaysUseWindowPadding,
	                  ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	ImGui::PopStyleVar();
	style.Alpha = e * pe;
	switch (g_tab) {
	case TAB_FIXES: DrawFixesPage(); break;
	case TAB_LUA: DrawLuaPage(); break;
	case TAB_PROF: DrawProfPage(); break;
	case TAB_LAG: DrawLagPage(); break;
	case TAB_LOGS: DrawLogPage(); break;
	case TAB_SETTINGS: DrawSettingsPage(); break;
	}
	style.Alpha = e;
	ImGui::EndChild();
	DrawTopBar(ImVec2(railB.x, a.y), ImVec2(b.x, a.y + kTitleH), open);
	DrawToast(a, b);
	ImGui::End();
	style.Alpha = 1.0f;
}

// Restraint-first minimal corner telemetry strip (SteamOS / RivaTuner-by-Braun):
// Zero icons, zero neon, fixed-width tabular slots (zero horizontal jitter), calm latched cadence.
void DrawFpsOverlay()
{
	ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 14, 14), ImGuiCond_Always, ImVec2(1, 0));
	ImGui::Begin("##fps", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs |
	                                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);

	char fpsStr[16], msStr[16], cpuStr[16], gpuStr[16];
	sprintf_s(fpsStr, "%4.0f", g_frames.dispFps);
	sprintf_s(msStr, "%4.1f", g_frames.dispMs);
	sprintf_s(cpuStr, "%3.0f%%", g_frames.sysCpu);
	if (g_frames.gpu >= 0.0f) sprintf_s(gpuStr, "%3.0f%%", g_frames.gpu);
	else strcpy_s(gpuStr, "  — ");

	ImGui::PushFont(g_fonts.mono);
	const float charW = ImGui::CalcTextSize("0").x;
	const float fh = ImGui::GetFontSize();
	ImGui::PopFont();

	// Fixed tabular geometry so the strip never dances left/right when digits change
	const float padX = 11.0f;
	const float h = 26.0f;
	const float wFps  = charW * 4;
	const float wUnit = charW * 3;
	const float wMs   = charW * 4;
	const float wMsU  = charW * 2;
	const float wLbl  = charW * 3;
	const float wPct  = charW * 4;

	const float totalW = padX + wFps + 4 + wUnit + 12 + wMs + 3 + wMsU + 12 + 1 + 12 + wLbl + 4 + wPct + 12 + wLbl + 4 + wPct + padX;
	ImVec2 p = Snap(ImGui::GetCursorScreenPos());
	ImVec2 b = Snap(ImVec2(p.x + totalW, p.y + h));
	ImDrawList *dl = ImGui::GetWindowDrawList();

	dl->AddRectFilled(p, b, IM_COL32(18, 16, 21, 214), 5.0f);
	dl->AddRect(ImVec2(p.x + 0.5f, p.y + 0.5f), ImVec2(b.x - 0.5f, b.y - 0.5f), IM_COL32(218, 198, 168, 44), 5.0f);

	float x = p.x + padX;
	float ty = (float)(int)(p.y + (h - fh) * 0.5f);

	ImGui::PushFont(g_fonts.mono);
	dl->AddText(Snap(ImVec2(x, ty)), pal::Text, fpsStr);
	x += wFps + 4;
	dl->AddText(Snap(ImVec2(x, ty)), pal::Accent, "FPS");
	x += wUnit + 12;

	dl->AddText(Snap(ImVec2(x, ty)), pal::Text2, msStr);
	x += wMs + 3;
	dl->AddText(Snap(ImVec2(x, ty)), pal::Text3, "ms");
	x += wMsU + 12;

	dl->AddLine(ImVec2((float)(int)x + 0.5f, p.y + 6.0f), ImVec2((float)(int)x + 0.5f, b.y - 6.0f), IM_COL32(218, 198, 168, 42));
	x += 13;

	dl->AddText(Snap(ImVec2(x, ty)), pal::Text3, "CPU");
	x += wLbl + 4;
	dl->AddText(Snap(ImVec2(x, ty)), g_frames.sysCpu >= 85.0f ? pal::Warn : pal::Text, cpuStr);
	x += wPct + 12;

	dl->AddText(Snap(ImVec2(x, ty)), pal::Text3, "GPU");
	x += wLbl + 4;
	dl->AddText(Snap(ImVec2(x, ty)), g_frames.gpu >= 90.0f ? pal::Warn : pal::Text, gpuStr);
	ImGui::PopFont();

	ImGui::Dummy(ImVec2(totalW, h));
	ImGui::End();
}

// tools/preview: open a tab without clicking
void MenuSelectTab(int tab, bool openRows)
{
	g_previewOpen = openRows;
	g_tab = tab;
	g_luaView = openRows && tab == TAB_LUA ? 1 : 0;
	g_tabSince = -10.0;
}
