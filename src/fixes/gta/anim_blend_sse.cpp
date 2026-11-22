// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [AnimBlendSSE] Vectorizes GTA SA's hottest ped skeleton blending routines (~1.2-1.5% self in crowds):
//   1. CAnimBlendNode::Update (0x4D06C0)
//   2. CAnimBlendNode::UpdateCompressed (0x4D08D0)
//   3. RpAnimBlend::FrameUpdateCallBackSkinned (0x4D2B90)
// Vanilla copies every bone's keyframes through the stack via x87 fld/fstp, calls CQuaternion::Slerp
// separately per bone per anim, scales quaternions/vectors one scalar float at a time, and uses x87
// fnstsw ax branches for hemisphere alignment and normalization. Here all three run in SSE2 with inlined
// minimax Slerp, branchless hemisphere alignment, and stall-free int16->float unpacking.

#include <immintrin.h>
#include <math.h>
#include <stdint.h>

#include "../registry.h"

struct Vec3 { float x, y, z; };
struct Quat { float x, y, z, w; };

struct AnimBlendSequence {
	uint32_t hash;     // 0x00
	uint16_t flags;    // 0x04: bit 0 = hasRot, bit 1 = hasTrans, bit 3 = compressed
	int16_t numFrames; // 0x06
	void *frames;      // 0x08
};

struct AnimBlendAssoc {
	uint8_t pad0[0x18];
	float blendAmount; // 0x18
	uint8_t pad1[0x0C];
	float timeStep;    // 0x28
	uint16_t pad2;     // 0x2C
	uint16_t flags;    // 0x2E: bit 0 (0x01) = playing, bit 4 (0x10) = partial
};

struct AnimBlendNode {
	float theta;            // 0x00
	float invSinTheta;      // 0x04
	int16_t frameA;         // 0x08
	int16_t frameB;         // 0x0A
	float remainingTime;    // 0x0C
	AnimBlendSequence *seq; // 0x10
	AnimBlendAssoc *assoc;  // 0x14
}; // sizeof == 0x18

struct SkinnedKeyFrame {
	float q[4]; // 0x00
	Vec3 t;     // 0x10
};

struct AnimBlendFrameData {
	uint8_t flags;             // 0x00
	uint8_t pad[3];            // 0x01
	Vec3 bonePos;              // 0x04
	SkinnedKeyFrame *keyFrame; // 0x10
};

struct SkinnedUpdateData {
	int hasPartial;          // 0x00
	AnimBlendNode *nodes[1]; // 0x04, NULL-terminated
};

static const DWORD kUpdate              = 0x4D06C0;
static const DWORD kUpdateCompressed    = 0x4D08D0;
static const DWORD kFrameUpdateSkinned  = 0x4D2B90;
static const DWORD kNextKeyFrame        = 0x4D04A0;
static const DWORD kNextKeyFrameComp    = 0x4D0570;
static const DWORD kVelExtract          = 0x4D1680;
static const DWORD kVelExtract3D        = 0x4D1A50;
static const DWORD kSlerpAddr           = 0x59C300;
static const DWORD kClumpAssocPtr       = 0xB4EA0C;

static const float kInv4096 = 1.0f / 4096.0f;
static const float kInv1024 = 1.0f / 1024.0f;
static const float kInv60   = 1.0f / 60.0f;

static inline __m128 UnpackInt16x4(const void *p)
{
	__m128i w = _mm_loadl_epi64((const __m128i *)p);
	__m128i d = _mm_srai_epi32(_mm_unpacklo_epi16(w, w), 16);
	return _mm_cvtepi32_ps(d);
}

static inline float SinPoly(float x)
{
	const float x2 = x * x;
	return x * (0.9999999765893699f + x2 * (-0.16666647634365314f + x2 * (0.008332899819376866f + x2 * (-0.00019800897549276573f + x2 * 2.5904881178100494e-06f))));
}

static inline __m128 SlerpWeighted(__m128 qFrom, __m128 qTo, float halftheta, float sinInv, float t, float w)
{
	if (halftheta == 0.0f)
		return _mm_mul_ps(qTo, _mm_set1_ps(w));
	if (halftheta > 0.0f && halftheta <= 3.14159274f && t >= 0.0f && t <= 1.0f) {
		float a, b;
		if (halftheta > 1.57079637f) {
			const float th = 3.14159274f - halftheta;
			a = SinPoly((1.0f - t) * th) * sinInv * w;
			b = -(SinPoly(th * t) * sinInv * w);
		} else {
			a = SinPoly((1.0f - t) * halftheta) * sinInv * w;
			b = SinPoly(halftheta * t) * sinInv * w;
		}
		return _mm_add_ps(_mm_mul_ps(qFrom, _mm_set1_ps(a)), _mm_mul_ps(qTo, _mm_set1_ps(b)));
	}
	alignas(16) float f[4], to[4], out[4];
	_mm_store_ps(f, qFrom);
	_mm_store_ps(to, qTo);
	((void(__thiscall *)(float *, const float *, const float *, float, float, float))kSlerpAddr)(out, f, to, halftheta, sinInv, t);
	return _mm_mul_ps(_mm_load_ps(out), _mm_set1_ps(w));
}

static inline bool UpdateNodeUncompressed(AnimBlendNode *self, Vec3 &outTrans, __m128 &outRot, float weight)
{
	outTrans.x = outTrans.y = outTrans.z = 0.0f;
	outRot = _mm_setzero_ps();
	bool looped = false;

	AnimBlendAssoc *assoc = self->assoc;
	if (assoc->flags & 1) {
		self->remainingTime -= assoc->timeStep;
		if (self->remainingTime <= 0.0f)
			looped = ((bool(__thiscall *)(AnimBlendNode *))kNextKeyFrame)(self);
	}

	float w = assoc->blendAmount;
	if (!(assoc->flags & 0x10))
		w *= weight;
	if (!(w > 0.0f))
		return looped;

	const AnimBlendSequence *seq = self->seq;
	const uint16_t sflags = seq->flags;
	const bool hasTrans = (sflags & 2) != 0;
	const int stride = hasTrans ? 32 : 20;
	const BYTE *base = (const BYTE *)seq->frames;
	const float *kfB = (const float *)(base + (int)self->frameB * stride);
	const float *kfA = (const float *)(base + (int)self->frameA * stride);

	const float dt = kfA[4];
	const float t = (dt != 0.0f) ? ((dt - self->remainingTime) / dt) : 0.0f;

	if (hasTrans) {
		const float bx = kfB[5], by = kfB[6], bz = kfB[7];
		outTrans.x = (bx + (kfA[5] - bx) * t) * w;
		outTrans.y = (by + (kfA[6] - by) * t) * w;
		outTrans.z = (bz + (kfA[7] - bz) * t) * w;
	}
	if (sflags & 1) {
		const __m128 qFrom = _mm_loadu_ps(kfB);
		const __m128 qTo   = _mm_loadu_ps(kfA);
		outRot = SlerpWeighted(qFrom, qTo, self->theta, self->invSinTheta, t, w);
	}
	return looped;
}

static bool __fastcall hkUpdate(AnimBlendNode *self, void *, Vec3 *outTrans, Quat *outRot, float weight)
{
	__m128 q;
	const bool looped = UpdateNodeUncompressed(self, *outTrans, q, weight);
	_mm_storeu_ps(&outRot->x, q);
	return looped;
}

static bool __fastcall hkUpdateCompressed(AnimBlendNode *self, void *, Vec3 *outTrans, Quat *outRot, float weight)
{
	outTrans->x = outTrans->y = outTrans->z = 0.0f;
	_mm_storeu_ps(&outRot->x, _mm_setzero_ps());
	bool looped = false;

	AnimBlendAssoc *assoc = self->assoc;
	if (assoc->flags & 1) {
		self->remainingTime -= assoc->timeStep;
		if (self->remainingTime <= 0.0f)
			looped = ((bool(__thiscall *)(AnimBlendNode *))kNextKeyFrameComp)(self);
	}

	float w = assoc->blendAmount;
	if (!(assoc->flags & 0x10))
		w *= weight;
	if (!(w > 0.0f))
		return looped;

	const AnimBlendSequence *seq = self->seq;
	const uint16_t sflags = seq->flags;
	const bool hasTrans = (sflags & 2) != 0;
	const int stride = hasTrans ? 16 : 10;
	const BYTE *base = (const BYTE *)seq->frames;
	const int16_t *kfB = (const int16_t *)(base + (int)self->frameB * stride);
	const int16_t *kfA = (const int16_t *)(base + (int)self->frameA * stride);

	const int16_t dt16 = kfA[4];
	float t = 0.0f;
	if (dt16 != 0) {
		const float dt = (float)dt16 * kInv60;
		t = (dt - self->remainingTime) / dt;
	}

	if (hasTrans) {
		const float scaleW = kInv1024 * w;
		const float bx = (float)kfB[5], by = (float)kfB[6], bz = (float)kfB[7];
		outTrans->x = (bx + ((float)kfA[5] - bx) * t) * scaleW;
		outTrans->y = (by + ((float)kfA[6] - by) * t) * scaleW;
		outTrans->z = (bz + ((float)kfA[7] - bz) * t) * scaleW;
	}
	if (sflags & 1) {
		const __m128 qScale = _mm_set1_ps(kInv4096);
		const __m128 qFrom = _mm_mul_ps(UnpackInt16x4(kfB), qScale);
		const __m128 qTo   = _mm_mul_ps(UnpackInt16x4(kfA), qScale);
		_mm_storeu_ps(&outRot->x, SlerpWeighted(qFrom, qTo, self->theta, self->invSinTheta, t, w));
	}
	return looped;
}

// 0x4D1680 / 0x4D1A50 use custom register calling convention: eax = data, [esp] = frameData (cdecl cleanup)
// Load all parameters into registers BEFORE pushing so esp-relative addressing under /O2 is never shifted.
static __declspec(noinline) void CallVelExtract(AnimBlendFrameData *frameData, SkinnedUpdateData *data, DWORD fn)
{
	__asm {
		mov ecx, fn
		mov edx, frameData
		mov eax, data
		push edx
		call ecx
		add esp, 4
	}
}

static void __cdecl hkFrameUpdateSkinned(AnimBlendFrameData *frameData, SkinnedUpdateData *data)
{
	const uint8_t fflags = frameData->flags;
	if (fflags & 8) {
		const BYTE *clumpAssoc = *(const BYTE **)kClumpAssocPtr;
		if (*(const DWORD *)(clumpAssoc + 0x0C) != 0) {
			CallVelExtract(frameData, data, (fflags & 0x10) ? kVelExtract3D : kVelExtract);
			return;
		}
	}

	float partialWeight = 0.0f;
	if (data->hasPartial) {
		for (AnimBlendNode **p = data->nodes; *p; ++p) {
			const AnimBlendNode *node = *p;
			if (node->seq && (node->assoc->flags & 0x10))
				partialWeight += node->assoc->blendAmount;
		}
	}

	const float nonPartialWeight = 1.0f - partialWeight;
	const __m128 zero = _mm_setzero_ps();
	const __m128 signBit = _mm_castsi128_ps(_mm_set1_epi32((int)0x80000000u));

	__m128 totalRot = zero;
	Vec3 totalTrans = { 0.0f, 0.0f, 0.0f };
	float totalTransWeight = 0.0f;

	for (AnimBlendNode **p = data->nodes; *p; ++p) {
		AnimBlendNode *node = *p;
		if (node->seq) {
			Vec3 trans;
			__m128 rot;
			UpdateNodeUncompressed(node, trans, rot, nonPartialWeight);

			if (node->seq->flags & 2) {
				totalTrans.x += trans.x;
				totalTrans.y += trans.y;
				totalTrans.z += trans.z;
				totalTransWeight += node->assoc->blendAmount;
			}

			// Branchless quaternion hemisphere alignment (dot(rot, totalRot) < 0 => subtract, else add)
			const __m128 prod = _mm_mul_ps(rot, totalRot);
			__m128 sum = _mm_add_ps(prod, _mm_shuffle_ps(prod, prod, _MM_SHUFFLE(2, 3, 0, 1)));
			sum = _mm_add_ss(sum, _mm_movehl_ps(sum, sum));
			__m128 negMask = _mm_and_ps(_mm_cmplt_ss(sum, zero), signBit);
			negMask = _mm_shuffle_ps(negMask, negMask, 0);
			totalRot = _mm_add_ps(totalRot, _mm_xor_ps(rot, negMask));
		}
		*p = node + 1;
	}

	SkinnedKeyFrame *kf = frameData->keyFrame;
	if (!(fflags & 2)) {
		const __m128 sq = _mm_mul_ps(totalRot, totalRot);
		__m128 sum = _mm_add_ps(sq, _mm_shuffle_ps(sq, sq, _MM_SHUFFLE(2, 3, 0, 1)));
		sum = _mm_add_ss(sum, _mm_movehl_ps(sum, sum));
		const float lenSq = _mm_cvtss_f32(sum);
		if (lenSq == 0.0f) {
			// Exact match for CQuaternion::Normalise (0x4D1648)
			alignas(16) float q[4];
			_mm_store_ps(q, totalRot);
			q[3] = 1.0f;
			_mm_storeu_ps(kf->q, _mm_load_ps(q));
		} else {
			const float invLen = 1.0f / sqrtf(lenSq);
			_mm_storeu_ps(kf->q, _mm_mul_ps(totalRot, _mm_set1_ps(invLen)));
		}
	}

	if (!(fflags & 4)) {
		const float oneMinusW = 1.0f - totalTransWeight;
		kf->t.x = totalTrans.x * totalTransWeight + frameData->bonePos.x * oneMinusW;
		kf->t.y = totalTrans.y * totalTransWeight + frameData->bonePos.y * oneMinusW;
		kf->t.z = totalTrans.z * totalTransWeight + frameData->bonePos.z * oneMinusW;
	}
}

static void Apply()
{
	static const BYTE exUpd[]  = { 0x83, 0xEC, 0x24, 0x33, 0xC0, 0x55, 0x8B, 0x6C, 0x24, 0x2C };
	static const BYTE exComp[] = { 0x83, 0xEC, 0x34, 0x53, 0x8B, 0x5C, 0x24, 0x3C, 0x55, 0x56 };
	static const BYTE exSkin[] = { 0x83, 0xEC, 0x3C, 0x55, 0x8B, 0x6C, 0x24, 0x44, 0x8A, 0x45, 0x00 };

	struct Patch { DWORD addr; const BYTE *exp; size_t len; const void *fn; };
	const Patch list[] = {
		{ kUpdate,             exUpd,  sizeof(exUpd),  (const void *)hkUpdate },
		{ kUpdateCompressed,   exComp, sizeof(exComp), (const void *)hkUpdateCompressed },
		{ kFrameUpdateSkinned, exSkin, sizeof(exSkin), (const void *)hkFrameUpdateSkinned },
	};

	int done = 0;
	for (const Patch &p : list) {
		if (memcmp((void *)p.addr, p.exp, p.len) != 0) continue;
		BYTE jmp[5] = { 0xE9 };
		*(int32_t *)(jmp + 1) = Rel32((BYTE *)p.addr, p.fn);
		if (PatchBytes(p.addr, nullptr, jmp, 5)) done++;
	}
	g_fixAnimBlendSSE.active = done > 0;
	Log("AnimBlendSSE: %d/3 (CAnimBlendNode::Update/Compressed + FrameUpdateCallBackSkinned on SSE2)", done);
}

Fix g_fixAnimBlendSSE = {
	.section = "AnimBlendSSE",
	.title = "Смешивание анимаций педов на SSE2",
	.gain = "-1–1.5% кадра",
	.summary = "Ускоряет обновление и смешивание костей скелета в толпе (CAnimBlendNode::Update + FrameUpdateCallBackSkinned).",
	.details = "В толпе игроков GTA SA по 32 раза на каждого педа за кадр смешивает ключевые кадры анимаций старой x87-математикой "
	           "с постоянным копированием кватернионов через стек и ветвлениями fnstsw ax.\n\n"
	           "Обновление узлов, Slerp, выравнивание полусферы кватернионов и нормализация объединены в чистый SSE2 без лишних вызовов. "
	           "Анимации на 100% идентичны.",
	.gta = true,
	.atStart = Apply,
};
