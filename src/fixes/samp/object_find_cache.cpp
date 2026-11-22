// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [ObjectFindCache] CObjectPool::Find (R1 samp+0xF520) maps a GTA object to its SA-MP CObject by a
// linear scan of all slots (a cache miss per object), called from SA-MP's render hooks for every
// object drawn, on two pools. The answer is cached per (pool, entity) and checked before use: a
// found object must still be in its slot with the same entity; "not found" is kept only while the
// GTA object pool slot of the entity has the same id byte (a new GTA object there changes it) and
// the pool's highest id is the same, and for at most a second.

#include "../registry.h"

static const SampId kObjFindSites[] = { S_OBJFIND_SITE1, S_OBJFIND_SITE2, S_OBJFIND_SITE3, S_OBJFIND_SITE4, S_OBJFIND_SITE5 };

struct ObjFindSlot { BYTE *pool; DWORD ent; int idx, largest; DWORD time; BYTE gen; bool neg; };
static ObjFindSlot g_objFind[2048];
static unsigned g_objFindCalls, g_objFindHits;

static DWORD __fastcall hkObjFind(BYTE *pool, void *, DWORD ent)
{
	int largest = *(int *)pool;
	BYTE *const *objs = (BYTE *const *)(pool + g_sl.objPoolObjs); // [1..largest], as the original scans
	g_objFindCalls++;
	ObjFindSlot &s = g_objFind[((ent >> 3) ^ ((DWORD)pool >> 6) ^ (ent >> 14)) & 2047];
	BYTE gen = 0;
	bool hasGen = GtaObjectGen(ent, gen);
	if (s.pool == pool && s.ent == ent) {
		if (!s.neg) {
			if (s.idx >= 1 && s.idx <= largest) {
				BYTE *o = objs[s.idx];
				if (o && *(DWORD *)(o + 0x40) == ent) {
					g_objFindHits++;
					return (DWORD)o;
				}
			}
		} else if (hasGen && s.gen == gen && s.largest == largest && TimeMs() - s.time < Jitter(ent, 1000)) {
			g_objFindHits++;
			return 0;
		}
	}
	for (int i = 1; i <= largest; i++) {
		BYTE *o = objs[i];
		if (o && *(DWORD *)(o + 0x40) == ent) {
			s = { pool, ent, i, largest, 0, gen, false };
			return (DWORD)o;
		}
	}
	if (hasGen) s = { pool, ent, 0, largest, TimeMs(), gen, true };
	else s.pool = nullptr;
	return 0;
}

static bool Apply(BYTE *)
{
	BYTE *find = SampAt(S_OBJPOOL_FIND);
	const int n = sizeof(kObjFindSites) / sizeof(kObjFindSites[0]);
	BYTE *addrs[n];
	const void *data[n];
	size_t sizes[n];
	int32_t rel[n];
	int np = 0;
	for (int k = 0; k < n; k++) {
		BYTE *site = SampAt(kObjFindSites[k]);
		if (!site || !find || !IsCallTo(site, (DWORD)find)) continue;
		rel[np] = Rel32(site, hkObjFind);
		addrs[np] = site + 1;
		data[np] = &rel[np];
		sizes[np] = 4;
		np++;
	}
	if ((g_fixObjectFindCache.active = np && PatchMainThread(addrs, data, sizes, np))) Log("ObjectFindCache: %d/%d CObjectPool::Find calls cached", np, n);
	else Log("ObjectFindCache: no original CObjectPool::Find calls - skipped");
	return true;
}

static void Report(std::string &out, double secs)
{
	if (g_objFindCalls)
		Appendf(out, "ObjectFindCache: %.0f%% из %.0f поисков/с из кеша\n", 100.0 * g_objFindHits / g_objFindCalls, g_objFindCalls / secs);
	g_objFindCalls = g_objFindHits = 0;
}

Fix g_fixObjectFindCache = {
	.section = "ObjectFindCache",
	.title = "Поиск объектов SA-MP",
	.gain = "-1% кадра",
	.summary = "Перед отрисовкой объекта SA-MP больше не перебирает весь пул: ответ запоминается и перепроверяется.",
	.details = "Перед отрисовкой каждого объекта SA-MP ищет свой объект перебором всех слотов пула (до 1000, промах кэша на каждый), ~200 раз за кадр.\n\nОтвет запоминается для пары «пул + объект GTA» и проверяется перед выдачей. Иначе честный перебор. Без потерь.",
	.atSamp = Apply,
	.report = Report,
};
