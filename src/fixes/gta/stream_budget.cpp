// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [StreamBudget] The disk thread reads up to 16 models in one request; when the read is done,
// CStreaming::ProcessLoadingChannel (0x40E170) turns all of them into game objects (textures, geometry,
// collision) in one frame on the game's thread: 1.5-4 MB at once is a 10-15 ms frame.
// The per-frame call (0x15670CA in CStreaming::LoadRequestedModels) is wrapped: the models past
// budget_kb are taken out of the channel for the call and put back after it, and the channel stays
// "reading", so the game comes back for them on its next turn. The order is kept (a texture dictionary
// before the model that uses it). Loads that must finish at once (LoadAllRequestedModels, FlushChannels)
// call the function directly and convert everything that is left, as before.
// A model too big for one channel (ms_bLoadingBigModel) is loaded in two steps by the game: untouched.

#include "../registry.h"

static const DWORD kSite = 0x15670CA; // call ProcessLoadingChannel in LoadRequestedModels
static const DWORD kProcessLoadingChannel = 0x40E170;
static BYTE *const kChannels = (BYTE *)0x8E4A60;     // tStreamingChannel ms_channel[2], 0x98 each
static const BYTE *const kLoadingBig = (BYTE *)0x8E4A58; // ms_bLoadingBigModel
static const BYTE *const kInfo = (BYTE *)0x8E4CC0;   // CStreamingInfo ms_aInfoForModel[], 20 bytes each
enum { kChannelSize = 0x98, kIds = 16, kStatusOff = 0x80, kInfoSize = 20, kInfoCdSize = 0xC, kSector = 2048, kReading = 1, kIdle = 0 };

static int g_budgetKb = 96;
static DWORD g_split, g_whole;

static bool __cdecl hkProcessLoadingChannel(int ch)
{
	auto process = (bool(__cdecl *)(int))kProcessLoadingChannel;
	int *ids = (int *)(kChannels + ch * kChannelSize);
	int *status = (int *)((BYTE *)ids + kStatusOff);
	if (*kLoadingBig || *status != kReading) return process(ch);

	int later[kIds], laterN = 0, kept = 0;
	DWORD sectors = 0, budget = (DWORD)g_budgetKb * 1024 / kSector;
	for (int i = 0; i < kIds; i++) {
		later[i] = -1;
		int id = ids[i];
		if (id < 0) continue;
		sectors += *(const DWORD *)(kInfo + id * kInfoSize + kInfoCdSize);
		// at least one a call; once over the budget, everything after it waits (the order is kept)
		if (kept && (laterN || sectors > budget)) {
			later[i] = id;
			ids[i] = -1;
			laterN++;
		} else {
			kept++;
		}
	}
	if (!laterN) {
		g_whole++;
		return process(ch);
	}
	bool done = process(ch);
	for (int i = 0; i < kIds; i++)
		if (later[i] >= 0) ids[i] = later[i];
	// the data is still in the channel's buffer: "reading" keeps the game from starting a new read into it
	if (done && *status == kIdle) *status = kReading;
	if (done) g_split++;
	return done;
}

static void Load(const Ini &ini)
{
	g_budgetKb = ini.Int("StreamBudget", "budget_kb", 96);
	if (g_budgetKb < 16) g_budgetKb = 16;
}

static void Apply()
{
	if (!IsCallTo((BYTE *)kSite, kProcessLoadingChannel)) {
		Log("StreamBudget: the ProcessLoadingChannel call is not original - skipped");
		return;
	}
	g_fixStreamBudget.active = RedirectCall((BYTE *)kSite, (void *)hkProcessLoadingChannel);
	if (g_fixStreamBudget.active) Log("StreamBudget: at most %d KB of models are unpacked per frame", g_budgetKb);
}

static void Report(std::string &out, double secs)
{
	if (g_split + g_whole)
		Appendf(out, Tr("StreamBudget: пачек моделей %.1f/с разбито на несколько кадров, %.1f/с уложились в один\n", "StreamBudget: %.1f/s batches split across frames, %.1f/s fit in one\n"), g_split / secs, g_whole / secs);
	g_split = g_whole = 0;
}

static const Param kParams[] = {
	{ "budget_kb", "За кадр, КБ", &g_budgetKb, nullptr, 16, 1024, true },
};

Fix g_fixStreamBudget = {
	.section = "StreamBudget",
	.title = "Подгрузка моделей по частям",
	.gain = "без фризов 10-15 мс",
	.summary = "Прочитанные с диска модели распаковываются не все в одном кадре, а по budget_kb за кадр.",
	.details = "Диск читает до 16 моделей одним запросом, а игра потом распаковывает их все сразу в одном кадре (текстуры, "
	           "геометрия, коллизия): 1.5-4 МБ за раз — это кадр в 10-15 мс при езде и полёте.\n\n"
	           "Теперь за кадр распаковывается не больше budget_kb, остальное — в следующих кадрах, в том же порядке. "
	           "Модели появляются на несколько кадров позже. Срочные загрузки (телепорт, спавн) идут как раньше.",
	.load = Load,
	.gta = true,
	.atStart = Apply,
	.report = Report,
	.params = kParams,
	.paramCount = Count(kParams),
};
