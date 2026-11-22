// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// All fixes in the order they are applied.
#pragma once

#include "fix.h"

// gta_sa.exe
extern Fix g_fixColPlaneCache;
extern Fix g_fixRemotePedPhysics;
extern Fix g_fixRemoteInAir;
extern Fix g_fixRefPool;
extern Fix g_fixPedDrawDistance;
extern Fix g_fixVehicleLod;
extern Fix g_fixDrawDistance;
extern Fix g_fixStreamBudget;
extern Fix g_fixModelNameLookup;
extern Fix g_fixSampModelWait;
extern Fix g_fixMatrixSSE;
extern Fix g_fixCMatrixSSE;
extern Fix g_fixOcclusionOff;
extern Fix g_fixFtolSSE3;
extern Fix g_fixFloorSSE4;
extern Fix g_fixFrustumSSE;
extern Fix g_fixColTriSSE;
extern Fix g_fixColBoxSSE;
extern Fix g_fixColLinesSSE;
extern Fix g_fixFastAtan2SSE;
extern Fix g_fixFastStricmpSSE;
extern Fix g_fixMsgKeysFast;
extern Fix g_fixFxParticlesSSE;
extern Fix g_fixStreamMemBoost;
extern Fix g_fixEnvMapCache;
extern Fix g_fixCursorCenterFix;
extern Fix g_fixMessagePumpFix;
extern Fix g_fixAnimMathSSE;
extern Fix g_fixAnimBlendSSE;
extern Fix g_fixFrameSyncFast;
extern Fix g_fixSkinMatrixSSE;

// samp.dll
extern Fix g_fixNametagLosCache;
extern Fix g_fixObjectSkip;
extern Fix g_fixSampRotationSSE;
extern Fix g_fixPlayerLoop;
extern Fix g_fixPlayerTagState;
extern Fix g_fixSampStateBlockFix;
extern Fix g_fixHealthBarBatch;
extern Fix g_fixObjectDrawLastFast;
extern Fix g_fixObjectMatTextFast;
extern Fix g_fixSampKeyPollCache;
extern Fix g_fixObjectFindCache;
extern Fix g_fixUnprotectCache;
extern Fix g_fixObjectPrefetch;
extern Fix g_fixColLoadedCache;
extern Fix g_fixGangZoneCull;
extern Fix g_fixSampObjectCull;
extern Fix g_fixRadarBatch;
extern Fix g_fixSearchLightCull;
extern Fix g_fixSampTextMemset;
extern Fix g_fixAccessoryFindCache;
extern Fix g_fixTextCache;
extern Fix g_fixMaterialFontCache; // applied by TextCache once its font hook is in

// other modules
extern Fix g_fixSampfuncsDrawFast;
extern Fix g_fixLuaMemProtectCache;
extern Fix g_fixAsiMemProtectCache;
extern Fix g_fixSilentPatchObjectFix;
extern Fix g_fixAudioSleepFix;
extern Fix g_fixSkyboxLogOff;
void SampfuncsDrawFastTick();
void SilentPatchObjectFixTick();
void SampStateBlockFixTick();

// profilers
extern Fix g_fixLuaProfiler;
extern Fix g_fixSampProfiler;

struct IDirect3DDevice9;

void FixesLoad(const Ini &ini);
void FixesStart();                // DllMain
bool FixesNeedSamp();             // some enabled fix waits for samp.dll
bool FixesAttachSamp(BYTE *samp); // true when every samp fix is done
void FixesReport(std::string &out, double secs); // stats of every enabled fix, counters reset
Fix *const *FixList(int &count);
void FixesTickStats();             // every frame on the main thread: refreshes FixesStats() once a second
const std::string &FixesStats();   // stats of the last second
