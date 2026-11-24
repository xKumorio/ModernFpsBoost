// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [SampObjectCull] SA-MP draws its server objects itself: CObjectPool::Draw / DrawLast (R1 samp+0xF6E0,
// samp+0xF720; every version through samp_addrs_gen.h) call CObject::Draw (samp+0xA48A0) for every object in the pool, and it checks only the
// draw distance - an object behind the camera is set up and drawn as one in front of it (on a mapped
// server ~700 calls and ~300 draws a frame, 10% of the frame).
// Before CObject::Draw the object's bounding sphere (its collision model's, moved by the entity's
// matrix) is tested against the camera (CCamera::IsSphereVisible); off screen only the position SA-MP
// keeps for its distance checks is updated (samp+0xA3970, the first thing Draw does) and the drawing is
// skipped. Objects never drawn yet (materials are made on the first draw) and models without a
// collision model always go through as before.

#include "../registry.h"

static const DWORD kIsSphereVisible = 0x420D40; // bool CCamera::IsSphereVisible(const CVector &, float)
static BYTE *const kCamera = (BYTE *)0xB6F028;
static BYTE **const kModelInfos = (BYTE **)0xA9B0C8;
static const DWORD kDummyVtbl = 0x863C40;

static DWORD g_draw, g_updatePos;
static DWORD g_drawn, g_culled;
static int g_entOff = 0x40, g_drawnOff = 0x118F;

static bool OnScreen(BYTE *ent)
{
	short model = *(short *)(ent + 0x22);
	if (model < 0 || model >= 20000) return true;
	BYTE *mi = kModelInfos[model];
	if (!mi) return true;
	BYTE *col = *(BYTE **)(mi + 0x14);
	if (!col) return true;
	const float *c = (const float *)(col + 0x18);
	float r = *(const float *)(col + 0x24) + 1.0f, w[3];
	if (const float *m = *(const float **)(ent + 0x14)) {
		for (int i = 0; i < 3; i++) w[i] = m[12 + i] + m[i] * c[0] + m[4 + i] * c[1] + m[8 + i] * c[2];
	} else {
		const float *p = (const float *)(ent + 4);
		for (int i = 0; i < 3; i++) w[i] = p[i] + c[i];
	}
	return ((bool(__thiscall *)(BYTE *, const float *, float))kIsSphereVisible)(kCamera, w, r);
}

static void __fastcall hkObjectDraw(BYTE *obj)
{
	BYTE *ent = *(BYTE **)(obj + g_entOff);
	if (ent && *(DWORD *)ent != kDummyVtbl && *(DWORD *)(obj + g_drawnOff) == 1 && !OnScreen(ent)) {
		((void(__thiscall *)(BYTE *))g_updatePos)(obj);
		g_culled++;
		return;
	}
	g_drawn++;
	((void(__thiscall *)(BYTE *))g_draw)(obj);
}

static bool Apply(BYTE *samp)
{
	if (!IsGta10US()) return true;
	BYTE *sites[] = { SampAt(S_OBJDRAW_SITE), SampAt(S_OBJDRAW_LAST_SITE) };
	g_draw = (DWORD)SampAt(S_OBJ_DRAW);
	g_updatePos = (DWORD)SampAt(S_OBJ_UPDATE_POS);
	if (!g_draw || !g_updatePos || !SampHas(S_OBJ_DRAWN) || !SampHas(S_ENT_GAME)) {
		Log("SampObjectCull: this SA-MP version is not in the table - skipped");
		return true;
	}
	g_entOff = SampVal(S_ENT_GAME);
	g_drawnOff = SampVal(S_OBJ_DRAWN);
	for (BYTE *s : sites)
		if (!s || !IsCallTo(s, g_draw)) {
			Log("SampObjectCull: a CObject::Draw call is not original - skipped");
			return true;
		}
	int rel[2];
	BYTE *addrs[2];
	const void *data[2];
	size_t sizes[2];
	for (int i = 0; i < 2; i++) {
		rel[i] = Rel32(sites[i], hkObjectDraw);
		addrs[i] = sites[i] + 1;
		data[i] = &rel[i];
		sizes[i] = 4;
	}
	if ((g_fixSampObjectCull.active = PatchMainThread(addrs, data, sizes, 2)))
		Log("SampObjectCull: SA-MP objects off screen are not drawn (samp+%X)", (DWORD)(sites[0] - samp));
	return true;
}

static void Report(std::string &out, double secs)
{
	if (g_drawn + g_culled)
		Appendf(out, "SampObjectCull: объектов SA-MP %.0f/с нарисовано, %.0f/с пропущено (вне экрана)\n", g_drawn / secs, g_culled / secs);
	g_drawn = g_culled = 0;
}

Fix g_fixSampObjectCull = {
	.section = "SampObjectCull",
	.title = "Объекты SA-MP вне экрана",
	.gain = "до -5% кадра",
	.summary = "Объекты сервера (маппинг), которые не попадают в камеру, не рисуются.",
	.details = "SA-MP рисует объекты сервера сам и проверяет только дальность прорисовки: объект за спиной готовится и "
	           "рисуется так же, как перед глазами. На сервере с маппингом это ~700 объектов и ~10% кадра.\n\n"
	           "Теперь сначала проверяется, попадает ли объект в камеру (по его сфере). Не попадает — SA-MP только "
	           "обновляет его позицию, без отрисовки. На экране всё то же самое.",
	.atSamp = Apply,
	.report = Report,
};
