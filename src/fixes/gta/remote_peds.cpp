// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [RemotePedPhysics] and [RemoteInAir]: other players' peds.
//
// [RemotePedPhysics] SA-MP 0.3.7 R1 never sets a remote ped's height: on a sync packet (samp+0x11670)
// it teleports the ped if it is >2 m off in X/Y or >1 m off in Z, otherwise it sets the packet speed
// plus 10% of the error; between packets (samp+0x11840) it only nudges X/Y speed. Everything else is
// local GTA physics: gravity + CPhysical::ProcessCollision (0x54DFB0) against the map, every frame,
// and since remote peds are CPlayerPed, IsPlayer() keeps even motionless ones from skipping
// CheckCollision. Kinematic mode replaces ProcessCollision for remote peds beyond min_distance with
// the no-collision tail of the original (ApplySpeed, Reorthogonalise, RemoveAndAdd) and turns their
// gravity off, so they move only by the synced speed and SA-MP's corrections. Their col model stays
// active: bullets, cars and melee against them are checked by the other side and are unaffected.
// SA-MP's GameInstallHooks writes CPlayerPed_ProcessCollision_Hook into the same slot when the game
// starts, and that hook jumps to 0x54DFB0 directly, so a hook installed before it drops out of the
// chain. The hook is installed from its own thread once SA-MP's is in the slot, and wraps it.
//
// [RemoteInAir] CPedGeometryAnalyser::IsInAir (0x5F1CB0) casts a vertical line (and a sphere test)
// under every ped that is not flagged as standing, every frame, from CEventInAir::AffectsPed
// (call at 0x4B0C84) and CEventScanner::ScanForEvents (0x607EE3). For other players' peds the
// result is reused while the ped has not moved (same position bits) and is younger than max_age_ms.
// Kinematic peds (above) are never in the air; that check needs the same hook, so it is installed
// for either fix.

#include <math.h>

#include "../registry.h"

// ---------------------------------------------------------------- [RemotePedPhysics]

static const int kVtProcessCollision = 11;

static float g_kinMinDist = 20.0f;
static void(__thiscall *oPedProcessCollision)(void *);

struct KinSlot { void *ped; DWORD frame; bool gravityOff; };
static KinSlot g_kin[1024];

static KinSlot &KinFor(void *ped) { return g_kin[((DWORD)ped >> 4) & 1023]; }

static bool IsKinematicNow(void *ped)
{
	KinSlot &s = KinFor(ped);
	return s.ped == ped && *(DWORD *)kFrameCounter - s.frame <= 1;
}

static void __fastcall hkPedProcessCollision(void *ped, void *)
{
	void *local = LocalPed();
	BYTE *p = (BYTE *)ped;
	DWORD &physFlags = *(DWORD *)(p + 0x40);
	KinSlot &s = KinFor(ped);

	bool kin = false;
	// SA-MP's own "no collisions for this ped" bit (dwProcFlags top bit): its hook returns at once
	if (ped != local && local && !(*(DWORD *)(p + 0x1C) & 0x80000000)) {
		const float *a = EntityPos(ped), *b = EntityPos(local);
		float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
		kin = dx * dx + dy * dy + dz * dz >= g_kinMinDist * g_kinMinDist;
	}
	if (!kin) {
		if (s.ped == ped && s.gravityOff) {
			physFlags |= 0x2; // bApplyGravity back on
			s.gravityOff = false;
		}
		if (s.ped == ped) s.ped = nullptr;
		oPedProcessCollision(ped);
		return;
	}

	if (s.ped != ped) { s.ped = ped; s.gravityOff = false; }
	s.frame = *(DWORD *)kFrameCounter;
	if (physFlags & 0x2) { physFlags &= ~0x2u; s.gravityOff = true; }

	float old[3];
	memcpy(old, EntityPos(ped), sizeof(old));
	*(float *)(p + 0xD4) = 0.0f;                       // m_fMovingSpeed
	physFlags &= ~(0x1000u | 0x8000u);                   // bSkipLineCol, bProcessingShift
	((void(__thiscall *)(void *))0x547B80)(ped);         // CPhysical::ApplySpeed
	if (void *m = *(void **)(p + 0x14))
		((void(__thiscall *)(void *))0x59B6A0)(m);       // CMatrix::Reorthogonalise
	*(DWORD *)(p + 0x1C) = (*(DWORD *)(p + 0x1C) & ~0x10u) | 0x20u; // !bIsStuck, bIsInSafePosition
	physFlags &= ~0x800u;                                // bProcessCollisionEvenIfStationary
	const float *now = EntityPos(ped);
	float mx = now[0] - old[0], my = now[1] - old[1], mz = now[2] - old[2];
	*(float *)(p + 0xD4) = sqrtf(mx * mx + my * my + mz * mz);
	((void(__thiscall *)(void *))0x542560)(ped);         // CPhysical::RemoveAndAdd
}

static DWORD WINAPI RemotePedPhysicsThread(LPVOID)
{
	void **vt = (void **)kPlayerPedVtbl;
	// up to 5 minutes for SA-MP to start the game; without SA-MP the original gets wrapped
	for (int i = 0; i < 3000 && vt[kVtProcessCollision] == (void *)0x54DFB0; i++) Sleep(100);
	Sleep(500); // SA-MP writes its hooks in one go; let it finish
	oPedProcessCollision = (void(__thiscall *)(void *))vt[kVtProcessCollision];
	void *hook = (void *)hkPedProcessCollision;
	if (oPedProcessCollision == hook) return 0;
	extern Fix g_fixRemotePedPhysics;
	g_fixRemotePedPhysics.active = PatchBytes((DWORD)&vt[kVtProcessCollision], nullptr, &hook, 4); // aligned 4-byte write: atomic for the main thread
	Log("RemotePedPhysics: kinematic remote peds beyond %.1f m (wraps %p)", g_kinMinDist, oPedProcessCollision);
	return 0;
}

// ---------------------------------------------------------------- [RemoteInAir]

static const DWORD kIsInAir = 0x5F1CB0;
static const DWORD kIsInAirCallSites[] = { 0x4B0C84, 0x607EE3 };

static int g_inAirMaxAge; // 0 while [RemoteInAir] is off: only the kinematic check runs
extern Fix g_fixRemoteInAir;

struct InAirSlot { void *ped; float x, y, z; DWORD time; bool result; };
static InAirSlot g_inAir[1024];

static bool __cdecl hkIsInAir(void *ped)
{
	auto orig = (bool(__cdecl *)(void *))kIsInAir;
	if (!ped || ped == LocalPed())
		return orig(ped);
	if (IsKinematicNow(ped)) // no gravity, no ground collision: never start a fall from here
		return false;
	if (g_inAirMaxAge <= 0)
		return orig(ped);

	const float *pos = EntityPos(ped);
	DWORD now = TimeMs();
	InAirSlot &s = g_inAir[((DWORD)ped >> 4) & 1023];
	if (s.ped == ped && now - s.time < (DWORD)g_inAirMaxAge && !memcmp(&s.x, pos, 12))
		return s.result;
	bool r = orig(ped);
	s.ped = ped;
	memcpy(&s.x, pos, 12);
	s.time = now;
	s.result = r;
	return r;
}

static void InstallInAirHook()
{
	static bool done;
	if (done) return;
	done = true;
	int n = 0;
	for (DWORD site : kIsInAirCallSites) {
		BYTE *p = (BYTE *)site;
		if (!IsCallTo(p, kIsInAir)) {
			Log("RemoteInAir: call at %08lX is not IsInAir (patched by another mod?) - skipped", site);
			continue;
		}
		BYTE call[5] = { 0xE8 };
		*(int32_t *)(call + 1) = Rel32(p, hkIsInAir);
		if (PatchBytes(site, nullptr, call, 5)) n++;
	}
	g_fixRemoteInAir.active = n > 0;
	Log("RemoteInAir: %d/2 call sites hooked (max_age_ms=%d)", n, g_inAirMaxAge);
}

// ---------------------------------------------------------------- fixes

static void LoadPhysics(const Ini &ini)
{
	g_kinMinDist = ini.Float("RemotePedPhysics", "min_distance", 20.0f);
}

static void ApplyPhysics()
{
	CloseHandle(CreateThread(nullptr, 0, RemotePedPhysicsThread, nullptr, 0, nullptr));
	InstallInAirHook();
}

static void LoadInAir(const Ini &ini)
{
	g_inAirMaxAge = ini.Int("RemoteInAir", "max_age_ms", 250);
}

static const Param kPhysicsParams[] = {
	{ "min_distance", "Дистанция, м", nullptr, &g_kinMinDist, 0, 150, true },
};

static const Param kInAirParams[] = {
	{ "max_age_ms", "Держать ответ, мс", &g_inAirMaxAge, nullptr, 0, 1000, true },
};

Fix g_fixRemotePedPhysics = {
	.section = "RemotePedPhysics",
	.title = "Физика чужих игроков",
	.gain = "-10% кадра",
	.summary = "Дальние игроки двигаются по синхронизации сервера, без расчёта столкновений с картой.",
	.details = "Каждый чужой игрок для GTA обычный пед: каждый кадр его сферы проверяются против треугольников карты, "
	           "даже если он стоит AFK. При этом высоту ему всё равно задаёт сервер.\n\n"
	           "Дальше min_distance метров у чужих игроков выключаются столкновения с картой и гравитация, они двигаются по скорости из пакетов. "
	           "Твои пули, машина и удары по ним работают как раньше, твой пед не трогается. Вблизи полная физика.",
	.load = LoadPhysics,
	.gta = true,
	.atStart = ApplyPhysics,
	.params = kPhysicsParams,
	.paramCount = Count(kPhysicsParams),
};

Fix g_fixRemoteInAir = {
	.section = "RemoteInAir",
	.title = "«В воздухе?» для чужих",
	.gain = "-4.5% кадра",
	.summary = "Луч вниз под стоящим игроком не пускается заново каждый кадр.",
	.details = "ИИ GTA каждый кадр для каждого педа пускает луч вниз сквозь карту, чтобы решить, не начать ли падение. "
	           "100 игроков = 100 лучей за кадр.\n\n"
	           "Для чужих игроков ответ запоминается, пока пед стоит на месте, и не дольше max_age_ms. Двигающиеся проверяются как раньше.",
	.load = LoadInAir,
	.gta = true,
	.atStart = InstallInAirHook,
	.params = kInAirParams,
	.paramCount = Count(kInAirParams),
};
