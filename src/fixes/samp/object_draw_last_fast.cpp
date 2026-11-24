// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [ObjectDrawLastFast] SA-MP 0.3.7-R1 CObjectPool::DrawLast (samp+0xF720, ~1.2% self):
// Every single frame it allocates 8000 bytes on the stack via _chkstk, zeroes all 8000 bytes with
// rep stosd (even when 0 objects are in the drawLast list!), scans all slots up to m_nLargestId,
// and sorts them with an O(N^2) x87 bubble sort.
// Here we skip the 8 KB memset completely, collect only active drawLast objects into a compact array,
// sort descending by distance with std::sort, and call CObject::Draw (following SampObjectCull if active).

#include <algorithm>

#include "../registry.h"

struct DrawLastEntry {
	BYTE *obj;
	float dist;
};

static float(__thiscall *oGetDistFromCam)(BYTE *);
static BYTE *g_drawCallSite; // CObjectPool::Draw's call to CObject::Draw (honors SampObjectCull)
static BYTE *g_origObjDraw;  // CObject::Draw
static int g_objsOff = 0xFA4; // 0xFA4 on R1..R5, 0x20D4 on 0.3.DL

static unsigned g_framesCount, g_emptyFrames;

static void __fastcall hkDrawLast(BYTE *pool)
{
	g_framesCount++;
	const int maxId = *(const int *)pool;
	if (maxId < 0) {
		g_emptyFrames++;
		return;
	}
	const int limit = maxId < 1000 ? maxId : 999;
	const int *exists = (const int *)(pool + 4);
	BYTE *const *objs = (BYTE *const *)(pool + g_objsOff);

	DrawLastEntry list[1000];
	int count = 0;
	for (int i = 0; i <= limit; i++) {
		if (!exists[i]) continue;
		BYTE *obj = objs[i];
		if (!obj || !obj[0x49] || !obj[0x6C]) continue;
		list[count++] = { obj, oGetDistFromCam(obj) };
	}
	if (!count) {
		g_emptyFrames++;
		return;
	}
	if (count > 1) {
		std::sort(list, list + count, [](const DrawLastEntry &a, const DrawLastEntry &b) {
			return a.dist > b.dist;
		});
	}
	// Resolve CObject::Draw target dynamically so SampObjectCull's hook at samp+0xF70D is honored
	void(__thiscall *drawFn)(BYTE *) = (void(__thiscall *)(BYTE *))g_origObjDraw;
	if (g_drawCallSite && *g_drawCallSite == 0xE8) {
		int32_t rel = *(const int32_t *)(g_drawCallSite + 1);
		drawFn = (void(__thiscall *)(BYTE *))(g_drawCallSite + 5 + rel);
	}
	for (int i = 0; i < count; i++)
		drawFn(list[i].obj);
}

static bool Apply(BYTE *)
{
	BYTE *fn = SampAt(S_OBJPOOL_DRAWLAST);
	BYTE *getDist = SampAt(S_OBJ_GETDISTFROMCAM);
	BYTE *drawSite = SampAt(S_OBJDRAW_SITE);
	BYTE *objDraw = SampAt(S_OBJ_DRAW);
	static const BYTE expect[] = { 0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0xB8, 0x40, 0x1F, 0x00, 0x00 };
	if (!fn || !getDist || !drawSite || !objDraw || !SampHas(S_OBJPOOL_OBJS) || !SafeMemMatch(fn, expect, sizeof(expect))) {
		Log("ObjectDrawLastFast: CObjectPool::DrawLast mismatch on %s - skipped", SampVersionName());
		return true;
	}
	oGetDistFromCam = (float(__thiscall *)(BYTE *))getDist;
	g_drawCallSite  = drawSite;
	g_origObjDraw   = objDraw;
	g_objsOff       = SampVal(S_OBJPOOL_OBJS);

	BYTE jmp[5] = { 0xE9 };
	*(int32_t *)(jmp + 1) = Rel32(fn, hkDrawLast);
	BYTE *addrs[] = { fn };
	const void *data[] = { jmp };
	size_t sizes[] = { 5 };
	if ((g_fixObjectDrawLastFast.active = PatchMainThread(addrs, data, sizes, 1)))
		Log("ObjectDrawLastFast: CObjectPool::DrawLast skips 8 KB memset and O(N^2) sort (%s)", SampVersionName());
	else
		Log("ObjectDrawLastFast: patch failed");
	return true;
}

static void Report(std::string &out, double secs)
{
	if (g_framesCount)
		Appendf(out, "ObjectDrawLastFast: убрано %.0f обнулений 8 КБ/с (%.0f%% пустых)\n",
		        g_framesCount / secs, 100.0 * g_emptyFrames / g_framesCount);
	g_framesCount = g_emptyFrames = 0;
}

Fix g_fixObjectDrawLastFast = {
	.section = "ObjectDrawLastFast",
	.title = "Быстрый CObjectPool::DrawLast",
	.gain = "-1.2% кадра",
	.summary = "Убирает бессмысленное обнуление 8 КБ стека каждый кадр и пузырьковую сортировку O(N^2) в SA-MP.",
	.details = "Функция SA-MP CObjectPool::DrawLast (прозрачные серверные объекты) каждый кадр через _chkstk выделяет и "
	           "обнуляет 8000 байт на стеке (rep stosd × 2000), даже если таких объектов 0, а затем сортирует их медленным "
	           "пузырьком O(N^2) на x87.\n\n"
	           "Здесь обнуление убрано, собираются только реально видимые drawLast-объекты и сортируются быстрым std::sort "
	           "в том же порядке (от дальних к ближним). Без потерь.",
	.atSamp = Apply,
	.report = Report,
};
