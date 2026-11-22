// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [RefPool] CEntity::RegisterReference (0x571B70) takes a CReference { next, CEntity **slot } from
// CReferences::pEmptyList (0xBA1768, 3000 static entries). When the list is empty it prunes every
// ped, vehicle and object until one frees something; in a crowd all refs are live, so every call
// walks all pools for nothing and the reference is dropped. The load of the list head at 0x571BB3
// is replaced by RefPoolHead: prune every entity once a second (a slice every frame), grow the list
// with new blocks when it is still empty.

#include "../registry.h"

static const DWORD kRefEmptyList = 0xBA1768;
static const DWORD kRefHeadLoad = 0x571BB3;
static const DWORD kPruneReferences = 0x571A90; // CEntity::PruneReferences
static int g_refBlock = 4096, g_refMaxBlocks = 64, g_refBlocks;
static DWORD g_refPrunedAt;

// Prunes slots [from, to) of a pool; returns its size.
static int PrunePool(DWORD poolPtr, int stride, int from, int to)
{
	typedef void(__thiscall * Prune)(void *);
	GtaPool *p = *(GtaPool **)poolPtr;
	if (!p) return 0;
	if (to > p->size) to = p->size;
	for (int i = from; i < to; i++)
		if (p->flags[i] >= 0) ((Prune)kPruneReferences)(p->objects + i * stride);
	return p->size;
}

// Every entity once a second, as a 1/64 slice every ~16 ms instead of all at once.
static void PruneSlice()
{
	static const struct { DWORD pool; int stride; } pools[] = { { kPedPool, 0x7C4 }, { kVehiclePool, 0xA18 }, { kObjectPool, kObjectSize } };
	static int cursor;
	int total = 0;
	for (auto &q : pools) if (GtaPool *p = *(GtaPool **)q.pool) total += p->size;
	if (!total) return;
	int n = (total + 63) / 64, base = 0;
	if (cursor >= total) cursor = 0;
	for (auto &q : pools) {
		GtaPool *p = *(GtaPool **)q.pool;
		int size = p ? p->size : 0;
		if (cursor < base + size && cursor + n > base) PrunePool(q.pool, q.stride, (cursor > base ? cursor - base : 0), cursor + n - base);
		base += size;
	}
	cursor += n;
}

static DWORD __cdecl RefPoolHead()
{
	// stale refs (the pointer was overwritten) go back once a second, as the game did when empty;
	// the extra blocks would otherwise keep the list from ever running empty and lists would grow
	DWORD &head = *(DWORD *)kRefEmptyList;
	DWORD now = GetTickCount();
	if (now - g_refPrunedAt >= 16) {
		g_refPrunedAt = now;
		PruneSlice();
	}
	if (!head && g_refBlocks < g_refMaxBlocks) {
		// VirtualAlloc memory is zeroed: every entry's slot starts as nullptr
		DWORD *b = (DWORD *)VirtualAlloc(nullptr, g_refBlock * 8, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (b) {
			for (int i = 0; i + 1 < g_refBlock; i++) b[2 * i] = (DWORD)&b[2 * i + 2];
			head = (DWORD)b;
			g_refBlocks++;
		}
	}
	return head; // still 0 only past max_blocks: the game prunes everything itself as before
}

static void Load(const Ini &ini)
{
	g_refBlock = max(256, ini.Int("RefPool", "block", 4096));
	g_refMaxBlocks = max(0, ini.Int("RefPool", "max_blocks", 64));
}

static void Apply()
{
	static const BYTE expect[] = { 0xA1, 0x68, 0x17, 0xBA, 0x00 }; // mov eax, [pEmptyList]
	BYTE call[5] = { 0xE8 };
	*(int32_t *)(call + 1) = Rel32((BYTE *)kRefHeadLoad, RefPoolHead);
	if ((g_fixRefPool.active = PatchBytes(kRefHeadLoad, expect, call, 5))) Log("RefPool: on (block %d, max %d blocks)", g_refBlock, g_refMaxBlocks);
	else Log("RefPool: code at 0x571BB3 is not original - skipped");
}

static void Report(std::string &out, double)
{
	Appendf(out, "RefPool: добавлено блоков: %d по %d ссылок\n", g_refBlocks, g_refBlock);
}

static const Param kParams[] = {
	{ "block", "Ссылок в блоке", &g_refBlock, nullptr, 256, 16384, true },
	{ "max_blocks", "Максимум блоков", &g_refMaxBlocks, nullptr, 0, 256, true },
};

Fix g_fixRefPool = {
	.section = "RefPool",
	.title = "Пул ссылок",
	.gain = "до -30% кадра",
	.summary = "Пул ссылок между объектами больше не забивается: пропадает полная чистка всех педов и машин на каждую новую ссылку.",
	.details = "Когда объект игры запоминает другой (цель ИИ, сосед, событие), GTA пишет ссылку в общий пул на 3000 мест. "
	           "В толпе пул забит, и каждая новая ссылка запускает чистку всех педов, машин и объектов, которая ничего не находит.\n\n"
	           "Плагин чистит устаревшие ссылки понемногу каждый кадр, а если мест нет, добавляет новый блок. Без потерь, ссылки больше не теряются.",
	.load = Load,
	.gta = true,
	.atStart = Apply,
	.report = Report,
	.params = kParams,
	.paramCount = Count(kParams),
};
