// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [PedDrawDistance] Other players' peds farther than max_m from the camera are not drawn
// (CPlayerPed::Render, vtable slot 18 at 0x86D1B0; nothing else of the ped changes: its weapon is
// queued for drawing by its own Render, so it goes with it). SA-MP puts CPed_Render_Hook into that
// slot when the game starts, so the hook is installed from its own thread after that and wraps SA-MP's.

#include "../registry.h"

static const int kVtRender = 18;
static const DWORD kPedRender = 0x5E7680;

static float g_pedMaxDist = 150.0f;
static void(__thiscall *oPedRender)(void *);

static void __fastcall hkPedRender(void *ped, void *)
{
	if (g_pedMaxDist > 0 && ped != LocalPed()) {
		const float *a = EntityPos(ped);
		float dx = a[0] - kCamPos[0], dy = a[1] - kCamPos[1], dz = a[2] - kCamPos[2];
		if (dx * dx + dy * dy + dz * dz > g_pedMaxDist * g_pedMaxDist) return;
	}
	oPedRender(ped);
}

static DWORD WINAPI PedDrawDistanceThread(LPVOID)
{
	void **vt = (void **)kPlayerPedVtbl;
	for (int i = 0; i < 3000 && vt[kVtRender] == (void *)kPedRender; i++) Sleep(100);
	Sleep(500);
	oPedRender = (void(__thiscall *)(void *))vt[kVtRender];
	void *hook = (void *)hkPedRender;
	if (oPedRender == hook) return 0;
	extern Fix g_fixPedDrawDistance;
	g_fixPedDrawDistance.active = PatchBytes((DWORD)&vt[kVtRender], nullptr, &hook, 4);
	Log("PedDrawDistance: other players beyond %.0f m are not drawn (wraps %p)", g_pedMaxDist, oPedRender);
	return 0;
}

static void Load(const Ini &ini)
{
	g_pedMaxDist = ini.Float("PedDrawDistance", "max_m", 150.0f);
}

static void Apply()
{
	if (g_pedMaxDist > 0) CloseHandle(CreateThread(nullptr, 0, PedDrawDistanceThread, nullptr, 0, nullptr));
}

static const Param kParams[] = {
	{ "max_m", "Дальность, м", nullptr, &g_pedMaxDist, 30, 500, true },
};

Fix g_fixPedDrawDistance = {
	.section = "PedDrawDistance",
	.title = "Дальность прорисовки игроков",
	.gain = "толпа вдали",
	.summary = "Чужие игроки дальше max_m от камеры не рисуются. Ники и синхронизация как раньше.",
	.details = "GTA рисует игрока на любом расстоянии, на которое его прислал сервер (200-300 м), хотя дальше ~150 м это пара пикселей, "
	           "а стоит он десятки вызовов видеокарты и скиннинг.\n\n"
	           "Дальше max_m игрок не рисуется вместе с оружием. Ник, синхронизация, попадания и твой пед не меняются.",
	.load = Load,
	.gta = true,
	.atStart = Apply,
	.params = kParams,
	.paramCount = Count(kParams),
};
