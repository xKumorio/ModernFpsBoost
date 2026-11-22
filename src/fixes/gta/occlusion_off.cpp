// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [OcclusionOff] COcclusion::ProcessBeforeRendering (0x7201C0) is GTA SA's 2004 PS2-era CPU software
// occlusion culler: every frame it rotates and projects occluder boxes around downtown buildings
// (SetRotateX/Y/Z, CMatrix::operator*, MultiplyMatrixWithVector, CalcScreenCoors, ProcessLineSegment,
// DoesInfiniteLineTouchScreen) — burning up to 15-20% of the entire CPU frame in dense city spots
// just to save a handful of draw calls for a PS2 GPU.
// Clearing COcclusion::NumActiveOccluders (0xC73CC0) and returning immediately (the exact same path
// GTA SA itself takes inside interiors) eliminates this CPU bottleneck completely with zero visual pop-in.

#include "../registry.h"

static const DWORD kOcclusionProcess = 0x7201C0;
static const DWORD kNumActiveOccluders = 0xC73CC0;

static void __cdecl hkOcclusionProcess()
{
	*(int *)kNumActiveOccluders = 0;
}

static void Apply()
{
	static const BYTE expect[] = { 0x51, 0xA1, 0x14, 0x29, 0xB7, 0x00, 0x53, 0x55, 0x56, 0x57, 0x33, 0xFF, 0x3B, 0xC7 };
	if (memcmp((void *)kOcclusionProcess, expect, sizeof(expect))) {
		Log("OcclusionOff: COcclusion::ProcessBeforeRendering at 0x7201C0 is not original - skipped");
		return;
	}
	BYTE jmp[5] = { 0xE9 };
	*(int32_t *)(jmp + 1) = Rel32((BYTE *)kOcclusionProcess, hkOcclusionProcess);
	g_fixOcclusionOff.active = PatchBytes(kOcclusionProcess, nullptr, jmp, 5);
	Log(g_fixOcclusionOff.active ? "OcclusionOff: 2004 CPU software occlusion disabled (hardware Z-buffer used)" : "OcclusionOff: patch failed");
}

Fix g_fixOcclusionOff = {
	.section = "OcclusionOff",
	.title = "Без софт-окклюзии 2004 г.",
	.gain = "-5–19% кадра в городе",
	.summary = "Отключает тяжёлый программный расчёт окклюдеров на CPU (COcclusion), который в центре города съедает до 19% кадра.",
	.details = "В 2004 году под PlayStation 2 в GTA SA сделали программную окклюзию на процессоре: каждый кадр игра "
	           "вращает и проецирует десятки невидимых коробок-окклюдеров зданий (ProcessOneOccluder, ProcessLineSegment, "
	           "CalcScreenCoors, SetRotateX/Y/Z), чтобы не отправлять скрытые дома на слабую видеокарту PS2.\n\n"
	           "В центре города (LS/SF/LV) этот расчёт съедает до 15-20% всего времени процессора — в разы больше, чем "
	           "экономит современная видеокарта через аппаратный Z-буфер. Здесь NumActiveOccluders обнуляется сразу "
	           "(как сама игра делает в интерьерах). Картинка идентична, без мерцаний и пропаданий.",
	.gta = true,
	.atStart = Apply,
};
