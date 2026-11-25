// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [SearchLightCull] main.scm puts 7 search lights on Area 69's towers, and CTheScripts::
// RenderAllSearchLights (0x493E30) draws all of them every frame wherever the player is: each
// CHeli::SearchLightCone (0x6C58E0) builds a 41-step cone with x87 sin/cos, normalizations and square
// roots (~8 us), casts a ray, registers the lamp's corona and stores the light spot's shadow.
// A cone whose bounding sphere (origin to target, plus the larger radius and a margin) is outside the
// camera's view is not built. Its corona is still registered with the same arguments (the lamp glares
// only when looked into, and a corona not registered for a frame fades out and back in), so turning
// the camera shows exactly what it did before. The points the cone stores for "is char in search
// light" keep their last value while it is off screen (SA-MP's scripts never ask).

#include <math.h>
#include <stdint.h>

#include "../registry.h"

struct Vec3 { float x, y, z; };
struct RwIm3DVertex { float x, y, z, nx, ny, nz; DWORD color; float u, v; }; // 36 bytes (D3D9)
static_assert(sizeof(RwIm3DVertex) == 0x24, "RwIm3DVertex size mismatch");

static const DWORD kRenderAllSite = 0x53E1D6; // call CTheScripts::RenderAllSearchLights in RenderEffects
static const DWORD kRenderAll = 0x493E30;
static const DWORD kRenderSite = 0x493FB7; // call CHeli::SearchLightCone in RenderAllSearchLights
static const DWORD kSearchLightCone = 0x6C58E0;
static const DWORD kRegisterCorona = 0x6FC580;
static const DWORD kIsSphereVisible = 0x420D40; // CCamera::IsSphereVisible(const CVector &, float)
static const DWORD kTheCamera = 0xB6F028;
static const DWORD kRwIm3DTransform = 0x7EF450;
static const DWORD kRwIm3DRenderIndexedPrimitive = 0x7EF550;
static const DWORD kRwIm3DEnd = 0x7EF520;

typedef void(__cdecl *Cone_t)(int, Vec3, Vec3, float, float, BYTE, BYTE, Vec3 *, Vec3 *, Vec3 *, bool, float, float, float, float);
typedef void(__cdecl *Corona_t)(int, void *, BYTE, BYTE, BYTE, BYTE, const Vec3 *, float, float, void *, int, int, int, int, float, bool,
                               float, bool, float, bool, bool);
typedef void *(__cdecl *RwIm3DTransform_t)(RwIm3DVertex *, uint32_t, void *, uint32_t);
typedef int(__cdecl *RwIm3DRenderIdx_t)(int, uint16_t *, int);
typedef int(__cdecl *RwIm3DEnd_t)();

static DWORD g_drawn, g_culled;
static DWORD g_renderAllNext = kRenderAll;
static bool g_renderAllHooked;
static DWORD g_passDrawn, g_passCulled;
static DWORD g_lastSyncFrame;

// Vanilla RenderAllSearchLights always draws Area 69's 7 cones via RwIm3D between Pre_SearchLightCone
// and Post_SearchLightCone. When all cones are culled and no Fx particles or first-person weapons drew
// in RenderEffects, RenderWare/D3D9 stays in CCoronas' Im2D state (D3DRS_CLIPPING=0, Im2D decl/VB/IB,
// unflushed state cache), which breaks CPostEffects::Render and post-processing hooks (ColorMod/SkyGfx)
// whenever zero particles exist. One degenerate 0-area Im3D triangle restores exact vanilla Im3D pipeline
// state with negligible cost while keeping all cone geometry/raycast/shadow savings.
static void SyncVanillaIm3DState()
{
	static RwIm3DVertex v[3] = {};
	static uint16_t idx[3] = { 0, 0, 0 };
	if (((RwIm3DTransform_t)kRwIm3DTransform)(v, 3, nullptr, 0x18)) {
		((RwIm3DRenderIdx_t)kRwIm3DRenderIndexedPrimitive)(3 /* rwPRIMTYPETRILIST */, idx, 3);
		((RwIm3DEnd_t)kRwIm3DEnd)();
	}
}

static void Normalise(Vec3 &v)
{
	float m = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
	if (m > 0) {
		v.x /= m, v.y /= m, v.z /= m;
	} else {
		v.x = 1.0f; // as CVector::Normalise
	}
}

// what SearchLightCone registers: the lamp's corona at the origin, brighter the more the camera looks
// along the beam (dot^6)
static void RegisterLampCorona(int id, const Vec3 &origin, const Vec3 &target)
{
	Vec3 dir = { target.x - origin.x, target.y - origin.y, target.z - origin.z };
	Normalise(dir);
	BYTE *matrix = *(BYTE **)(kTheCamera + 0x14);
	const float *cam = matrix ? (const float *)(matrix + 0x30) : (const float *)(kTheCamera + 4);
	Vec3 toCam = { cam[0] - origin.x, cam[1] - origin.y, cam[2] - origin.z };
	Normalise(toCam);
	float d = toCam.x * dir.x + toCam.y * dir.y + toCam.z * dir.z;
	if (d < 0) d = 0;
	float d6 = d * d * d * d * d * d;
	((Corona_t)kRegisterCorona)(id, nullptr, 200, 200, 255, (BYTE)(int)(d6 * 255.0f), &origin, d6 * 20.0f, 100.0f, nullptr, 0, 1, 0, 0,
	                            0.0f, false, 1.5f, false, 15.0f, false, false);
}

static void __cdecl hkCone(int id, Vec3 origin, Vec3 target, float targetRadius, float power, BYTE clip, BYTE shadow, Vec3 *u0, Vec3 *u1,
                           Vec3 *u2, bool script, float baseRadius, float a13, float a14, float a15)
{
	float dx = target.x - origin.x, dy = target.y - origin.y, dz = target.z - origin.z;
	Vec3 mid = { origin.x + dx * 0.5f, origin.y + dy * 0.5f, origin.z + dz * 0.5f };
	float r = 0.5f * sqrtf(dx * dx + dy * dy + dz * dz) + (targetRadius > baseRadius ? targetRadius : baseRadius) * 2.0f + 2.0f;
	if (((bool(__thiscall *)(DWORD, const Vec3 &, float))kIsSphereVisible)(kTheCamera, mid, r)) {
		g_drawn++;
		g_passDrawn++;
		g_lastSyncFrame = *(DWORD *)kFrameCounter;
		((Cone_t)kSearchLightCone)(id, origin, target, targetRadius, power, clip, shadow, u0, u1, u2, script, baseRadius, a13, a14, a15);
		return;
	}
	g_culled++;
	g_passCulled++;
	RegisterLampCorona(id, origin, target);
	if (!g_renderAllHooked) {
		DWORD frame = *(DWORD *)kFrameCounter;
		if (frame != g_lastSyncFrame) {
			g_lastSyncFrame = frame;
			SyncVanillaIm3DState();
		}
	}
}

static void __cdecl hkRenderAllSearchLights()
{
	g_passDrawn = 0;
	g_passCulled = 0;
	((void(__cdecl *)())g_renderAllNext)();
	if (g_passDrawn == 0 && g_passCulled > 0)
		SyncVanillaIm3DState();
}

static void Apply()
{
	if (!IsCallTo((BYTE *)kRenderSite, kSearchLightCone)) {
		Log("SearchLightCull: the search light call is not original - skipped");
		return;
	}
	g_fixSearchLightCull.active = RedirectCall((BYTE *)kRenderSite, (void *)hkCone);
	if (g_fixSearchLightCull.active) {
		BYTE *allCall = (BYTE *)kRenderAllSite;
		if (IsCallTo(allCall, kRenderAll) && RedirectCall(allCall, (void *)hkRenderAllSearchLights)) {
			AddCallChain(allCall, &g_renderAllNext);
			g_renderAllHooked = true;
		}
		Log("SearchLightCull: script search lights outside the view are not built");
	}
}

static void Report(std::string &out, double secs)
{
	if (g_drawn + g_culled)
		Appendf(out, Tr("SearchLightCull: конусов прожекторов %.0f/с нарисовано, %.0f/с пропущено вне экрана\n", "SearchLightCull: %.0f/s cones drawn, %.0f/s culled off-screen\n"), g_drawn / secs, g_culled / secs);
	g_drawn = g_culled = 0;
}

Fix g_fixSearchLightCull = {
	.section = "SearchLightCull",
	.title = "Прожекторы вне экрана",
	.gain = "до -1.5% кадра",
	.summary = "Прожекторы Зоны 69 не строятся, когда их луч вне экрана.",
	.details = "В main.scm на вышках Зоны 69 стоят 7 прожекторов, и игра каждый кадр строит конус каждого (41 шаг с синусами, "
	           "корнями и нормализациями, ~8 мкс), где бы ты ни был.\n\nТеперь конус строится, только если он попадает в кадр. "
	           "Блик лампы регистрируется как раньше, поэтому при повороте камеры картинка та же.",
	.gta = true,
	.atStart = Apply,
	.report = Report,
};
