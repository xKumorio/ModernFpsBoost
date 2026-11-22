// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [FrameSyncFast] Replaces RenderWare's recursive _FrameSyncHierarchyRecurse (0x809700, ~0.7% self)
// and _FrameSyncHierarchyRecurseNoLTM (0x809780) with an iterative preorder DFS that:
//   1. Never makes recursive calls on NULL child pointers (half of all skeleton/vehicle frames are leaves).
//   2. Inlines SSE2 RwMatrixMultiply directly (eliminating indirect calls through RwMatrixMultiply +
//      MatrixMultiplySSE's stack/register framing on every bone).
//   3. Caches parent->ltm in XMM4..XMM7 across consecutive siblings sharing the same parent.
//   4. Prefetches child and next sibling frames ahead of LTM computation.

#include <immintrin.h>
#include <stdint.h>

#include "../registry.h"

struct RwLLLink {
	RwLLLink *next;
	RwLLLink *prev;
};

struct RwFrame {
	uint8_t type;         // 0x00
	uint8_t subType;      // 0x01
	uint8_t flags;        // 0x02
	uint8_t privateFlags; // 0x03: bit 2 = syncLTM, bit 3 = syncObj
	RwFrame *parent;      // 0x04
	uint8_t pad0[8];      // 0x08
	float modelling[16];  // 0x10 (4x4, w column is flags/pad)
	float ltm[16];        // 0x50 (4x4, w column is flags/pad)
	RwLLLink objectList;  // 0x90
	RwFrame *child;       // 0x98
	RwFrame *next;        // 0x9C
};

static const DWORD kSyncRecurse      = 0x809700;
static const DWORD kSyncRecurseNoLTM = 0x809780;
static const DWORD kRwEngineInst     = 0xC97B24;
static const DWORD kRwMatrixModOff   = 0xC979BC;

static inline void MulRow3(float *dstRow, const float *aRow, __m128 b0, __m128 b1, __m128 b2)
{
	__m128 r = _mm_mul_ps(_mm_set1_ps(aRow[0]), b0);
	r = _mm_add_ps(r, _mm_mul_ps(_mm_set1_ps(aRow[1]), b1));
	r = _mm_add_ps(r, _mm_mul_ps(_mm_set1_ps(aRow[2]), b2));
	_mm_storel_pi((__m64 *)dstRow, r);
	_mm_store_ss(dstRow + 2, _mm_movehl_ps(r, r));
}

static inline void MulPos3(float *dstRow, const float *aRow, __m128 b0, __m128 b1, __m128 b2, __m128 b3)
{
	__m128 r = _mm_mul_ps(_mm_set1_ps(aRow[0]), b0);
	r = _mm_add_ps(r, _mm_mul_ps(_mm_set1_ps(aRow[1]), b1));
	r = _mm_add_ps(r, _mm_mul_ps(_mm_set1_ps(aRow[2]), b2));
	r = _mm_add_ps(r, b3);
	_mm_storel_pi((__m64 *)dstRow, r);
	_mm_store_ss(dstRow + 2, _mm_movehl_ps(r, r));
}

static inline void SyncAttachedObjects(RwFrame *frame)
{
	RwLLLink *head = &frame->objectList;
	RwLLLink *cur = head->next;
	if (cur == head) return;
	do {
		BYTE *obj = (BYTE *)cur - 8;
		((void(__cdecl *)(void *))(*(void **)(obj + 0x10)))(obj);
		cur = cur->next;
	} while (cur != head);
}

static void __cdecl hkFrameSyncHierarchyRecurse(RwFrame *frame, int flags)
{
	if (!frame) return;

	struct StackEntry { RwFrame *frame; uint8_t flags; };
	StackEntry stack[64];
	int sp = 0;

	const RwFrame *cachedParent = nullptr;
	__m128 p0 = _mm_setzero_ps(), p1 = p0, p2 = p0, p3 = p0;
	uint32_t parentFlags = 0;
	const BYTE *rwEngine = *(const BYTE **)kRwEngineInst;
	const uint32_t idMask = *(const uint32_t *)(rwEngine + *(const uint32_t *)kRwMatrixModOff + 4) & 0x20000u;
	uint8_t curFlags = (uint8_t)flags;

	for (;;) {
		RwFrame *child = frame->child;
		RwFrame *next = frame->next;
		if (child) _mm_prefetch((const char *)child, _MM_HINT_T0);
		if (next) _mm_prefetch((const char *)next, _MM_HINT_T0);

		const uint8_t accum = frame->privateFlags | curFlags;
		if (accum & 4) {
			const RwFrame *parent = frame->parent;
			if (parent != cachedParent) {
				cachedParent = parent;
				p0 = _mm_loadu_ps(parent->ltm + 0);
				p1 = _mm_loadu_ps(parent->ltm + 4);
				p2 = _mm_loadu_ps(parent->ltm + 8);
				p3 = _mm_loadu_ps(parent->ltm + 12);
				parentFlags = *(const uint32_t *)(parent->ltm + 3);
			}
			// Exact match for RwMatrixMultiply (0x7F18B0): handle identity fast-paths AND update dst->flags!
			const uint32_t modFlags = *(const uint32_t *)(frame->modelling + 3);
			if (modFlags & idMask) {
				_mm_storeu_ps(frame->ltm + 0,  p0);
				_mm_storeu_ps(frame->ltm + 4,  p1);
				_mm_storeu_ps(frame->ltm + 8,  p2);
				_mm_storeu_ps(frame->ltm + 12, p3);
			} else if (parentFlags & idMask) {
				_mm_storeu_ps(frame->ltm + 0,  _mm_loadu_ps(frame->modelling + 0));
				_mm_storeu_ps(frame->ltm + 4,  _mm_loadu_ps(frame->modelling + 4));
				_mm_storeu_ps(frame->ltm + 8,  _mm_loadu_ps(frame->modelling + 8));
				_mm_storeu_ps(frame->ltm + 12, _mm_loadu_ps(frame->modelling + 12));
			} else {
				MulRow3(frame->ltm + 0,  frame->modelling + 0,  p0, p1, p2);
				MulRow3(frame->ltm + 4,  frame->modelling + 4,  p0, p1, p2);
				MulRow3(frame->ltm + 8,  frame->modelling + 8,  p0, p1, p2);
				MulPos3(frame->ltm + 12, frame->modelling + 12, p0, p1, p2, p3);
				*(uint32_t *)(frame->ltm + 3) = modFlags & parentFlags;
			}
		}

		SyncAttachedObjects(frame);
		frame->privateFlags &= 0xF3;

		// Re-read child/next after object sync callbacks (matches vanilla)
		child = frame->child;
		next = frame->next;
		if (child) {
			if (next) {
				if (sp < 64) {
					stack[sp++] = { next, curFlags };
				} else {
					hkFrameSyncHierarchyRecurse(child, accum);
					frame = next;
					continue;
				}
			}
			frame = child;
			curFlags = accum;
			continue;
		}
		if (next) {
			frame = next;
			continue;
		}
		if (sp == 0) break;
		--sp;
		frame = stack[sp].frame;
		curFlags = stack[sp].flags;
	}
}

static void __cdecl hkFrameSyncHierarchyRecurseNoLTM(RwFrame *frame)
{
	if (!frame) return;

	RwFrame *stack[64];
	int sp = 0;

	for (;;) {
		RwFrame *child = frame->child;
		RwFrame *next = frame->next;
		if (child) _mm_prefetch((const char *)child, _MM_HINT_T0);
		if (next) _mm_prefetch((const char *)next, _MM_HINT_T0);

		SyncAttachedObjects(frame);
		frame->privateFlags &= 0xF7;

		child = frame->child;
		next = frame->next;
		if (child) {
			if (next) {
				if (sp < 64) {
					stack[sp++] = next;
				} else {
					hkFrameSyncHierarchyRecurseNoLTM(child);
					frame = next;
					continue;
				}
			}
			frame = child;
			continue;
		}
		if (next) {
			frame = next;
			continue;
		}
		if (sp == 0) break;
		frame = stack[--sp];
	}
}

static void Apply()
{
	static const BYTE exSync[]  = { 0x56, 0x8B, 0x74, 0x24, 0x08, 0x85, 0xF6, 0x74, 0x6F, 0x53, 0x55, 0x57 };
	static const BYTE exNoLtm[] = { 0x55, 0x8B, 0x6C, 0x24, 0x08, 0x85, 0xED, 0x74, 0x45, 0x56, 0x57 };

	int done = 0;
	if (!memcmp((void *)kSyncRecurse, exSync, sizeof(exSync))) {
		BYTE jmp[5] = { 0xE9 };
		*(int32_t *)(jmp + 1) = Rel32((BYTE *)kSyncRecurse, hkFrameSyncHierarchyRecurse);
		if (PatchBytes(kSyncRecurse, nullptr, jmp, 5)) done++;
	}
	if (!memcmp((void *)kSyncRecurseNoLTM, exNoLtm, sizeof(exNoLtm))) {
		BYTE jmp[5] = { 0xE9 };
		*(int32_t *)(jmp + 1) = Rel32((BYTE *)kSyncRecurseNoLTM, hkFrameSyncHierarchyRecurseNoLTM);
		if (PatchBytes(kSyncRecurseNoLTM, nullptr, jmp, 5)) done++;
	}
	g_fixFrameSyncFast.active = done > 0;
	Log("FrameSyncFast: %d/2 (iterative SSE2 _FrameSyncHierarchyRecurse + NoLTM)", done);
}

Fix g_fixFrameSyncFast = {
	.section = "FrameSyncFast",
	.title = "Быстрая иерархия костей (FrameSync)",
	.gain = "-0.7–1.2% кадра",
	.summary = "Итеративный обход скелетов и деталей машин со встроенным SSE2-умножением матриц и кешем родительской LTM.",
	.details = "При обновлении поз педов и деталей машин RenderWare рекурсивно обходит дерево RwFrame (_FrameSyncHierarchyRecurse), "
	           "делая пустой рекурсивный вызов даже для конечных костей без детей и косвенный вызов RwMatrixMultiply на каждую кость.\n\n"
	           "Рекурсия заменена на итеративный обход с упреждающей подгрузкой памяти (prefetch), встроенным SSE2-умножением и "
	           "кешированием матрицы родителя между соседними узлами. Порядок обхода и результат 100% идентичны.",
	.gta = true,
	.atStart = Apply,
};
