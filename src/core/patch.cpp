// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
#include "patch.h"

#include <string.h>

#include "plugin.h"

bool IsReadableProtect(DWORD protect)
{
	if (protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
	return (protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
	                   PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

bool IsExecutableReadableProtect(DWORD protect)
{
	if (protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
	return (protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

bool IsReadableRange(const void *addr, size_t size)
{
	if (!addr || !size) return false;
	uintptr_t cur = (uintptr_t)addr;
	uintptr_t end = cur + size;
	if (end < cur) return false;
	while (cur < end) {
		MEMORY_BASIC_INFORMATION mbi{};
		if (!VirtualQuery((LPCVOID)cur, &mbi, sizeof(mbi))) return false;
		if (mbi.State != MEM_COMMIT || !IsReadableProtect(mbi.Protect)) return false;
		uintptr_t regEnd = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
		if (regEnd <= cur) return false;
		cur = regEnd;
	}
	return true;
}

bool SafeReadBytes(const void *src, void *dst, size_t size)
{
	if (!src || !dst || !size) return false;
	__try {
		memcpy(dst, src, size);
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool PatchBytes(DWORD addr, const void *expect, const void *data, size_t n)
{
	if (!addr || !data || !n) return false;
	if (expect && (!IsReadableRange((void *)addr, n) || memcmp((void *)addr, expect, n))) return false;
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
	if (!mod || !dll || !fn || !IsReadableRange(mod, sizeof(IMAGE_DOS_HEADER))) return false;
	auto dos = (IMAGE_DOS_HEADER *)mod;
	if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
	BYTE *ntPtr = (BYTE *)mod + dos->e_lfanew;
	if (!IsReadableRange(ntPtr, sizeof(IMAGE_NT_HEADERS))) return false;
	auto nt = (IMAGE_NT_HEADERS *)ntPtr;
	if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
	auto &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (!dir.VirtualAddress) return false;
	for (auto imp = (IMAGE_IMPORT_DESCRIPTOR *)((BYTE *)mod + dir.VirtualAddress);
	     IsReadableRange(imp, sizeof(IMAGE_IMPORT_DESCRIPTOR)) && imp->Name; imp++) {
		const char *modName = (const char *)((BYTE *)mod + imp->Name);
		if (!IsReadableRange(modName, 1) || _stricmp(modName, dll)) continue;
		auto names = (IMAGE_THUNK_DATA *)((BYTE *)mod + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
		auto addrs = (IMAGE_THUNK_DATA *)((BYTE *)mod + imp->FirstThunk);
		for (; IsReadableRange(names, sizeof(IMAGE_THUNK_DATA)) && IsReadableRange(addrs, sizeof(IMAGE_THUNK_DATA)) && names->u1.AddressOfData; names++, addrs++) {
			if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
			auto ibn = (IMAGE_IMPORT_BY_NAME *)((BYTE *)mod + names->u1.AddressOfData);
			if (!IsReadableRange(ibn, sizeof(IMAGE_IMPORT_BY_NAME)) || strcmp((char *)ibn->Name, fn)) continue;
			if (orig) *orig = (void *)addrs->u1.Function;
			return PatchBytes((DWORD)&addrs->u1.Function, nullptr, &hook, 4);
		}
	}
	return false;
}

BYTE *ModuleEnd(HMODULE m)
{
	if (!m || !IsReadableRange(m, sizeof(IMAGE_DOS_HEADER))) return nullptr;
	auto dos = (IMAGE_DOS_HEADER *)m;
	if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
	BYTE *ntPtr = (BYTE *)m + dos->e_lfanew;
	if (!IsReadableRange(ntPtr, sizeof(IMAGE_NT_HEADERS))) return nullptr;
	auto nt = (IMAGE_NT_HEADERS *)ntPtr;
	if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
	return (BYTE *)m + nt->OptionalHeader.SizeOfImage;
}

bool InModule(HMODULE m, const void *p)
{
	BYTE *end = ModuleEnd(m);
	return end && (const BYTE *)p >= (const BYTE *)m && (const BYTE *)p < end;
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
