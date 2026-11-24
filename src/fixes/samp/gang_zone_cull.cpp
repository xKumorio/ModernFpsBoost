// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [GangZoneCull] CGangZonePool::Draw (R1 samp+0x2260) walks all 1024 gang zone slots every frame
// and hands every used zone to CRadar::DrawAreaOnRadar (0x5853D0): four corners turned into radar
// space and clipped against the radar circle, even for zones kilometres away that clip to nothing.
// Servers with hundreds of zones pay that every frame for a radar showing a few of them.
// The radar shows a circle of radius range around vec2DRadarOrigin: GTA clips shapes to the square
// and CRadar::DrawRadarMask hides the corners. A zone whose box stays farther than range * 1.05 from
// the centre is invisible and is skipped before the transform. If a mod changed DrawRadarMask (square
// radar), the test is the radar's square instead (half side range * 1.5, over the rotated sqrt(2)).
// The full map in the pause menu is drawn as before: culling is off while GTA's own drawing-map flag
// (FrontEndMenuManager.m_bDrawingMap, 0xBA67A1, what CRadar::DrawBlips and DrawAreaOnRadar read) is
// set. SA-MP's inMenu argument is not that flag (it passes the byte at 0xBA67A4), so it is not trusted.
// SA-MP calls it through mov eax, 0x5853D0 / call eax (one place in every version); the immediate
// is pointed here.

#include <math.h>

#include "../registry.h"

static const DWORD kDrawAreaOnRadar = 0x5853D0;
static float *const kRadarRange = (float *)0xBA8314;
static float *const kRadarOrigin = (float *)0xBAA248; // CVector2D
static const BYTE *const kDrawingMap = (const BYTE *)0xBA67A1;
static const DWORD kDrawRadarMask = 0x585700;
static bool g_round; // radar is GTA's circle (its mask hides everything past range): test the circle


static DWORD g_zonesDrawn, g_zonesCulled;

static void __cdecl hkDrawAreaOnRadar(const float *rect, const void *color, bool inMenu)
{
	if (!inMenu && !*kDrawingMap) {
		float x0 = fminf(rect[0], rect[2]), x1 = fmaxf(rect[0], rect[2]);
		float y0 = fminf(rect[1], rect[3]), y1 = fmaxf(rect[1], rect[3]);
		float range = *kRadarRange, ox = kRadarOrigin[0], oy = kRadarOrigin[1];
		bool away;
		if (g_round) { // distance from the radar centre to the nearest point of the box
			float dx = fmaxf(fmaxf(x0 - ox, ox - x1), 0), dy = fmaxf(fmaxf(y0 - oy, oy - y1), 0), r = range * 1.05f;
			away = dx * dx + dy * dy > r * r;
		} else {
			float r = range * 1.5f;
			away = x1 < ox - r || x0 > ox + r || y1 < oy - r || y0 > oy + r;
		}
		if (range > 0 && away) {
			g_zonesCulled++;
			return;
		}
	}
	g_zonesDrawn++;
	((void(__cdecl *)(const float *, const void *, bool))kDrawAreaOnRadar)(rect, color, inMenu);
}

static bool Apply(BYTE *samp)
{
	if (!IsGta10US()) return true;
	static const BYTE kMask[] = { 0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x83, 0xEC, 0x6C }; // CRadar::DrawRadarMask as shipped
	g_round = SafeMemMatch((void *)kDrawRadarMask, kMask, sizeof(kMask)); // a square-radar mod changes the mask
	static const BYTE kCall[] = { 0xB8, 0xD0, 0x53, 0x58, 0x00, 0xFF, 0xD0 }; // mov eax, 0x5853D0; call eax
	BYTE *modEnd = ModuleEnd((HMODULE)samp), *site = nullptr;
	int found = 0;
	if (modEnd && modEnd > samp + 0x1000 + sizeof(kCall)) {
		BYTE *scanEnd = modEnd - sizeof(kCall);
		for (BYTE *cur = samp + 0x1000; cur < scanEnd;) {
			MEMORY_BASIC_INFORMATION mbi{};
			if (!VirtualQuery(cur, &mbi, sizeof(mbi))) break;
			BYTE *regEnd = (BYTE *)mbi.BaseAddress + mbi.RegionSize;
			if (regEnd <= cur) break;
			if (mbi.State == MEM_COMMIT && IsExecutableReadableProtect(mbi.Protect)) {
				BYTE *lim = regEnd > scanEnd ? scanEnd : (regEnd > cur + sizeof(kCall) ? regEnd - sizeof(kCall) : cur);
				for (BYTE *p = cur; p < lim; p++)
					if (p[0] == 0xB8 && !memcmp(p, kCall, sizeof(kCall))) site = p, found++;
			}
			cur = regEnd;
		}
	}
	if (found != 1) {
		Log("GangZoneCull: %d DrawAreaOnRadar calls in samp.dll instead of 1 - skipped", found);
		return true;
	}
	DWORD target = (DWORD)hkDrawAreaOnRadar;
	BYTE *addrs[] = { site + 1 };
	const void *data[] = { &target };
	size_t sizes[] = { 4 };
	if ((g_fixGangZoneCull.active = PatchMainThread(addrs, data, sizes, 1)))
		Log("GangZoneCull: gang zones away from the radar are skipped (samp+%X)", (DWORD)(site - samp));
	return true;
}

static void Report(std::string &out, double secs)
{
	if (!g_zonesDrawn && !g_zonesCulled) return;
	char line[128];
	sprintf_s(line, "GangZoneCull: %.0f zones/s drawn, %.0f/s skipped (away from the radar)\n", g_zonesDrawn / secs, g_zonesCulled / secs);
	out += line;
	g_zonesDrawn = g_zonesCulled = 0;
}

Fix g_fixGangZoneCull = {
	.section = "GangZoneCull",
	.title = "Ганг-зоны вдали от радара",
	.gain = "до -0.5% кадра",
	.summary = "Зоны, которые не попадают на радар, не считаются каждый кадр.",
	.details = "SA-MP каждый кадр передаёт на радар каждую ганг-зону сервера, и GTA для каждой переводит углы в координаты радара и обрезает их по кругу — даже для зон за километры, от которых на радаре ничего не остаётся.\n\nТеперь зона, коробка которой не задевает область радара, пропускается до этих расчётов. Карта в меню паузы рисуется как раньше.",
	.atSamp = Apply,
	.report = Report,
};
