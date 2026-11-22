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

#include "../registry.h"

struct Vec3 { float x, y, z; };

static const DWORD kRenderSite = 0x493FB7; // call CHeli::SearchLightCone in RenderAllSearchLights
static const DWORD kSearchLightCone = 0x6C58E0;
static const DWORD kRegisterCorona = 0x6FC580;
static const DWORD kIsSphereVisible = 0x420D40; // CCamera::IsSphereVisible(const CVector &, float)
static const DWORD kTheCamera = 0xB6F028;

typedef void(__cdecl *Cone_t)(int, Vec3, Vec3, float, float, BYTE, BYTE, Vec3 *, Vec3 *, Vec3 *, bool, float, float, float, float);
typedef void(__cdecl *Corona_t)(int, void *, BYTE, BYTE, BYTE, BYTE, const Vec3 *, float, float, void *, int, int, int, int, float, bool,
                               float, bool, float, bool, bool);

static DWORD g_drawn, g_culled;

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
		((Cone_t)kSearchLightCone)(id, origin, target, targetRadius, power, clip, shadow, u0, u1, u2, script, baseRadius, a13, a14, a15);
		return;
	}
	g_culled++;
	RegisterLampCorona(id, origin, target);
}

static void Apply()
{
	if (!IsCallTo((BYTE *)kRenderSite, kSearchLightCone)) {
		Log("SearchLightCull: the search light call is not original - skipped");
		return;
	}
	g_fixSearchLightCull.active = RedirectCall((BYTE *)kRenderSite, (void *)hkCone);
	if (g_fixSearchLightCull.active) Log("SearchLightCull: script search lights outside the view are not built");
}

static void Report(std::string &out, double secs)
{
	if (g_drawn + g_culled)
		Appendf(out, "SearchLightCull: конусов прожекторов %.0f/с нарисовано, %.0f/с пропущено вне экрана\n", g_drawn / secs, g_culled / secs);
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
