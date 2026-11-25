// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [StreamMemBoost] GTA SA 1.0 US caps CStreaming::ms_memoryAvailable (0x8A5A80) at ~25-50 MB
// (or even 13.5 MB from stream.ini), forcing the streaming engine to constantly evict and re-read
// map/vehicle/skin models and re-upload textures while driving around the city.
// Raising ms_memoryAvailable up to 3584 MB (~3.5 GB, safely below the 32-bit 4 GB LAA / uint32
// ceiling, and automatically capped to 1400 MB if gta_sa.exe lacks LARGE_ADDRESS_AWARE) keeps
// loaded models and textures resident in RAM instead of thrashing disk/CPU/GPU.

#include "../registry.h"

static const DWORD kStreamMemAvailable = 0x8A5A80; // uint32 bytes (compared unsigned via jae at 0x40E153)
static const DWORD kStreamMemUsed      = 0x8E4CB4; // uint32 bytes

static int g_memMb = 2048;
static bool g_hasLaa = false;

static bool DetectLaa()
{
	BYTE *base = (BYTE *)GetModuleHandleA(nullptr);
	if (!base) return false;
	auto dos = (IMAGE_DOS_HEADER *)base;
	auto nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
	return (nt->FileHeader.Characteristics & IMAGE_FILE_LARGE_ADDRESS_AWARE) != 0;
}

static int EffectiveMb()
{
	int maxMb = g_hasLaa ? 3584 : 1400; // leave room for exe/DLLs/DXVK/heaps; 4096 MB would overflow uint32 to 0
	int mb = g_memMb;
	if (mb < 64) mb = 64;
	if (mb > maxMb) mb = maxMb;
	return mb;
}

static void ApplyLimit()
{
	uint64_t bytes = (uint64_t)EffectiveMb() * 1024ull * 1024ull;
	*(uint32_t *)kStreamMemAvailable = (uint32_t)bytes;
}

static DWORD WINAPI EnforceThread(LPVOID)
{
	// CStreaming::Init2 reads stream.ini during startup and overwrites 0x8A5A80;
	// re-apply for the first 10 seconds so our budget sticks.
	for (int i = 0; i < 40; i++) {
		ApplyLimit();
		Sleep(250);
	}
	return 0;
}

static void Load(const Ini &ini)
{
	g_memMb = ini.Int("StreamMemBoost", "memory_mb", 2048);
	if (g_memMb < 64) g_memMb = 64;
	if (g_memMb > 3584) g_memMb = 3584;
}

static void Apply()
{
	g_hasLaa = DetectLaa();
	ApplyLimit();
	g_fixStreamMemBoost.active = true;
	CloseHandle(CreateThread(nullptr, 0, EnforceThread, nullptr, 0, nullptr));
	int eff = EffectiveMb();
	if (eff < g_memMb)
		Log("StreamMemBoost: gta_sa.exe has no LAA (2 GB limit) -> capped %d MB to %d MB", g_memMb, eff);
	else
		Log("StreamMemBoost: streaming memory pool raised to %d MB (LAA=%d)", eff, (int)g_hasLaa);
}

static void Changed()
{
	ApplyLimit();
}

static void Report(std::string &out, double)
{
	uint32_t used = *(const uint32_t *)kStreamMemUsed;
	uint32_t avail = *(const uint32_t *)kStreamMemAvailable;
	Appendf(out, Tr("StreamMemBoost: занято %.0f / %.0f МБ ОЗУ стриминга\n", "StreamMemBoost: %.0f / %.0f MB streaming RAM used\n"),
	        used / (1024.0 * 1024.0), avail / (1024.0 * 1024.0));
}

static const Param kParams[] = {
	{ "memory_mb", "Память стриминга, МБ", &g_memMb, nullptr, 128, 3584, true },
};

Fix g_fixStreamMemBoost = {
	.section = "StreamMemBoost",
	.title = "Расширенный пул ОЗУ (стриминг)",
	.gain = "без фризов карты",
	.summary = "Отдаёт стримингу GTA SA до 3.5 ГБ ОЗУ (по умолчанию 2 ГБ) вместо 13–50 МБ — карта и моды остаются в памяти.",
	.details = "По умолчанию GTA SA (и stream.ini) ограничивает память под загруженные модели и текстуры всего 13–50 МБ — "
	           "наследие 2004 года. При езде по городу игра постоянно выгружает здания, машины и текстуры из ОЗУ, а через "
	           "пару секунд снова читает их с диска, распаковывает на CPU и заново грузит на видеокарту.\n\n"
	           "Ползунок настраивается от 128 до 3584 МБ (~3.5 ГБ — реальный потолок 32-битного процесса с LAA, т.к. "
	           "остальные ~500 МБ нужны самой игре, DXVK и плагинам, а ровно 4096 МБ переполняют 32-битный счётчик в 0). "
	           "Если у игрока gta_sa.exe без флага LAA (2 ГБ), лимит автоматически безопасно режется до 1400 МБ.",
	.load = Load,
	.gta = true,
	.atStart = Apply,
	.report = Report,
	.changed = Changed,
	.params = kParams,
	.paramCount = Count(kParams),
};
