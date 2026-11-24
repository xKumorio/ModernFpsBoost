// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
#include "samp.h"

#include "game.h"
#include "patch.h"
#include "plugin.h"

int g_sampVersion = -1;
SampLayout g_sl;

// CNetGame::GetState (R1 samp+0x2E20: mov eax, [ecx+0x3BD]) and GAME_MODE_CONNECTED, read from each dll:
// R1/R2 compare the state with 14 in 15 places, R3-1..DL with 5 (samp-api's enums agree).
static const int kNetState[SAMP_VERSIONS][2] = {
	{ 0x3BD, 14 }, { 0x3B9, 14 }, { 0x3CD, 5 }, { 0x3CD, 5 }, { 0x3CD, 5 }, { 0x3CD, 5 },
};

bool SampIdentify(HMODULE samp, DWORD *entry)
{
	if (!samp || !entry || !IsReadableRange(samp, sizeof(IMAGE_DOS_HEADER))) return false;
	auto dos = (IMAGE_DOS_HEADER *)samp;
	if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
	BYTE *ntPtr = (BYTE *)samp + dos->e_lfanew;
	if (!IsReadableRange(ntPtr, sizeof(IMAGE_NT_HEADERS))) return false;
	auto nt = (IMAGE_NT_HEADERS *)ntPtr;
	if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
	*entry = nt->OptionalHeader.AddressOfEntryPoint;
	for (int v = 0; v < SAMP_VERSIONS; v++)
		if (kSampEntryPoints[v] == *entry) {
			g_sampVersion = v;
			g_sl = {
				SampVal(S_POOL_LARGEST), SampVal(S_POOL_LOCAL), SampVal(S_POOL_INFO), SampVal(S_POOL_USED),
				SampVal(S_INFO_REMOTE), SampVal(S_REMOTE_PED), SampVal(S_LOCAL_PED),
				SampVal(S_NET_POOLS), SampVal(S_POOLS_PLAYER), SampVal(S_OBJPOOL_OBJS),
				kNetState[v][0], kNetState[v][1],
			};
			return true;
		}
	return false;
}

const char *SampVersionName()
{
	return g_sampVersion >= 0 ? kSampVersionNames[g_sampVersion] : nullptr;
}

BYTE *SampAt(SampId id)
{
	return g_samp && SampHas(id) ? g_samp + SampVal(id) : nullptr;
}

BYTE *SampPlayerPool()
{
	BYTE **net = (BYTE **)SampAt(S_NETGAME);
	BYTE *pools = net && *net ? *(BYTE **)(*net + g_sl.netPools) : nullptr;
	return pools ? *(BYTE **)(pools + g_sl.poolsPlayer) : nullptr;
}
