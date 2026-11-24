// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [SampTextMemset] SA-MP's colored text draw (R1 samp+0x6F600, every chat line, label and dialog string
// it prints) converts the string to UTF-16 with samp+0x6F3C0(src, buf, 20000): memset of the whole
// 40 KB buffer, then MultiByteToWideChar without a terminator - the zeroing is what terminates it.
// The readers (samp+0x6F500.., color parser samp+0xAF650) stop at the first 0 char. Here only the
// converted chars and the 16 chars after them are written: same contents up to and past the
// terminator for every reader, ~40 KB less memory traffic per string.

#include "../registry.h"

static int __cdecl hkSampToWide(const char *src, wchar_t *buf, int cnt)
{
	int len = (int)strlen(src);
	int n = MultiByteToWideChar(CP_ACP, 0, src, len, nullptr, 0);
	int from = 0;
	if (n < cnt) from = MultiByteToWideChar(CP_ACP, 0, src, len, buf, n);
	if (from < 0) from = 0;
	int to = from + 16 < cnt ? from + 16 : cnt;
	for (int i = from; i < to; i++) buf[i] = 0;
	return n < cnt ? from : n;
}

static bool Apply(BYTE *)
{
	BYTE *p = SampAt(S_TO_WIDE_SITE), *wide = SampAt(S_TO_WIDE);
	// the converter's first bytes and the 20000-char count: the same in every version
	static const BYTE fn[] = { 0x53, 0x8B, 0x5C, 0x24, 0x10, 0x55, 0x8B, 0x6C, 0x24, 0x10, 0x56, 0x8B, 0x74, 0x24, 0x10, 0x8D, 0x0C, 0x1B };
	if (!p || !wide || !IsReadableRange(p - 10, 15) || !IsReadableRange(wide, sizeof(fn)) ||
	    !IsCallTo(p, (DWORD)wide) || memcmp(wide, fn, sizeof(fn)) || *(DWORD *)(p - 10) != 0x4E20) {
		Log("SampTextMemset: the UTF-16 conversion call is not original - skipped");
		return true;
	}
	if ((g_fixSampTextMemset.active = RedirectCall(p, hkSampToWide))) Log("SampTextMemset: colored text no longer clears 40 KB per string");
	return true;
}

Fix g_fixSampTextMemset = {
	.section = "SampTextMemset",
	.title = "Цветной текст SA-MP",
	.gain = "-0.5% кадра",
	.summary = "Перед каждой строкой чата и 3D-текста больше не обнуляется буфер на 40 КБ.",
	.details = "Перед выводом каждой строки SA-MP переводит её в Unicode и сначала обнуляет весь буфер на 40 КБ, даже для строки из 10 букв.\n\nОбнуляется только место сразу за строкой: SA-MP читает буфер до первого нуля. Без потерь.",
	.atSamp = Apply,
};
