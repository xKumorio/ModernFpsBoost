// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// gta_sa.exe 1.0 US and samp.dll 0.3.7 R1: addresses and layouts used by more than one fix.
#pragma once

#include <windows.h>
#include <string.h>

#include "samp.h"

// ---------------------------------------------------------------- gta_sa.exe 1.0 US

static const DWORD kTimeMs = 0xB7CB84;        // CTimer::m_snTimeInMilliseconds
static const DWORD kFrameCounter = 0xB7CB4C;  // CTimer::m_FrameCounter
static const DWORD kLocalPlayerPed = 0xB7CD98; // CWorld::Players[0].m_pPed
static const DWORD kPlayerPedVtbl = 0x86D168;  // CPlayerPed vtable
static const float *const kCamPos = (const float *)0xB6F9CC; // TheCamera source position, as GetDistanceToCamera reads it

static const DWORD kPedPool = 0xB74490;     // CPools::ms_pPedPool
static const DWORD kVehiclePool = 0xB74494; // CPools::ms_pVehiclePool
static const DWORD kObjectPool = 0xB7449C;  // CPools::ms_pObjectPool
static const int   kObjectSize = 0x19C;

struct GtaPool { BYTE *objects; signed char *flags; int size; };

static inline DWORD TimeMs() { return *(DWORD *)kTimeMs; }
static inline void *LocalPed() { return *(void **)kLocalPlayerPed; }

// gta_sa.exe 1.0 US: checked in DllMain before any patch ([ColPlaneCache] rewrites the bytes it looks at)
bool IsGta10US();

// CPlaceable position: the matrix's if it has one
static inline const float *EntityPos(const void *e)
{
	BYTE *matrix = *(BYTE **)((const BYTE *)e + 0x14);
	return matrix ? (const float *)(matrix + 0x30) : (const float *)((const BYTE *)e + 4);
}

// id byte of the GTA object pool slot of ent (changes when a new object takes the slot)
static inline bool GtaObjectGen(DWORD ent, BYTE &gen)
{
	GtaPool *p = *(GtaPool **)kObjectPool;
	if (!p) return false;
	DWORD off = ent - (DWORD)p->objects;
	if (off >= (DWORD)p->size * kObjectSize || off % kObjectSize) return false;
	gen = (BYTE)p->flags[off / kObjectSize];
	return true;
}

// ---------------------------------------------------------------- samp.dll (every version: core/samp.h)

static const int kMaxPlayers = 0x3EC; // the same in every version

// Player structs move between versions: the offsets come from the table of the running version.
struct SampLayout {
	int poolLargest, poolLocal, poolInfo, poolUsed; // CPlayerPool
	int infoRemote, remotePed, localPed;            // CPlayerInfo -> CRemotePlayer -> CPed, CLocalPlayer -> CPed
	int netPools, poolsPlayer;                      // CNetGame -> pools -> CPlayerPool
	int objPoolObjs;                                // CObjectPool -> CObject*[]
	int netState, netConnected;                     // CNetGame -> game state, its value once connected
};
extern SampLayout g_sl; // set once samp.dll is identified

// CPlayerPool* of the running game, or nullptr
BYTE *SampPlayerPool();
