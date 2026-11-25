// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [ModelNameLookup] CModelInfo::GetModelInfo(name, &index) (0x4C5940) finds a model by name by walking
// all 20000 model slots and comparing name hashes: from the last found slot to the end, then back to
// the start. CFileLoader::LoadCollisionFile calls it for every model of a collision file whose stored
// model number does not match (replaced and added maps): a file of a few hundred models is tens of
// milliseconds on the game's thread, one frame.
// The function is replaced by a table from the name hash to the slot. A hit is checked against the slot
// before it is used, so a table that is out of date can only cost a rebuild (one pass over the slots),
// never a wrong answer. "No such model" is answered from the table only while it was built in this very
// frame of a running game; otherwise the slots are walked again, as the game does. Two models with one
// name are left to the game's own search order.

#include "../registry.h"

static const DWORD kGetModelInfoByName = 0x4C5940;
static const DWORD kGetUppercaseKey = 0x53CF30;      // uint32 CKeyGen::GetUppercaseKey(const char *)
static BYTE **const kModelInfos = (BYTE **)0xA9B0C8; // CBaseModelInfo *ms_modelInfoPtrs[20000]; name hash at +4
static int *const kLastFound = (int *)0xAAE948;      // ms_lastPositionSearched
static const DWORD *const kFrames = (DWORD *)0xB7CB4C; // CTimer::m_FrameCounter
enum { kModels = 20000, kSlots = 65536 };

struct Slot {
	DWORD key;
	short index; // -1 = empty
	short twice; // more than one model has this name
};
static Slot g_table[kSlots];
static bool g_built;
static DWORD g_builtFrame;
static DWORD g_hits, g_rebuilds, g_walks;

static inline DWORD KeyOf(int i)
{
	return *(DWORD *)(kModelInfos[i] + 4);
}

static Slot *Find(DWORD key, bool add)
{
	for (DWORD h = (key * 2654435761u) >> 16;; h = (h + 1) & (kSlots - 1)) {
		Slot &s = g_table[h];
		if (s.index < 0) return add ? &s : nullptr;
		if (s.key == key) return &s;
	}
}

static void Build()
{
	memset(g_table, 0xFF, sizeof(g_table));
	for (int i = 0; i < kModels; i++) {
		if (!kModelInfos[i]) continue;
		DWORD key = KeyOf(i);
		Slot *s = Find(key, true);
		if (s->index < 0) *s = { key, (short)i, 0 };
		else s->twice = 1;
	}
	g_built = true;
	g_builtFrame = *kFrames;
	g_rebuilds++;
}

static BYTE *Found(int i, int *index)
{
	if (index) *index = i;
	*kLastFound = i;
	return kModelInfos[i];
}

// the game's own search
static BYTE *Walk(DWORD key, int *index)
{
	g_walks++;
	int last = *kLastFound;
	for (int i = last; i < kModels; i++)
		if (i >= 0 && kModelInfos[i] && KeyOf(i) == key) return Found(i, index);
	for (int i = last < kModels ? last : kModels - 1; i >= 0; i--)
		if (kModelInfos[i] && KeyOf(i) == key) return Found(i, index);
	return nullptr;
}

static BYTE *__cdecl hkGetModelInfoByName(const char *name, int *index)
{
	DWORD key = ((DWORD(__cdecl *)(const char *))kGetUppercaseKey)(name);
	for (int pass = 0; pass < 2; pass++) {
		Slot *s = g_built ? Find(key, false) : nullptr;
		if (s) {
			if (s->twice) return Walk(key, index);
			if (kModelInfos[s->index] && KeyOf(s->index) == key) {
				g_hits++;
				return Found(s->index, index);
			}
		} else if (g_built && (pass || (g_builtFrame == *kFrames && g_builtFrame))) {
			return nullptr; // the table is of this moment and has no such name
		}
		if (!pass) Build();
	}
	return Walk(key, index);
}

static void Apply()
{
	static const BYTE entry[] = { 0x8B, 0x44, 0x24, 0x04, 0x56, 0x50, 0xE8 }; // mov eax, [esp+4]; push esi; push eax; call
	static const BYTE slots[] = { 0x81, 0xF9, 0x20, 0x4E, 0x00, 0x00 };        // cmp ecx, 20000
	static const BYTE array[] = { 0x8B, 0x04, 0x8D, 0xC8, 0xB0, 0xA9, 0x00 }; // mov eax, [ecx*4 + ms_modelInfoPtrs]
	if (memcmp((void *)kGetModelInfoByName, entry, sizeof(entry)) || memcmp((void *)0x4C5956, slots, sizeof(slots)) ||
	    memcmp((void *)0x4C5960, array, sizeof(array))) {
		Log("ModelNameLookup: CModelInfo::GetModelInfo is not original (a limit adjuster?) - skipped");
		return;
	}
	BYTE jmp[5] = { 0xE9 };
	*(int32_t *)(jmp + 1) = Rel32((BYTE *)kGetModelInfoByName, hkGetModelInfoByName);
	g_fixModelNameLookup.active = PatchBytes(kGetModelInfoByName, entry, jmp, sizeof(jmp));
	if (g_fixModelNameLookup.active) Log("ModelNameLookup: models are found by name through a table");
}

static void Report(std::string &out, double secs)
{
	if (g_hits + g_walks + g_rebuilds)
		Appendf(out, Tr("ModelNameLookup: поисков по имени %.0f/с из таблицы, %.1f/с перестроек таблицы, %.1f/с перебором\n", "ModelNameLookup: %.0f/s table hits, %.1f/s rebuilds, %.1f/s linear scans\n"), g_hits / secs,
		        g_rebuilds / secs, g_walks / secs);
	g_hits = g_rebuilds = g_walks = 0;
}

Fix g_fixModelNameLookup = {
	.section = "ModelNameLookup",
	.title = "Поиск модели по имени",
	.gain = "без фризов 20-30 мс",
	.summary = "Модель по имени ищется по таблице, а не перебором всех 20000 моделей. Нужно при загрузке коллизий.",
	.details = "Чтобы найти модель по имени, игра перебирает все 20000 ячеек моделей и сравнивает хэши имён. При загрузке файла "
	           "коллизий с заменённым или добавленным маппингом так ищется каждая модель файла: несколько сотен моделей — это "
	           "кадр в 20-30 мс.\n\n"
	           "Теперь имя ищется по таблице за одно обращение. Найденное всегда сверяется с самой ячейкой, результат тот же.",
	.gta = true,
	.atStart = Apply,
	.report = Report,
};
