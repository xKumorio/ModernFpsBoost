// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [ObjectSkip] CObjectPool::Process (R1 samp+0xF5A0) runs CObject::Process (samp+0xA3F70) for every
// server object every frame (~460 per frame on a mapped server). For an object that is not attached
// and not moving, that work is only "is the camera inside its draw distance -> keep / create /
// delete its RW object" (samp+0xA3EB0: draw distance at +0x54, and dd - 30 for model requests).
// Distance to the camera can change by no more than the camera has moved, so the call is skipped
// while the camera has moved less than the gap to the nearest threshold (minus 0.5 m) and the object
// itself (entity, RW object, position, draw distance, attach/move state) is unchanged, and at most
// for skip_ms. Everything else (attached, moving, near a threshold) runs every frame as before.
// The check itself is cache misses (object, entity, matrix, all far apart); the pool is walked in the
// same order every frame, so the objects a few calls ahead (last frame's order) are prefetched.

#include <math.h>
#include <xmmintrin.h>

#include "../registry.h"

// CObject fields (the same in every version): entity +0x40, draw distance +0x54, position +0x5C,
// attached to vehicle/object +0xBE/+0xC0, moving +0x1AF bit 0, +0x118B
static DWORD g_objProcess;
static int   g_objSkipMs = 100;

struct ObjSkipSlot {
	BYTE *obj, *ent;
	void *rw;
	float dd, pos[3], cam[3], margin;
	DWORD f118b, time;
};
static ObjSkipSlot g_objSkip[4096];
static unsigned g_objSkipped, g_objRun;

// call order of the last frame and of this one: object, its entity and matrix
struct ObjOrder { BYTE *obj, *ent, *mat; };
static ObjOrder g_order[2][4096];
static int g_orderN[2], g_orderCur, g_callN;
static DWORD g_orderFrame;
static const int kAhead = 6;

static void PrefetchAhead(BYTE *o, BYTE *ent)
{
	DWORD frame = *(DWORD *)kFrameCounter;
	if (frame != g_orderFrame) {
		g_orderFrame = frame;
		g_orderN[g_orderCur] = g_callN;
		g_orderCur ^= 1;
		g_callN = 0;
	}
	int n = g_callN++;
	if (n < 4096) {
		ObjOrder &cur = g_order[g_orderCur][n];
		cur.obj = o;
		cur.ent = ent;
		cur.mat = ent ? *(BYTE **)(ent + 0x14) : nullptr;
	}
	const ObjOrder *last = g_order[g_orderCur ^ 1];
	int k = n + kAhead;
	if (k >= g_orderN[g_orderCur ^ 1]) return;
	const ObjOrder &a = last[k];
	if (!a.obj) return;
	_mm_prefetch((const char *)a.obj + 0x40, _MM_HINT_T0);
	_mm_prefetch((const char *)a.obj + 0x80, _MM_HINT_T0);
	_mm_prefetch((const char *)a.obj + 0xC0, _MM_HINT_T0);
	_mm_prefetch((const char *)a.obj + 0x1AF, _MM_HINT_T0);
	_mm_prefetch((const char *)a.obj + 0x118B, _MM_HINT_T0);
	_mm_prefetch((const char *)&g_objSkip[((DWORD)a.obj >> 4) & 4095], _MM_HINT_T0);
	if (a.ent) _mm_prefetch((const char *)a.ent + 0x14, _MM_HINT_T0);
	if (a.mat) _mm_prefetch((const char *)a.mat + 0x30, _MM_HINT_T0);
}

static bool ObjEntityPos(BYTE *ent, float out[3])
{
	if (!ent) return false;
	memcpy(out, EntityPos(ent), 12);
	return true;
}

// attached to a vehicle/object or moving: always processed
static bool ObjBusy(BYTE *o)
{
	return *(WORD *)(o + 0xBE) != 0xFFFF || *(WORD *)(o + 0xC0) != 0xFFFF || (o[0x1AF] & 1);
}

static void __fastcall hkObjectProcess(BYTE *o, void *, float dt)
{
	auto orig = (void(__thiscall *)(BYTE *, float))g_objProcess;
	BYTE *ent = *(BYTE **)(o + 0x40);
	PrefetchAhead(o, ent);
	if (ObjBusy(o)) {
		orig(o, dt);
		return;
	}
	void *rw = ent ? *(void **)(ent + 0x18) : nullptr;
	float pos[3];
	if (!ObjEntityPos(ent, pos)) memcpy(pos, o + 0x5C, 12);
	DWORD now = TimeMs();
	ObjSkipSlot &s = g_objSkip[((DWORD)o >> 4) & 4095];
	if (s.obj == o && s.ent == ent && s.rw == rw && s.dd == *(float *)(o + 0x54) && s.f118b == *(DWORD *)(o + 0x118B) &&
	    !memcmp(s.pos, pos, 12) && now - s.time < Jitter((DWORD)o, (DWORD)g_objSkipMs)) {
		float dx = kCamPos[0] - s.cam[0], dy = kCamPos[1] - s.cam[1], dz = kCamPos[2] - s.cam[2];
		if (dx * dx + dy * dy + dz * dz < s.margin * s.margin) {
			g_objSkipped++;
			return;
		}
	}
	g_objRun++;
	orig(o, dt);

	// state after the real call decides how long the answer holds
	s.obj = nullptr;
	if (ObjBusy(o)) return;
	s.ent = *(BYTE **)(o + 0x40);
	s.rw = s.ent ? *(void **)(s.ent + 0x18) : nullptr;
	if (!ObjEntityPos(s.ent, s.pos)) memcpy(s.pos, o + 0x5C, 12);
	s.dd = *(float *)(o + 0x54);
	s.f118b = *(DWORD *)(o + 0x118B);
	memcpy(s.cam, kCamPos, 12);
	float dx = s.pos[0] - s.cam[0], dy = s.pos[1] - s.cam[1], dz = s.pos[2] - s.cam[2];
	float dist = sqrtf(dx * dx + dy * dy + dz * dz);
	float m1 = fabsf(dist - s.dd), m2 = fabsf(dist - (s.dd - 30.0f));
	s.margin = (m1 < m2 ? m1 : m2) - 0.5f;
	// out of range but still holding its RW object: SA-MP counts frames before deleting it, keep that exact
	if (s.margin <= 0 || (s.rw && dist > s.dd)) return;
	s.time = now;
	s.obj = o;
}

static void Load(const Ini &ini)
{
	g_objSkipMs = ini.Int("ObjectSkip", "skip_ms", 100);
}

static bool Apply(BYTE *)
{
	if (g_objSkipMs <= 0) return true;
	BYTE *site = SampAt(S_OBJ_PROCESS_SITE);
	g_objProcess = (DWORD)SampAt(S_OBJ_PROCESS);
	if (!site || !g_objProcess || !IsCallTo(site, g_objProcess)) {
		Log("ObjectSkip: the CObject::Process call is not original - skipped");
		return true;
	}
	int32_t rel = Rel32(site, hkObjectProcess);
	if ((g_fixObjectSkip.active = PatchBytes((DWORD)site + 1, nullptr, &rel, 4)))
		Log("ObjectSkip: static server objects re-checked only when the camera moved enough (skip_ms=%d)", g_objSkipMs);
	return true;
}

static void Report(std::string &out, double secs)
{
	if (g_objRun + g_objSkipped)
		Appendf(out, Tr("ObjectSkip: пропущено %.0f%% обработок объектов (%.0f/с обработано, %.0f/с пропущено)\n", "ObjectSkip: skipped %.0f%% of object checks (%.0f/s processed, %.0f/s skipped)\n"),
		        100.0 * g_objSkipped / (g_objRun + g_objSkipped), g_objRun / secs, g_objSkipped / secs);
	g_objRun = g_objSkipped = 0;
}

static const Param kParams[] = {
	{ "skip_ms", "Не дольше, мс", &g_objSkipMs, nullptr, 0, 500, true },
};

Fix g_fixObjectSkip = {
	.section = "ObjectSkip",
	.title = "Статичный маппинг",
	.gain = "-2–3% кадра",
	.summary = "Неподвижные объекты маппинга не перепроверяются каждый кадр, пока камера не сдвинулась достаточно.",
	.details = "SA-MP каждый кадр для каждого серверного объекта (~460) считает расстояние до камеры и решает, показать или удалить модель. Для статичного маппинга ответ почти всегда тот же.\n\nРасстояние не может измениться больше, чем сдвинулась камера, поэтому объект перепроверяется, только когда камера могла пересечь его границу дальности, или раз в skip_ms. Движущиеся и прикреплённые объекты обрабатываются как раньше.",
	.load = Load,
	.atSamp = Apply,
	.report = Report,
	.params = kParams,
	.paramCount = Count(kParams),
};
