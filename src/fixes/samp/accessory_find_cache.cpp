// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [AccessoryFindCache] SA-MP's object render hook (R1 samp+0x9F8BC) asks CPlayerPool::FindAccessory
// (R1 samp+0x106A0) for every drawn GTA object that is not a server object - lamps, fences, every map
// prop - every frame: the local player's and every connected player's 10 attached-object slots, three
// pointer hops per player. Same scheme as [ObjectFindCache]: a found accessory is re-checked in its
// slot before it is returned; "not found" is kept while the GTA object pool slot keeps its id byte
// (an accessory is always a newly created GTA object) and for at most a second.

#include "../registry.h"

struct AccFindSlot { DWORD ent; int who, k; DWORD time; BYTE gen; bool neg; };
static AccFindSlot g_accFind[2048];

static DWORD AccessoryAt(BYTE *ped, int k, DWORD ent)
{
	// CPed accessories: used flags at +0x4C, CObject* at +0x27C (the same in every version)
	if (!ped || !*(DWORD *)(ped + 0x4C + 4 * k)) return 0;
	BYTE *o = *(BYTE **)(ped + 0x27C + 4 * k);
	return o && *(DWORD *)(o + 0x40) == ent ? (DWORD)o : 0;
}

// SA-MP CPlayerPed of a player (-1 = local)
static BYTE *AccessoryPed(BYTE *pool, int who)
{
	if (who < 0) {
		BYTE *local = *(BYTE **)(pool + g_sl.poolLocal);
		return local ? *(BYTE **)(local + g_sl.localPed) : nullptr;
	}
	if (who >= kMaxPlayers || *(int *)(pool + g_sl.poolUsed + 4 * who) != 1) return nullptr;
	BYTE *info = *(BYTE **)(pool + g_sl.poolInfo + 4 * who);
	BYTE *remote = info ? *(BYTE **)(info + g_sl.infoRemote) : nullptr;
	return remote ? *(BYTE **)(remote + g_sl.remotePed) : nullptr;
}

static DWORD __fastcall hkFindAccessory(BYTE *pool, void *, DWORD ent)
{
	AccFindSlot &s = g_accFind[((ent >> 3) ^ (ent >> 14)) & 2047];
	BYTE gen = 0;
	bool hasGen = GtaObjectGen(ent, gen);
	if (s.ent == ent) {
		if (!s.neg) {
			if (DWORD o = AccessoryAt(AccessoryPed(pool, s.who), s.k, ent)) return o;
		} else if (hasGen && s.gen == gen && TimeMs() - s.time < Jitter(ent, 1000))
			return 0;
	}
	int largest = *(int *)(pool + g_sl.poolLargest);
	for (int who = -1; who <= largest && who < kMaxPlayers; who++) {
		BYTE *ped = AccessoryPed(pool, who);
		if (!ped) continue;
		for (int k = 0; k < 10; k++)
			if (DWORD o = AccessoryAt(ped, k, ent)) {
				s = { ent, who, k, 0, gen, false };
				return o;
			}
	}
	if (hasGen) s = { ent, 0, 0, TimeMs(), gen, true };
	else s.ent = 0;
	return 0;
}

static bool Apply(BYTE *)
{
	BYTE *p = SampAt(S_FIND_ACCESSORY_SITE), *fn = SampAt(S_FIND_ACCESSORY);
	// the call must be the original and FindAccessory not hooked by another mod (it would be bypassed)
	if (!p || !fn || !IsCallTo(p, (DWORD)fn) || *fn == 0xE9) {
		Log("AccessoryFindCache: the FindAccessory call is not original - skipped");
		return true;
	}
	if ((g_fixAccessoryFindCache.active = RedirectCall(p, hkFindAccessory))) Log("AccessoryFindCache: attached-object lookup per drawn object cached");
	return true;
}

Fix g_fixAccessoryFindCache = {
	.section = "AccessoryFindCache",
	.title = "Аксессуары игроков",
	.gain = "-0.7% кадра",
	.summary = "Для каждого фонаря и забора SA-MP больше не проверяет все аксессуары всех игроков каждый кадр.",
	.details = "Для каждого объекта карты SA-MP каждый кадр проверяет все 10 слотов аксессуаров у каждого игрока: вдруг объект надет на кого-то.\n\nОтвет запоминается: найденный аксессуар перепроверяется на месте, «не аксессуар» держится, пока объект тот же, и не дольше секунды.",
	.atSamp = Apply,
};
