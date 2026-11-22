// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [SkinMatrixSSE] Vectorizes RenderWare's _rpD3D9SkinVertexShaderMatrixUpdate (0x7C78A0):
// For every visible skinned ped each frame, vanilla loops over ~32 bones making 1-2 indirect calls
// to RwMatrixMultiply (64 calls per ped!) and transposing each 4x3 matrix with 24 scalar movs.
// Here invAtomicLTM is kept in XMM registers across all bones, matrix multiplication is inlined in
// SSE2, and transposition uses _MM_TRANSPOSE4_PS -> 3 unaligned 128-bit stores per bone.

#include <immintrin.h>
#include <stdint.h>

#include "../registry.h"

struct RwMatrix {
	float right[4]; // +0x00 (right.x, right.y, right.z, flags)
	float up[4];    // +0x10 (up.x, up.y, up.z, pad1)
	float at[4];    // +0x20 (at.x, at.y, at.z, pad2)
	float pos[4];   // +0x30 (pos.x, pos.y, pos.z, pad3)
};

struct RpHAnimNodeInfo {
	int32_t nodeID;
	int32_t nodeIndex;
	int32_t flags;
	void *pFrame; // +0x0C
};

struct RpHAnimHierarchy {
	int32_t flags;          // +0x00: bit 1 (0x02) = NOMATRICES, bit 14 (0x4000) = LOCALSPACEMATRICES
	int32_t numNodes;       // +0x04
	RwMatrix *pMatrixArray; // +0x08
	void *pMatrixArrayUnaligned;
	RpHAnimNodeInfo *pNodeInfo; // +0x10
};

static const DWORD kSkinMatUpdate   = 0x7C78A0;
static const DWORD kHAnimPluginOff  = 0xC978A4;
static const DWORD kRwEngineInst    = 0xC97B24;
static const DWORD kCacheHier       = 0xC978D4;
static const DWORD kCacheFrame      = 0xC978D8;
static const DWORD kCacheParent     = 0xC978DC;
static const DWORD kGetInvAtomicLtm = 0x7647B0;
static const DWORD kRwFrameGetLtm   = 0x7F0990;

static inline void MulMatSSE(const float *a, __m128 b0, __m128 b1, __m128 b2, __m128 b3,
                             __m128 &r0, __m128 &r1, __m128 &r2, __m128 &r3)
{
	auto mul3 = [&](const float *row) {
		__m128 res = _mm_mul_ps(_mm_set1_ps(row[0]), b0);
		res = _mm_add_ps(res, _mm_mul_ps(_mm_set1_ps(row[1]), b1));
		return _mm_add_ps(res, _mm_mul_ps(_mm_set1_ps(row[2]), b2));
	};
	r0 = mul3(a + 0);
	r1 = mul3(a + 4);
	r2 = mul3(a + 8);
	r3 = _mm_add_ps(mul3(a + 12), b3);
}

static inline void MulMatSSERegs(__m128 a0, __m128 a1, __m128 a2, __m128 a3,
                                 __m128 b0, __m128 b1, __m128 b2, __m128 b3,
                                 __m128 &r0, __m128 &r1, __m128 &r2, __m128 &r3)
{
	auto mul3 = [&](__m128 a) {
		__m128 res = _mm_mul_ps(_mm_shuffle_ps(a, a, _MM_SHUFFLE(0, 0, 0, 0)), b0);
		res = _mm_add_ps(res, _mm_mul_ps(_mm_shuffle_ps(a, a, _MM_SHUFFLE(1, 1, 1, 1)), b1));
		return _mm_add_ps(res, _mm_mul_ps(_mm_shuffle_ps(a, a, _MM_SHUFFLE(2, 2, 2, 2)), b2));
	};
	r0 = mul3(a0);
	r1 = mul3(a1);
	r2 = mul3(a2);
	r3 = _mm_add_ps(mul3(a3), b3);
}

static inline void StoreTransposed4x3(float *dst, __m128 r0, __m128 r1, __m128 r2, __m128 r3)
{
	_MM_TRANSPOSE4_PS(r0, r1, r2, r3);
	_mm_storeu_ps(dst + 0, r0);
	_mm_storeu_ps(dst + 4, r1);
	_mm_storeu_ps(dst + 8, r2);
}

static void __cdecl hkSkinVertexShaderMatrixUpdate(float *outMatrices, BYTE *atomic, BYTE *skin)
{
	const DWORD off = *(const DWORD *)kHAnimPluginOff;
	RpHAnimHierarchy *hier = *(RpHAnimHierarchy **)(atomic + off);
	if (!hier) return;

	const BYTE *rwEngine = *(const BYTE **)kRwEngineInst;
	const uint32_t frameTag = ~(uint32_t)(*(const uint16_t *)(rwEngine + 8));
	const DWORD parent = *(const DWORD *)(atomic + 4);

	if (*(RpHAnimHierarchy **)kCacheHier == hier &&
	    *(uint32_t *)kCacheFrame == frameTag &&
	    *(DWORD *)kCacheParent == parent)
		return;

	*(RpHAnimHierarchy **)kCacheHier = hier;
	*(uint32_t *)kCacheFrame = frameTag;
	*(DWORD *)kCacheParent = parent;

	const RwMatrix *invBones = *(const RwMatrix **)(skin + 0x0C);
	const int numNodes = hier->numNodes;
	if (numNodes <= 0) return;

	const int32_t flags = hier->flags;
	float *dst = outMatrices;

	if (flags & 2) {
		RwMatrix invLtm;
		((void(__cdecl *)(RwMatrix *))kGetInvAtomicLtm)(&invLtm);
		const __m128 i0 = _mm_loadu_ps(invLtm.right);
		const __m128 i1 = _mm_loadu_ps(invLtm.up);
		const __m128 i2 = _mm_loadu_ps(invLtm.at);
		const __m128 i3 = _mm_loadu_ps(invLtm.pos);
		const RpHAnimNodeInfo *nodes = hier->pNodeInfo;
		for (int k = 0; k < numNodes; ++k, dst += 12) {
			const RwMatrix *ltm = ((const RwMatrix *(__cdecl *)(void *))kRwFrameGetLtm)(nodes[k].pFrame);
			__m128 t0, t1, t2, t3, r0, r1, r2, r3;
			MulMatSSE(invBones[k].right,
			          _mm_loadu_ps(ltm->right), _mm_loadu_ps(ltm->up), _mm_loadu_ps(ltm->at), _mm_loadu_ps(ltm->pos),
			          t0, t1, t2, t3);
			MulMatSSERegs(t0, t1, t2, t3, i0, i1, i2, i3, r0, r1, r2, r3);
			StoreTransposed4x3(dst, r0, r1, r2, r3);
		}
		return;
	}

	const RwMatrix *nodeMats = hier->pMatrixArray;
	if (flags & 0x4000) {
		for (int k = 0; k < numNodes; ++k, dst += 12) {
			const RwMatrix &nm = nodeMats[k];
			__m128 r0, r1, r2, r3;
			MulMatSSE(invBones[k].right,
			          _mm_loadu_ps(nm.right), _mm_loadu_ps(nm.up), _mm_loadu_ps(nm.at), _mm_loadu_ps(nm.pos),
			          r0, r1, r2, r3);
			StoreTransposed4x3(dst, r0, r1, r2, r3);
		}
		return;
	}

	RwMatrix invLtm;
	((void(__cdecl *)(RwMatrix *))kGetInvAtomicLtm)(&invLtm);
	const __m128 i0 = _mm_loadu_ps(invLtm.right);
	const __m128 i1 = _mm_loadu_ps(invLtm.up);
	const __m128 i2 = _mm_loadu_ps(invLtm.at);
	const __m128 i3 = _mm_loadu_ps(invLtm.pos);
	for (int k = 0; k < numNodes; ++k, dst += 12) {
		const RwMatrix &nm = nodeMats[k];
		__m128 t0, t1, t2, t3, r0, r1, r2, r3;
		MulMatSSE(invBones[k].right,
		          _mm_loadu_ps(nm.right), _mm_loadu_ps(nm.up), _mm_loadu_ps(nm.at), _mm_loadu_ps(nm.pos),
		          t0, t1, t2, t3);
		MulMatSSERegs(t0, t1, t2, t3, i0, i1, i2, i3, r0, r1, r2, r3);
		StoreTransposed4x3(dst, r0, r1, r2, r3);
	}
}

static void Apply()
{
	static const BYTE expect[] = { 0x8B, 0x44, 0x24, 0x08, 0x8B, 0x0D, 0xA4, 0x78, 0xC9, 0x00, 0x81, 0xEC };
	if (memcmp((void *)kSkinMatUpdate, expect, sizeof(expect)) != 0) {
		Log("SkinMatrixSSE: _rpD3D9SkinVertexShaderMatrixUpdate is not original - skipped");
		return;
	}
	BYTE jmp[5] = { 0xE9 };
	*(int32_t *)(jmp + 1) = Rel32((BYTE *)kSkinMatUpdate, (const void *)hkSkinVertexShaderMatrixUpdate);
	g_fixSkinMatrixSSE.active = PatchBytes(kSkinMatUpdate, nullptr, jmp, 5);
	Log(g_fixSkinMatrixSSE.active
	        ? "SkinMatrixSSE: _rpD3D9SkinVertexShaderMatrixUpdate uses inline SSE2 + _MM_TRANSPOSE4_PS"
	        : "SkinMatrixSSE: patch failed");
}

Fix g_fixSkinMatrixSSE = {
	.section = "SkinMatrixSSE",
	.title = "Матрицы скиннинга педов на SSE2",
	.gain = "-0.5–1% кадра",
	.summary = "Ускоряет расчёт и транспонирование 32 матриц костей для шейдера скиннинга каждого видимого педа.",
	.details = "Перед отрисовкой каждого видимого педа GTA SA перемножает матрицы всех ~32 костей со скелетом "
	           "(_rpD3D9SkinVertexShaderMatrixUpdate, до 64 косвенных вызовов RwMatrixMultiply на каждого педа) и "
	           "поштучно транспонирует 384 float в буфер констант шейдера.\n\n"
	           "Умножение встроено на SSE2 с удержанием обратной матрицы в регистрах XMM, а транспонирование делается "
	           "через _MM_TRANSPOSE4_PS. Картинка на 100% идентична.",
	.gta = true,
	.atStart = Apply,
};
