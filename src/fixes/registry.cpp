// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
#include "registry.h"

// Order matters for samp.dll: SampProfiler times only call sites no fix has redirected, so it comes
// after them; TextCache waits for SA-MP's fonts and comes last.
static Fix *const kFixes[] = {
	&g_fixColPlaneCache,
	&g_fixRemotePedPhysics,
	&g_fixRemoteInAir,
	&g_fixRefPool,
	&g_fixPedDrawDistance,
	&g_fixVehicleLod,
	&g_fixDrawDistance,
	&g_fixStreamBudget,
	&g_fixModelNameLookup,
	&g_fixSampModelWait,
	&g_fixMatrixSSE,
	&g_fixCMatrixSSE,
	&g_fixOcclusionOff,
	&g_fixFtolSSE3,
	&g_fixFloorSSE4,
	&g_fixFrustumSSE,
	&g_fixColTriSSE,
	&g_fixColBoxSSE,
	&g_fixColLinesSSE,
	&g_fixFastAtan2SSE,
	&g_fixFastStricmpSSE,
	&g_fixMsgKeysFast,
	&g_fixFxParticlesSSE,
	&g_fixStreamMemBoost,
	&g_fixEnvMapCache,
	&g_fixCursorCenterFix,
	&g_fixMessagePumpFix,
	&g_fixAnimMathSSE,
	&g_fixAnimBlendSSE,
	&g_fixFrameSyncFast,
	&g_fixSkinMatrixSSE,
	&g_fixRadarBatch,
	&g_fixSearchLightCull,

	&g_fixNametagLosCache,
	&g_fixObjectSkip,
	&g_fixSampRotationSSE,
	&g_fixPlayerLoop,
	&g_fixPlayerTagState,
	&g_fixSampStateBlockFix,
	&g_fixHealthBarBatch,
	&g_fixObjectDrawLastFast,
	&g_fixObjectMatTextFast,
	&g_fixSampKeyPollCache,
	&g_fixObjectFindCache,
	&g_fixUnprotectCache,
	&g_fixObjectPrefetch,
	&g_fixSampfuncsDrawFast,
	&g_fixLuaMemProtectCache,
	&g_fixAsiMemProtectCache,
	&g_fixSilentPatchObjectFix,
	&g_fixColLoadedCache,
	&g_fixSampTextMemset,
	&g_fixAccessoryFindCache,
	&g_fixGangZoneCull,
	&g_fixSampObjectCull,
	&g_fixAudioSleepFix,
	&g_fixSkyboxLogOff,
	&g_fixLuaProfiler,
	&g_fixSampProfiler,
	&g_fixTextCache,
	&g_fixMaterialFontCache,
};

static bool RunFixLoad(Fix *f, const Ini &ini)
{
	__try {
		f->load(ini);
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		Log("%s: exception 0x%08lX during load - fix disabled", f->section, GetExceptionCode());
		f->enabled = false;
		f->active = false;
		f->attached = true;
		return false;
	}
}

static void RunFixStart(Fix *f)
{
	__try {
		f->atStart();
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		Log("%s: exception 0x%08lX during startup - fix disabled", f->section, GetExceptionCode());
		f->enabled = false;
		f->active = false;
		f->attached = true;
	}
}

static bool RunFixAttachSamp(Fix *f, BYTE *samp)
{
	__try {
		return f->atSamp(samp);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		Log("%s: exception 0x%08lX during SA-MP attach - fix disabled", f->section, GetExceptionCode());
		f->enabled = false;
		f->active = false;
		f->attached = true;
		return true;
	}
}

static void RunFixReport(Fix *f, std::string &out, double secs)
{
	__try {
		f->report(out, secs);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		Log("%s: exception 0x%08lX during report - reporting disabled", f->section, GetExceptionCode());
		f->report = nullptr;
	}
}

static void RunTickGuard(const char *name, void (*fn)(), bool &failed)
{
	if (failed) return;
	__try {
		fn();
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		failed = true;
		Log("%s: exception 0x%08lX during tick - tick disabled", name, GetExceptionCode());
	}
}

void FixesLoad(const Ini &ini)
{
	for (Fix *f : kFixes) {
		f->enabled = ini.Bool(f->section, "enabled", f->defaultOn);
		if (f->enabled && f->load) RunFixLoad(f, ini);
	}
}

void FixesStart()
{
	const bool gta = IsGta10US();
	bool skipped = false;
	for (Fix *f : kFixes) {
		if (!f->enabled || !f->atStart) continue;
		if (f->gta && !gta) {
			skipped = true;
			continue;
		}
		RunFixStart(f);
	}
	if (skipped) Log("gta_sa.exe is not 1.0 US - GTA 1.0 US fixes skipped");
}

bool FixesNeedSamp()
{
	for (Fix *f : kFixes)
		if (f->enabled && f->atSamp) return true;
	return false;
}

bool FixesAttachSamp(BYTE *samp)
{
	bool all = true;
	for (Fix *f : kFixes) {
		if (!f->enabled || !f->atSamp || f->attached) continue;
		f->attached = RunFixAttachSamp(f, samp);
		all &= f->attached;
	}
	return all;
}

void FixesReport(std::string &out, double secs)
{
	for (Fix *f : kFixes)
		if (f->enabled && f->report) RunFixReport(f, out, secs);
}

Fix *const *FixList(int &count)
{
	count = sizeof(kFixes) / sizeof(kFixes[0]);
	return kFixes;
}

static std::string g_stats;
static DWORD g_statsAt;

void FixesTickStats()
{
	DWORD now = GetTickCount();
	if (!g_statsAt) {
		g_statsAt = now;
		return;
	}
	if (now - g_statsAt < 1000) return;
	double secs = (now - g_statsAt) / 1000.0;
	g_statsAt = now;
	static bool sfFail, spFail, sbFail;
	RunTickGuard("SampfuncsDrawFast", SampfuncsDrawFastTick, sfFail);
	RunTickGuard("SilentPatchObjectFix", SilentPatchObjectFixTick, spFail);
	RunTickGuard("SampStateBlockFix", SampStateBlockFixTick, sbFail);
	g_stats.clear();
	FixesReport(g_stats, secs);
}

const std::string &FixesStats()
{
	return g_stats;
}
