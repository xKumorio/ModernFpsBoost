// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [ColTriSSE] CCollision::ProcessLineTriangle (0x4140F0, ~1.8% self) and CCollision::TestSphereTriangle
// (0x4165B0, ~1.2% self) are the #1 and #2 hottest functions in gta_sa.exe.
// Vanilla unpacks every int16 normal/vertex through movsx -> stack -> fild (store-forwarding stall on
// every coordinate), checks t < maxTouchDistance only AFTER unpacking all 3 vertices and running the 2D
// triangle test, and in TestSphereTriangle invokes CrossProduct + operator- + Magnitude (sqrt) up to 7 times.
// Here both are implemented in pure SSE2: stall-free int16->float unpacking, early t rejection before
// touching vertices, exact 6-case 2D test for lines, and exact sqrt-free closest-point-on-triangle for spheres.

#include <immintrin.h>
#include <math.h>
#include <stdint.h>

#include "../registry.h"

struct Vec3 { float x, y, z; };
struct ColLine { Vec3 start; float pad0; Vec3 end; float pad1; };
struct ColSphere {
	Vec3 center;     // 0x00
	float radius;    // 0x0C
	uint8_t surface; // 0x10
	uint8_t piece;   // 0x11
	uint8_t light;   // 0x12
	uint8_t pad;     // 0x13
};
struct CompressedVec { int16_t x, y, z; };
struct ColTriangle { uint16_t a, b, c; uint8_t material, light; };
struct ColTrianglePlane { int16_t nx, ny, nz, dist; uint8_t orient; uint8_t pad; };
struct ColPoint {
	Vec3 point;      // 0x00
	float pad0;      // 0x0C
	Vec3 normal;     // 0x10
	float pad1;      // 0x1C
	uint8_t surfA;   // 0x20
	uint8_t pieceA;  // 0x21
	uint8_t lightA;  // 0x22
	uint8_t surfB;   // 0x23
	uint8_t pieceB;  // 0x24
	uint8_t lightB;  // 0x25
	uint16_t pad2;   // 0x26
	float depth;     // 0x28
};
struct StoredCollPoly {
	Vec3 verts[3];   // 0x00..0x23
	uint8_t valid;   // 0x24
	uint8_t pad[3];
	uint32_t light;  // 0x28
};

static const DWORD kProcessLineTri   = 0x4140F0;
static const DWORD kTestSphereTri    = 0x4165B0;
static const DWORD kProcessSphereTri = 0x416BA0;
static const float kInv4096 = 1.0f / 4096.0f;
static const float kInv128  = 1.0f / 128.0f;

static inline __m128 UnpackInt16x4(const void *p)
{
	__m128i w = _mm_loadl_epi64((const __m128i *)p);
	__m128i d = _mm_srai_epi32(_mm_unpacklo_epi16(w, w), 16);
	return _mm_cvtepi32_ps(d);
}

static inline Vec3 UnpackVert(const CompressedVec &v)
{
	return { v.x * kInv128, v.y * kInv128, v.z * kInv128 };
}

static inline float Dot3(__m128 a, __m128 b)
{
	__m128 p = _mm_mul_ps(a, b);
	__m128 y = _mm_shuffle_ps(p, p, _MM_SHUFFLE(1, 1, 1, 1));
	__m128 z = _mm_movehl_ps(p, p);
	return _mm_cvtss_f32(_mm_add_ss(_mm_add_ss(p, y), z));
}

static bool __cdecl hkProcessLineTriangle(const ColLine *line, const CompressedVec *verts,
                                          const ColTriangle *tri, const ColTrianglePlane *plane,
                                          ColPoint *colPoint, float *maxTouchDist, StoredCollPoly *collPoly)
{
	// Unpack plane (nx, ny, nz, dist) and test both endpoints directly in XMM registers (zero stack bounce)
	const __m128 pl = _mm_mul_ps(UnpackInt16x4(plane), _mm_set_ps(kInv128, kInv4096, kInv4096, kInv4096));
	const float dist = _mm_cvtss_f32(_mm_shuffle_ps(pl, pl, _MM_SHUFFLE(3, 3, 3, 3)));

	const __m128 vStart = _mm_loadu_ps(&line->start.x);
	const __m128 vEnd   = _mm_loadu_ps(&line->end.x);
	const float dotS = Dot3(pl, vStart);
	const float dotE = Dot3(pl, vEnd);
	const float s0 = dotS - dist;
	const float s1 = dotE - dist;
	if (!(s0 * s1 <= 0.0f))
		return false;

	const __m128 vDir = _mm_sub_ps(vEnd, vStart);
	const float denom = Dot3(vDir, pl);
	const float t = (dist - dotS) / denom;

	// Reject further hits BEFORE touching the vertex array (vanilla checked this at the very end)
	if (!(t < *maxTouchDist))
		return false;

	const __m128 vP = _mm_add_ps(vStart, _mm_mul_ps(vDir, _mm_set1_ps(t)));
	const float px = _mm_cvtss_f32(vP);
	const float py = _mm_cvtss_f32(_mm_shuffle_ps(vP, vP, _MM_SHUFFLE(1, 1, 1, 1)));
	const float pz = _mm_cvtss_f32(_mm_movehl_ps(vP, vP));

	const CompressedVec &va = verts[tri->a];
	const CompressedVec &vb = verts[tri->b];
	const CompressedVec &vc = verts[tri->c];

	float au, av, v1u, v1v, v2u, v2v, pu, pv;
	switch (plane->orient) {
	case 0: // +X -> (y, z), (a, b, c)
		au = va.y * kInv128; av = va.z * kInv128;
		v1u = vb.y * kInv128; v1v = vb.z * kInv128;
		v2u = vc.y * kInv128; v2v = vc.z * kInv128;
		pu = py; pv = pz;
		break;
	case 1: // -X -> (y, z), (a, c, b)
		au = va.y * kInv128; av = va.z * kInv128;
		v1u = vc.y * kInv128; v1v = vc.z * kInv128;
		v2u = vb.y * kInv128; v2v = vb.z * kInv128;
		pu = py; pv = pz;
		break;
	case 2: // +Y -> (z, x), (a, b, c)
		au = va.z * kInv128; av = va.x * kInv128;
		v1u = vb.z * kInv128; v1v = vb.x * kInv128;
		v2u = vc.z * kInv128; v2v = vc.x * kInv128;
		pu = pz; pv = px;
		break;
	case 3: // -Y -> (z, x), (a, c, b)
		au = va.z * kInv128; av = va.x * kInv128;
		v1u = vc.z * kInv128; v1v = vc.x * kInv128;
		v2u = vb.z * kInv128; v2v = vb.x * kInv128;
		pu = pz; pv = px;
		break;
	case 4: // +Z -> (x, y), (a, b, c)
		au = va.x * kInv128; av = va.y * kInv128;
		v1u = vb.x * kInv128; v1v = vb.y * kInv128;
		v2u = vc.x * kInv128; v2v = vc.y * kInv128;
		pu = px; pv = py;
		break;
	case 5: // -Z -> (x, y), (a, c, b)
		au = va.x * kInv128; av = va.y * kInv128;
		v1u = vc.x * kInv128; v1v = vc.y * kInv128;
		v2u = vb.x * kInv128; v2v = vb.y * kInv128;
		pu = px; pv = py;
		break;
	default:
		return false;
	}

	// Exact 3-edge 2D test matching 0x414640..0x4146D5
	const float du = pu - au, dv = pv - av;
	if ((v2u - au) * dv - (v2v - av) * du < 0.0f) return false;
	if ((v1u - au) * dv - (v1v - av) * du > 0.0f) return false;
	if ((pv - v2v) * (v1u - v2u) - (pu - v2u) * (v1v - v2v) < 0.0f) return false;

	_mm_storel_pi((__m64 *)&colPoint->point.x, vP);
	_mm_store_ss(&colPoint->point.z, _mm_movehl_ps(vP, vP));
	_mm_storel_pi((__m64 *)&colPoint->normal.x, pl);
	_mm_store_ss(&colPoint->normal.z, _mm_movehl_ps(pl, pl));
	colPoint->surfA = 0;
	colPoint->pieceA = 0;
	colPoint->surfB = tri->material;
	colPoint->pieceB = 0;
	colPoint->lightB = tri->light;

	if (collPoly) {
		collPoly->verts[0] = UnpackVert(va);
		collPoly->verts[1] = UnpackVert(vb);
		collPoly->verts[2] = UnpackVert(vc);
		collPoly->valid = 1;
		collPoly->light = tri->light;
	}
	*maxTouchDist = t;
	return true;
}

static bool __cdecl hkTestSphereTriangle(const ColSphere *sphere, const CompressedVec *verts,
                                         const ColTriangle *tri, const ColTrianglePlane *plane)
{
	// Fast plane-distance reject directly in XMM registers (zero stack store-forwarding stall, rejects >90% of tris)
	const __m128 pl = _mm_mul_ps(UnpackInt16x4(plane), _mm_set_ps(kInv128, kInv4096, kInv4096, kInv4096));
	const __m128 sp = _mm_loadu_ps(&sphere->center.x); // [cx, cy, cz, r]
	const float r = _mm_cvtss_f32(_mm_shuffle_ps(sp, sp, _MM_SHUFFLE(3, 3, 3, 3)));
	const float dist = _mm_cvtss_f32(_mm_shuffle_ps(pl, pl, _MM_SHUFFLE(3, 3, 3, 3)));
	const float distToPlane = Dot3(pl, sp) - dist;
	if (!(fabsf(distToPlane) <= r))
		return false;

	const float cx = sphere->center.x, cy = sphere->center.y, cz = sphere->center.z;
	const Vec3 a = UnpackVert(verts[tri->a]);
	const Vec3 b = UnpackVert(verts[tri->b]);
	const Vec3 c = UnpackVert(verts[tri->c]);

	// Exact closest point on triangle (Ericson, Real-Time Collision Detection §5.1.5) — zero sqrt!
	const float abx = b.x - a.x, aby = b.y - a.y, abz = b.z - a.z;
	const float acx = c.x - a.x, acy = c.y - a.y, acz = c.z - a.z;
	const float apx = cx - a.x,  apy = cy - a.y,  apz = cz - a.z;

	const float r2 = r * r;
	auto hitPoint = [&](float qx, float qy, float qz) {
		float dx = cx - qx, dy = cy - qy, dz = cz - qz;
		return dx * dx + dy * dy + dz * dz <= r2;
	};

	const float d1 = abx * apx + aby * apy + abz * apz;
	const float d2 = acx * apx + acy * apy + acz * apz;
	if (d1 <= 0.0f && d2 <= 0.0f) return hitPoint(a.x, a.y, a.z);

	const float bpx = cx - b.x, bpy = cy - b.y, bpz = cz - b.z;
	const float d3 = abx * bpx + aby * bpy + abz * bpz;
	const float d4 = acx * bpx + acy * bpy + acz * bpz;
	if (d3 >= 0.0f && d4 <= d3) return hitPoint(b.x, b.y, b.z);

	const float vc = d1 * d4 - d3 * d2;
	if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
		float v = d1 / (d1 - d3);
		return hitPoint(a.x + v * abx, a.y + v * aby, a.z + v * abz);
	}

	const float cpx = cx - c.x, cpy = cy - c.y, cpz = cz - c.z;
	const float d5 = abx * cpx + aby * cpy + abz * cpz;
	const float d6 = acx * cpx + acy * cpy + acz * cpz;
	if (d6 >= 0.0f && d5 <= d6) return hitPoint(c.x, c.y, c.z);

	const float vb = d5 * d2 - d1 * d6;
	if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
		float w = d2 / (d2 - d6);
		return hitPoint(a.x + w * acx, a.y + w * acy, a.z + w * acz);
	}

	const float va = d3 * d6 - d5 * d4;
	if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
		float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
		return hitPoint(b.x + w * (c.x - b.x), b.y + w * (c.y - b.y), b.z + w * (c.z - b.z));
	}

	// Closest point is inside the triangle face: distance is |distToPlane| <= r, already verified!
	return true;
}

static bool __cdecl hkProcessSphereTriangle(const ColSphere *sphere, const CompressedVec *verts,
                                            const ColTriangle *tri, const ColTrianglePlane *plane,
                                            ColPoint *colPoint, float *maxTouchDistSq)
{
	// A hit must strictly improve a squared distance; a zero distance is already optimal.
	if (*maxTouchDistSq == 0.0f) return false;
	// Fast plane-distance reject in XMM registers (rejects >90% of tris before touching vertices)
	const __m128 pl = _mm_mul_ps(UnpackInt16x4(plane), _mm_set_ps(kInv128, kInv4096, kInv4096, kInv4096));
	const __m128 sp = _mm_loadu_ps(&sphere->center.x); // [cx, cy, cz, r]
	const float r = _mm_cvtss_f32(_mm_shuffle_ps(sp, sp, _MM_SHUFFLE(3, 3, 3, 3)));
	const float distPlane = _mm_cvtss_f32(_mm_shuffle_ps(pl, pl, _MM_SHUFFLE(3, 3, 3, 3)));
	const float distToPlane = Dot3(pl, sp) - distPlane;
	if (!(fabsf(distToPlane) <= r))
		return false;

	const float maxSq = *maxTouchDistSq;
	if (!(distToPlane * distToPlane <= maxSq))
		return false;

	const float cx = sphere->center.x, cy = sphere->center.y, cz = sphere->center.z;
	const Vec3 a = UnpackVert(verts[tri->a]);
	const Vec3 b = UnpackVert(verts[tri->b]);
	const Vec3 c = UnpackVert(verts[tri->c]);

	// Exact closest point Q on triangle (Ericson, Real-Time Collision Detection §5.1.5) — zero sqrt on misses!
	const float abx = b.x - a.x, aby = b.y - a.y, abz = b.z - a.z;
	const float acx = c.x - a.x, acy = c.y - a.y, acz = c.z - a.z;
	const float apx = cx - a.x,  apy = cy - a.y,  apz = cz - a.z;

	float qx, qy, qz;
	const float d1 = abx * apx + aby * apy + abz * apz;
	const float d2 = acx * apx + acy * apy + acz * apz;
	if (d1 <= 0.0f && d2 <= 0.0f) {
		qx = a.x; qy = a.y; qz = a.z;
	} else {
		const float bpx = cx - b.x, bpy = cy - b.y, bpz = cz - b.z;
		const float d3 = abx * bpx + aby * bpy + abz * bpz;
		const float d4 = acx * bpx + acy * bpy + acz * bpz;
		if (d3 >= 0.0f && d4 <= d3) {
			qx = b.x; qy = b.y; qz = b.z;
		} else {
			const float vc = d1 * d4 - d3 * d2;
			if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
				const float v = d1 / (d1 - d3);
				qx = a.x + v * abx; qy = a.y + v * aby; qz = a.z + v * abz;
			} else {
				const float cpx = cx - c.x, cpy = cy - c.y, cpz = cz - c.z;
				const float d5 = abx * cpx + aby * cpy + abz * cpz;
				const float d6 = acx * cpx + acy * cpy + acz * cpz;
				if (d6 >= 0.0f && d5 <= d6) {
					qx = c.x; qy = c.y; qz = c.z;
				} else {
					const float vb = d5 * d2 - d1 * d6;
					if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
						const float w = d2 / (d2 - d6);
						qx = a.x + w * acx; qy = a.y + w * acy; qz = a.z + w * acz;
					} else {
						const float va = d3 * d6 - d5 * d4;
						if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
							const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
							qx = b.x + w * (c.x - b.x); qy = b.y + w * (c.y - b.y); qz = b.z + w * (c.z - b.z);
						} else {
							const float nx = _mm_cvtss_f32(pl);
							const float ny = _mm_cvtss_f32(_mm_shuffle_ps(pl, pl, _MM_SHUFFLE(1, 1, 1, 1)));
							const float nz = _mm_cvtss_f32(_mm_movehl_ps(pl, pl));
							qx = cx - nx * distToPlane;
							qy = cy - ny * distToPlane;
							qz = cz - nz * distToPlane;
						}
					}
				}
			}
		}
	}

	const float dx = cx - qx, dy = cy - qy, dz = cz - qz;
	const float distSq = dx * dx + dy * dy + dz * dz;
	if (!(distSq < r * r) || !(distSq < maxSq))
		return false;

	const float dist = sqrtf(distSq);
	if (dist > 0.0f) {
		const float invD = 1.0f / dist;
		colPoint->normal.x = dx * invD;
		colPoint->normal.y = dy * invD;
		colPoint->normal.z = dz * invD;
	} else {
		// Exact match for CVector::Normalise (0x59C93D) when length == 0
		colPoint->normal.x = 1.0f;
		colPoint->normal.y = 0.0f;
		colPoint->normal.z = 0.0f;
	}

	colPoint->point.x = qx;
	colPoint->point.y = qy;
	colPoint->point.z = qz;
	colPoint->surfA = sphere->surface;
	colPoint->pieceA = sphere->piece;
	colPoint->lightA = sphere->light;
	colPoint->surfB = tri->material;
	colPoint->pieceB = 0;
	colPoint->depth = r - dist;
	*maxTouchDistSq = distSq;
	return true;
}

static void Apply()
{
	static const BYTE expectLine[]    = { 0x83, 0xEC, 0x44, 0x8B, 0x4C, 0x24, 0x54, 0x0F, 0xBF, 0x01 };
	static const BYTE expectSphere[]  = { 0x90, 0xE9, 0xD1, 0xAE, 0xFE, 0xFF };
	static const BYTE expectProcSph[] = { 0x81, 0xEC, 0x38, 0x01, 0x00, 0x00, 0x8B, 0x84, 0x24, 0x48, 0x01, 0x00, 0x00 };
	int done = 0;
	if (!memcmp((void *)kProcessLineTri, expectLine, sizeof(expectLine))) {
		BYTE jmp[5] = { 0xE9 };
		*(int32_t *)(jmp + 1) = Rel32((BYTE *)kProcessLineTri, hkProcessLineTriangle);
		if (PatchBytes(kProcessLineTri, nullptr, jmp, 5)) done++;
	}
	if (!memcmp((void *)kTestSphereTri, expectSphere, sizeof(expectSphere))) {
		BYTE jmp[5] = { 0xE9 };
		*(int32_t *)(jmp + 1) = Rel32((BYTE *)kTestSphereTri, hkTestSphereTriangle);
		if (PatchBytes(kTestSphereTri, nullptr, jmp, 5)) done++;
	}
	if (!memcmp((void *)kProcessSphereTri, expectProcSph, sizeof(expectProcSph))) {
		BYTE jmp[5] = { 0xE9 };
		*(int32_t *)(jmp + 1) = Rel32((BYTE *)kProcessSphereTri, hkProcessSphereTriangle);
		if (PatchBytes(kProcessSphereTri, nullptr, jmp, 5)) done++;
	}
	g_fixColTriSSE.active = done > 0;
	Log("ColTriSSE: %d/3 (ProcessLineTriangle + TestSphereTriangle + ProcessSphereTriangle on SSE2)", done);
}

Fix g_fixColTriSSE = {
	.section = "ColTriSSE",
	.title = "Коллизии треугольников на SSE",
	.gain = "-3–4% кадра",
	.summary = "ProcessLineTriangle, TestSphereTriangle и ProcessSphereTriangle переписаны на SSE2 без x87 и лишних sqrt.",
	.details = "При проверке коллизий лучей и сфер игра на каждый треугольник распаковывает int16-координаты через стек (fild — "
	           "простой store-forwarding на каждом числе), в ProcessLineTriangle проверяет дистанцию только в самом конце, а в "
	           "TestSphereTriangle и ProcessSphereTriangle многократно вызывает CrossProduct и квадратный корень (fsqrt).\n\n"
	           "Здесь плоскости распаковываются одной SSE2-командой, дальние попадания отбрасываются до чтения вершин, "
	           "а тесты сферы с треугольником идут через точную квадратичную дистанцию без лишних sqrt. Физика та же.",
	.gta = true,
	.atStart = Apply,
};
