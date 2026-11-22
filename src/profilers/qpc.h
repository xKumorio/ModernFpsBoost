// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
#pragma once

#include <windows.h>

static inline LONGLONG Qpc()
{
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return t.QuadPart;
}

static inline double QpcFreq()
{
	static const double f = [] {
		LARGE_INTEGER q;
		QueryPerformanceFrequency(&q);
		return double(q.QuadPart);
	}();
	return f;
}
