// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [SampfuncsDrawFast] SAMPFUNCS puts a proxy in front of the D3D9 device. Its DrawIndexedPrimitive
// (SF+0x338A9, called for every model GTA draws) first asks its event dispatcher (SF+0x32B48, event
// id 0x52) whether any SF plugin wants to see the call: for every loaded plugin a std::multimap lookup
// and an iteration, thousands of times a frame, ~3-4% of the main thread with nothing registered.
// Here the dispatcher's plugin map is walked at most every 250 ms; while no plugin has a handler for
// DrawIndexedPrimitive the call goes straight on to the device as the dispatcher would let it.
// The other state/draw methods (SetTexture, SetRenderState, DrawPrimitive, ...) inline the same
// dispatch; their wrappers only dispatch and then call the same method of the real device (checked
// for this SF build), so their entries in SF's proxy vtable point at small thunks: no handler for
// that method -> straight to the real device, else SF's wrapper as before. DrawIndexedPrimitive
// stays on SF's wrapper: its dispatcher hook refreshes the handler table.
// SAMPFUNCS.asi md5 181e79281ae5335eacf646d284f91b1d.

#include "../registry.h"

static DWORD g_sfDispatch, g_sfDipCheckAt, g_sfTable;
static bool g_sfDipHas = true;
static BYTE g_sfHas[128]; // per D3D9 device method: some SF plugin handles it (1 until the first walk)

// MSVC std::map node: left +0, parent +4, right +8, isnil +0xD, key +0x10, value +0x14
static bool SfMapHasKey(BYTE *head, int key)
{
	if (!head) return true;
	BYTE *n = *(BYTE **)(head + 4);
	for (int depth = 0; n && !n[0xD] && depth < 64; depth++) {
		int k = *(int *)(n + 0x10);
		if (key == k) return true;
		n = *(BYTE **)(n + (key < k ? 0 : 8));
	}
	return false;
}

// marks every method id any plugin has a handler for; false if the maps look wrong
static bool SfCollectHandlers(BYTE *obj, BYTE *has)
{
	__try {
		BYTE *head = *(BYTE **)obj;
		if (!head) return false;
		BYTE *stack[256];
		int sp = 0, visited = 0;
		BYTE *root = *(BYTE **)(head + 4);
		if (root && !root[0xD]) stack[sp++] = root;
		while (sp) {
			BYTE *n = stack[--sp];
			if (++visited > 4096) return false;
			BYTE *plugin = *(BYTE **)(n + 0x14);
			if (plugin) {
				BYTE *ih = *(BYTE **)(plugin + 0x24);
				if (!ih) return false;
				BYTE *istack[256];
				int isp = 0;
				BYTE *ir = *(BYTE **)(ih + 4);
				if (ir && !ir[0xD]) istack[isp++] = ir;
				while (isp) {
					BYTE *m = istack[--isp];
					if (++visited > 4096) return false;
					DWORD key = *(DWORD *)(m + 0x10);
					if (key < 128) has[key] = 1;
					for (int side = 0; side <= 8; side += 8) {
						BYTE *c = *(BYTE **)(m + side);
						if (c && !c[0xD]) {
							if (isp == 256) return false;
							istack[isp++] = c;
						}
					}
				}
			}
			for (int side = 0; side <= 8; side += 8) {
				BYTE *c = *(BYTE **)(n + side);
				if (c && !c[0xD]) {
					if (sp == 256) return false;
					stack[sp++] = c;
				}
			}
		}
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

static bool SfAnyHandler(BYTE *obj, int id)
{
	__try {
		BYTE *head = *(BYTE **)obj;
		if (!head) return true;
		BYTE *stack[256];
		int sp = 0, visited = 0;
		BYTE *root = *(BYTE **)(head + 4);
		if (root && !root[0xD]) stack[sp++] = root;
		while (sp) {
			BYTE *n = stack[--sp];
			if (++visited > 4096) return true;
			BYTE *plugin = *(BYTE **)(n + 0x14);
			if (plugin && SfMapHasKey(*(BYTE **)(plugin + 0x24), id)) return true;
			for (int side = 0; side <= 8; side += 8) {
				BYTE *c = *(BYTE **)(n + side);
				if (c && !c[0xD]) {
					if (sp == 256) return true;
					stack[sp++] = c;
				}
			}
		}
		return false;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return true;
	}
}

static void RefreshSfHandlers(BYTE *obj)
{
	if (!obj) return;
	g_sfDipCheckAt = GetTickCount();
	g_sfDipHas = SfAnyHandler(obj, 0x52);
	BYTE has[128] = {};
	if (!SfCollectHandlers(obj, has)) memset(has, 1, sizeof(has));
	memcpy(g_sfHas, has, sizeof(has));
}

void SampfuncsDrawFastTick()
{
	if (!g_sfTable || !IsReadableRange((void *)g_sfTable, sizeof(void *))) return;
	BYTE *obj = *(BYTE **)g_sfTable;
	if (obj) RefreshSfHandlers(obj);
}

static bool __fastcall hkSfDispatchDip(BYTE *obj, void *, int id, void *a, void *b, void *c, void *d, void *e, void *f)
{
	if (GetCurrentThreadId() == g_mainThread) {
		DWORD now = GetTickCount();
		if (now - g_sfDipCheckAt >= 250) {
			RefreshSfHandlers(obj);
			g_sfDipHas = SfAnyHandler(obj, id);
		}
		if (!g_sfDipHas) return true; // what the dispatcher returns with no handlers: go on to the device
	}
	return ((bool(__thiscall *)(BYTE *, int, void *, void *, void *, void *, void *, void *))g_sfDispatch)(obj, id, a, b, c, d, e, f);
}

// state/draw methods (including DrawIndexedPrimitive = 82): proxy vtable entries -> thunks
static void PatchProxyVtable(BYTE *sf)
{
	static const BYTE methods[] = { 44, 45, 46, 47, 49, 51, 53, 55, 57, 58, 64, 65, 67, 69, 75, 81, 82, 83, 84, 87, 89, 92, 94, 100, 104, 107, 109 };
	static const DWORD wrappers[] = { 0x36A13, 0x35248, 0x356F0, 0x36D97, 0x360A7, 0x36012, 0x3565B, 0x35A68, 0x364CA, 0x34BDF, 0x35118,
	                                  0x368E3, 0x36978, 0x365F4, 0x3668F, 0x339A5, 0x338A9, 0x33A40, 0x3391E, 0x36AA8, 0x35E61, 0x36B37, 0x36C61,
	                                  0x367AD, 0x35F83, 0x3626A, 0x36394 };
	const int nm = sizeof(methods);
	memset(g_sfHas, 1, sizeof(g_sfHas));
	DWORD *vt = (DWORD *)(sf + 0xD15CC); // SF's IDirect3DDevice9 proxy vtable
	DWORD realVar = (DWORD)sf + 0x11723C; // SF's pointer to the real device
	if (!InModule((HMODULE)sf, vt) || !IsReadableRange(vt, 128 * sizeof(DWORD)) ||
	    !InModule((HMODULE)sf, (void *)realVar) || !IsReadableRange((void *)realVar, 4) ||
	    vt[82] != (DWORD)sf + 0x338A9) {
		Log("SampfuncsDrawFast: proxy vtable not found - only DrawIndexedPrimitive");
		return;
	}
	static DWORD orig[128];
	BYTE *code = (BYTE *)VirtualAlloc(nullptr, 64 * nm, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (!code) return;
	int done = 0;
	for (int k = 0; k < nm; k++) {
		int i = methods[k];
		if (vt[i] != (DWORD)sf + wrappers[k]) continue; // someone else's entry: leave it
		orig[i] = vt[i];
		BYTE *t = code + 64 * k, *p = t;
		*p++ = 0x80; *p++ = 0x3D; *(DWORD *)p = (DWORD)&g_sfHas[i]; p += 4; *p++ = 0x00; // cmp byte [has_i], 0
		BYTE *jne1 = p; *p++ = 0x75; *p++ = 0;                                     // jne sf
		*p++ = 0xA1; *(DWORD *)p = realVar; p += 4;                                // mov eax, [realVar] = real device
		*p++ = 0x85; *p++ = 0xC0;                                                  // test eax, eax
		BYTE *je1 = p; *p++ = 0x74; *p++ = 0;                                      // je sf
		*p++ = 0x89; *p++ = 0x44; *p++ = 0x24; *p++ = 0x04;                        // mov [esp+4], eax (this)
		*p++ = 0x8B; *p++ = 0x08;                                                  // mov ecx, [eax]
		*p++ = 0xFF; *p++ = 0xA1; *(DWORD *)p = i * 4; p += 4;                     // jmp [ecx + i*4]
		jne1[1] = (BYTE)(p - (jne1 + 2));
		je1[1] = (BYTE)(p - (je1 + 2));
		*p++ = 0xFF; *p++ = 0x25; *(DWORD *)p = (DWORD)&orig[i]; p += 4;           // sf: jmp [orig_i]
		DWORD thunk = (DWORD)t;
		if (PatchBytes((DWORD)&vt[i], nullptr, &thunk, 4)) done++;
	}
	FlushInstructionCache(GetCurrentProcess(), code, 64 * nm);
	Log("SampfuncsDrawFast: %d/%d state and draw methods skip SF while no plugin handles them", done, nm);
}

static bool Apply(BYTE *)
{
	BYTE *sf = (BYTE *)GetModuleHandleA("SAMPFUNCS.asi");
	if (!sf) return true; // no SAMPFUNCS (it loads before samp.dll): nothing to do
	BYTE *site = sf + 0x338E8; // push 0x52; call dispatcher (after mov ecx, [event table])
	DWORD table = (DWORD)sf + 0x115854;
	if (!InModule((HMODULE)sf, site + 8) || !IsReadableRange(site - 6, 16) ||
	    !InModule((HMODULE)sf, (void *)table) || !IsReadableRange((void *)table, 4) ||
	    site[-6] != 0x8B || site[-5] != 0x0D || *(DWORD *)(site - 4) != table || site[0] != 0x6A || site[1] != 0x52 ||
	    !IsCallTo(site + 2, (DWORD)sf + 0x32B48)) {
		Log("SampfuncsDrawFast: SAMPFUNCS.asi is another version - skipped");
		return true;
	}
	g_sfDispatch = (DWORD)sf + 0x32B48;
	g_sfTable = table;
	if (!RedirectCall(site + 2, hkSfDispatchDip)) return true;
	g_fixSampfuncsDrawFast.active = true;
	Log("SampfuncsDrawFast: DrawIndexedPrimitive skips the SF dispatcher while no plugin handles it");
	PatchProxyVtable(sf);
	return true;
}

Fix g_fixSampfuncsDrawFast = {
	.section = "SampfuncsDrawFast",
	.title = "Прослойка SAMPFUNCS",
	.gain = "-4–5% кадра",
	.summary = "Вызовы видеокарты идут мимо SAMPFUNCS, пока ни один его плагин их не слушает.",
	.details = "SAMPFUNCS встаёт между игрой и видеокартой и на каждую отрисовку модели (тысячи за кадр) спрашивает все свои плагины, не хотят ли они её перехватить. Обычно никто не хочет, а время уходит.\n\nРаз в 250 мс плагин смотрит, у каких функций есть обработчики. Для остальных вызов идёт сразу в видеокарту. Работает только с проверенной версией SAMPFUNCS.",
	.atSamp = Apply,
};
