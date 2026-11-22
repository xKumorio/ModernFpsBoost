// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [NametagLOSCache] When the server enables nametag line-of-sight (netgame settings +0x2F), SA-MP
// checks every nametag and 3D label each frame with GTA script opcode 06BD IS_LINE_OF_SIGHT_CLEAR,
// run through the script interpreter: ScriptCommand(&cmd, x1,y1,z1, x2,y2,z2 (doubles), 1,0,0,1,0)
// at R1 samp+0x67577 (labels) and samp+0x6FE0A / 0x70F1A (player tags). Each is a ray through the map
// (CWorld::GetIsLineOfSightClear -> TestLineTriangle). The answer is reused for cache_ms while both
// ends stay within the same 0.5 m cell; a miss calls the original ScriptCommand.

#include <math.h>

#include "../registry.h"

static const SampId kSampLosSites[] = { S_LOS_SITE_LABEL, S_LOS_SITE_TAG, S_LOS_SITE_TAG2 };

static int   g_losCacheMs = 100;
static DWORD g_sampScriptCommand;

struct LosSlot { int key[8]; DWORD time; int result; bool used; };
static LosSlot g_los[2048];

static int __cdecl hkLosCommand(const void *cmd, double x1, double y1, double z1, double x2, double y2, double z2,
                                int buildings, int vehicles, int peds, int objects, int dummies)
{
	auto orig = (int(__cdecl *)(const void *, ...))g_sampScriptCommand;
	if (g_losCacheMs <= 0)
		return orig(cmd, x1, y1, z1, x2, y2, z2, buildings, vehicles, peds, objects, dummies);

	int key[8] = {
		(int)floor(x1 * 2.0), (int)floor(y1 * 2.0), (int)floor(z1 * 2.0),
		(int)floor(x2 * 2.0), (int)floor(y2 * 2.0), (int)floor(z2 * 2.0),
		(int)(DWORD)cmd, buildings | vehicles << 1 | peds << 2 | objects << 3 | dummies << 4,
	};
	DWORD h = 2166136261u;
	for (int k : key) h = (h ^ (DWORD)k) * 16777619u;
	LosSlot &s = g_los[h & 2047];
	DWORD now = TimeMs();
	if (s.used && now - s.time < Jitter((DWORD)&s, (DWORD)g_losCacheMs) && !memcmp(s.key, key, sizeof(key)))
		return s.result;

	int r = orig(cmd, x1, y1, z1, x2, y2, z2, buildings, vehicles, peds, objects, dummies);
	memcpy(s.key, key, sizeof(key));
	s.time = now;
	s.result = r;
	s.used = true;
	return r;
}

static void Load(const Ini &ini)
{
	g_losCacheMs = ini.Int("NametagLOSCache", "cache_ms", 100);
}

static bool Apply(BYTE *samp)
{
	g_sampScriptCommand = (DWORD)SampAt(S_SCRIPT_COMMAND);
	int done = 0;
	for (SampId id : kSampLosSites) {
		BYTE *site = SampAt(id);
		if (!site || !g_sampScriptCommand) continue;
		DWORD cmd = *(DWORD *)(site - 4); // push imm32, already relocated by the loader
		bool ok = site[-5] == 0x68 && IsCallTo(site, g_sampScriptCommand) &&
		          site[5] == 0x83 && site[6] == 0xC4 && site[7] == 0x48 && *(WORD *)cmd == 0x06BD;
		if (!ok) {
			Log("NametagLOSCache: samp+%05lX is not the expected 06BD call - skipped", (DWORD)(site - samp));
			continue;
		}
		int32_t rel = Rel32(site, hkLosCommand);
		if (PatchBytes((DWORD)site + 1, nullptr, &rel, 4)) done++;
	}
	g_fixNametagLosCache.active = done > 0;
	Log("NametagLOSCache: %d/3 line-of-sight checks cached (cache_ms=%d)", done, g_losCacheMs);
	return true;
}

static const Param kParams[] = {
	{ "cache_ms", "Держать ответ, мс", &g_losCacheMs, nullptr, 0, 500, true },
};

Fix g_fixNametagLosCache = {
	.section = "NametagLOSCache",
	.title = "Ники за стеной",
	.gain = "-7% кадра",
	.summary = "Ответ «ник за стеной или нет» запоминается на cache_ms, а не считается лучом каждый кадр.",
	.details = "Если сервер включил проверку видимости ников, SA-MP каждый кадр для каждого ника и 3D-текста пускает луч сквозь всю карту, да ещё через медленный интерпретатор скриптов GTA (опкод 06BD). 100 игроков = сотни лучей за кадр.\n\nОтвет держится cache_ms, пока камера и цель в той же клетке 0.5 м. Ник может спрятаться за стеной на долю секунды позже.",
	.load = Load,
	.atSamp = Apply,
	.params = kParams,
	.paramCount = Count(kParams),
};
