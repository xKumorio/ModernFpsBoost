// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [PlayerTagState] Every frame SA-MP 0.3.7-R1 calls CPlayerTags::BeginHealthBar / EndHealthBar
// (samp+0x6FC81 / 0x6FE7D) and CPlayerTags::BeginLabel / EndLabel (samp+0x70D71 / 0x70FAE), even
// when 0 players have visible tags or health bars. Both pairs capture and apply a full D3DSBT_ALL
// IDirect3DStateBlock9 (~200 D3D9 states via ID3DXSprite / StateBlock), costing ~1-2.5% of the frame.
// Here Begin* is deferred until the first Draw* actually fires in that frame; if nothing is drawn,
// both Begin* and End* are skipped with zero D3D9 state churn.

#include "../registry.h"

static void(__thiscall *oBeginHealthBar)(void *);
static void(__thiscall *oEndHealthBar)(void *);
static void(__thiscall *oBeginLabel)(void *);
static void(__thiscall *oEndLabel)(void *);
static DWORD g_origDrawHealthBar, g_origDrawLabel;

static bool g_hbDeferred, g_hbActive;
static bool g_lbDeferred, g_lbActive;
static unsigned g_calls, g_skipped;

static void __fastcall hkBeginHealthBar(void *self)
{
	g_calls++;
	g_hbDeferred = true;
	g_hbActive = false;
}

static void __fastcall EnsureHealthBarBegun(void *self)
{
	if (g_hbDeferred && !g_hbActive) {
		g_hbActive = true;
		oBeginHealthBar(self);
	}
}

static __declspec(naked) void hkDrawHealthBar()
{
	__asm {
		cmp byte ptr [g_hbDeferred], 0
		je go
		cmp byte ptr [g_hbActive], 0
		jne go
		push ecx
		call EnsureHealthBarBegun
		pop ecx
	go:
		jmp dword ptr [g_origDrawHealthBar]
	}
}

static void __fastcall hkEndHealthBar(void *self)
{
	if (g_hbActive) {
		g_hbActive = false;
		g_hbDeferred = false;
		oEndHealthBar(self);
	} else {
		g_hbDeferred = false;
		g_skipped++;
	}
}

static void __fastcall hkBeginLabel(void *self)
{
	g_calls++;
	g_lbDeferred = true;
	g_lbActive = false;
}

static void __fastcall EnsureLabelBegun(void *self)
{
	if (g_lbDeferred && !g_lbActive) {
		g_lbActive = true;
		oBeginLabel(self);
	}
}

static __declspec(naked) void hkDrawLabel()
{
	__asm {
		cmp byte ptr [g_lbDeferred], 0
		je go
		cmp byte ptr [g_lbActive], 0
		jne go
		push ecx
		call EnsureLabelBegun
		pop ecx
	go:
		jmp dword ptr [g_origDrawLabel]
	}
}

static void __fastcall hkEndLabel(void *self)
{
	if (g_lbActive) {
		g_lbActive = false;
		g_lbDeferred = false;
		oEndLabel(self);
	} else {
		g_lbDeferred = false;
		g_skipped++;
	}
}

static bool Apply(BYTE *)
{
	struct Site { SampId callId, targetId; const void *hook; };
	const Site sites[6] = {
		{ S_TAG_BEGIN_HB_SITE, S_TAG_BEGIN_HB, (const void *)hkBeginHealthBar },
		{ S_TAG_DRAW_HB_SITE,  S_TAG_DRAW_HB,  (const void *)hkDrawHealthBar },
		{ S_TAG_END_HB_SITE,   S_TAG_END_HB,   (const void *)hkEndHealthBar },
		{ S_TAG_BEGIN_LB_SITE, S_TAG_BEGIN_LB, (const void *)hkBeginLabel },
		{ S_TAG_DRAW_LB_SITE,  S_TAG_DRAW_LB,  (const void *)hkDrawLabel },
		{ S_TAG_END_LB_SITE,   S_TAG_END_LB,   (const void *)hkEndLabel },
	};
	BYTE *callPtrs[6];
	for (int i = 0; i < 6; i++) {
		callPtrs[i] = SampAt(sites[i].callId);
		BYTE *tgt = SampAt(sites[i].targetId);
		if (!callPtrs[i] || !tgt || !IsCallTo(callPtrs[i], (DWORD)tgt)) {
			Log("PlayerTagState: call site %d missing or changed on %s - skipped", i, SampVersionName());
			return true;
		}
	}
	oBeginHealthBar     = (void(__thiscall *)(void *))SampAt(S_TAG_BEGIN_HB);
	oEndHealthBar       = (void(__thiscall *)(void *))SampAt(S_TAG_END_HB);
	oBeginLabel         = (void(__thiscall *)(void *))SampAt(S_TAG_BEGIN_LB);
	oEndLabel           = (void(__thiscall *)(void *))SampAt(S_TAG_END_LB);
	g_origDrawHealthBar = (DWORD)SampAt(S_TAG_DRAW_HB);
	g_origDrawLabel     = (DWORD)SampAt(S_TAG_DRAW_LB);

	BYTE *addrs[6];
	const void *data[6];
	size_t sizes[6];
	int32_t rel[6];
	for (int i = 0; i < 6; i++) {
		BYTE *p = callPtrs[i];
		rel[i] = Rel32(p, sites[i].hook);
		addrs[i] = p + 1;
		data[i] = &rel[i];
		sizes[i] = 4;
	}
	if ((g_fixPlayerTagState.active = PatchMainThread(addrs, data, sizes, 6)))
		Log("PlayerTagState: CPlayerTags Begin/End deferred until a tag is drawn (%s)", SampVersionName());
	else
		Log("PlayerTagState: patch failed");
	return true;
}

static void Report(std::string &out, double secs)
{
	if (g_calls)
		Appendf(out, "PlayerTagState: пропущено %.0f%% пустых проходов Begin/End (%.0f/с)\n", 100.0 * g_skipped / g_calls, g_skipped / secs);
	g_calls = g_skipped = 0;
}

Fix g_fixPlayerTagState = {
	.section = "PlayerTagState",
	.title = "Ленивый рендер ников и HP",
	.gain = "-1–2.5% кадра",
	.summary = "SA-MP больше не сохраняет и не восстанавливает все стейты D3D9, когда рядом нет видимых ников или полосок HP.",
	.details = "Каждый кадр SA-MP вызывает BeginHealthBar/EndHealthBar и BeginLabel/EndLabel, а внутри них срабатывают "
	           "IDirect3DStateBlock9::Capture/Apply (D3DSBT_ALL — ~200 стейтов видеокарты) и ID3DXSprite::Begin/End — "
	           "даже если в кадре вообще нет игроков с видимым ником.\n\n"
	           "Здесь Begin вызывается только перед первой реальной отрисовкой ника или полоски HP в кадре; "
	           "если рисовать нечего, оба тяжёлых переключения стейтов полностью пропускаются. Картинка та же.",
	.atSamp = Apply,
	.report = Report,
};
