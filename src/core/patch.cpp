// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
#include "patch.h"

#include <string.h>

#include "plugin.h"

bool PatchBytes(DWORD addr, const void *expect, const void *data, size_t n)
{
	if (expect && memcmp((void *)addr, expect, n)) return false;
	DWORD old;
	if (!VirtualProtect((void *)addr, n, PAGE_EXECUTE_READWRITE, &old)) return false;
	memcpy((void *)addr, data, n);
	VirtualProtect((void *)addr, n, old, &old);
	FlushInstructionCache(GetCurrentProcess(), (void *)addr, n);
	return true;
}

bool PatchMainThread(BYTE *const *addrs, const void *const *data, const size_t *sizes, int count)
{
	// on the main thread itself nothing else runs its code: suspending it would freeze the game
	if (GetCurrentThreadId() == g_mainThread) {
		bool ok = true;
		for (int k = 0; k < count; k++) ok &= PatchBytes((DWORD)addrs[k], nullptr, data[k], sizes[k]);
		return ok;
	}
	HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, g_mainThread);
	if (!th) return false;
	for (int attempt = 0; attempt < 200; attempt++) {
		SuspendThread(th);
		CONTEXT c = {};
		c.ContextFlags = CONTEXT_CONTROL;
		bool inside = !GetThreadContext(th, &c);
		for (int k = 0; k < count && !inside; k++)
			if (c.Eip > (DWORD)addrs[k] && c.Eip < (DWORD)addrs[k] + sizes[k]) inside = true;
		if (!inside) {
			bool ok = true;
			for (int k = 0; k < count; k++) ok &= PatchBytes((DWORD)addrs[k], nullptr, data[k], sizes[k]);
			ResumeThread(th);
			CloseHandle(th);
			return ok;
		}
		ResumeThread(th);
		Sleep(1);
	}
	CloseHandle(th);
	return false;
}

bool RedirectCall(BYTE *site, const void *dest)
{
	int32_t rel = Rel32(site, dest);
	BYTE *addrs[] = { site + 1 };
	const void *data[] = { &rel };
	size_t sizes[] = { 4 };
	return PatchMainThread(addrs, data, sizes, 1);
}

bool PatchIat(HMODULE mod, const char *dll, const char *fn, void *hook, void **orig)
{
	auto dos = (IMAGE_DOS_HEADER *)mod;
	auto nt = (IMAGE_NT_HEADERS *)((BYTE *)mod + dos->e_lfanew);
	auto &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	for (auto imp = (IMAGE_IMPORT_DESCRIPTOR *)((BYTE *)mod + dir.VirtualAddress); imp->Name; imp++) {
		if (_stricmp((char *)mod + imp->Name, dll)) continue;
		auto names = (IMAGE_THUNK_DATA *)((BYTE *)mod + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
		auto addrs = (IMAGE_THUNK_DATA *)((BYTE *)mod + imp->FirstThunk);
		for (; names->u1.AddressOfData; names++, addrs++) {
			if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
			auto ibn = (IMAGE_IMPORT_BY_NAME *)((BYTE *)mod + names->u1.AddressOfData);
			if (strcmp((char *)ibn->Name, fn)) continue;
			*orig = (void *)addrs->u1.Function;
			return PatchBytes((DWORD)&addrs->u1.Function, nullptr, &hook, 4);
		}
	}
	return false;
}

BYTE *ModuleEnd(HMODULE m)
{
	auto nt = (IMAGE_NT_HEADERS *)((BYTE *)m + ((IMAGE_DOS_HEADER *)m)->e_lfanew);
	return (BYTE *)m + nt->OptionalHeader.SizeOfImage;
}

bool InModule(HMODULE m, const void *p)
{
	return (const BYTE *)p >= (const BYTE *)m && (const BYTE *)p < ModuleEnd(m);
}

static struct { BYTE *site; DWORD *next; } g_chains[16];
static int g_chainN;

void AddCallChain(BYTE *site, DWORD *next)
{
	if (g_chainN < 16) g_chains[g_chainN++] = { site, next };
}

DWORD *CallChainNext(BYTE *site)
{
	for (int k = 0; k < g_chainN; k++)
		if (g_chains[k].site == site) return g_chains[k].next;
	return nullptr;
}
