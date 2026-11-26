// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.

#include <d3d9.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <utility>
#include <vector>

#include "imgui.h"
#include "backends/imgui_impl_win32.h"

#include "../../res/resource.h"
#include "../core/game.h"
#include "../core/ini.h"
#include "../core/patch.h"
#include "../core/plugin.h"
#include "../fixes/registry.h"
#include "../profilers/profilers.h"
#include "menu.h"
#include "render_dx9.h"
#include "theme.h"
#include "ui.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

UiConfig g_ui;
FrameStats g_frames;
UiFonts g_fonts;
IDirect3DTexture9 *g_background;
int g_backgroundW, g_backgroundH;
bool g_captureKey;

static const DWORD kDevicePtr = 0xC97C28; // RwD3D9 device (what GTA calls)
static const DWORD kWindowPtr = 0xC97C1C; // RsGlobal.ps->window
static const int   kVtReset   = 16;
static const int   kVtPresent = 17;

enum { CURSOR_NONE = 0, CURSOR_LOCKCAMANDCONTROL = 2 };

typedef HRESULT(__stdcall *Reset_t)(IDirect3DDevice9 *, D3DPRESENT_PARAMETERS *);
typedef HRESULT(__stdcall *Present_t)(IDirect3DDevice9 *, const RECT *, const RECT *, HWND, const RGNDATA *);
static Reset_t oReset;
static Present_t oPresentOuter, oPresent;
static IDirect3DDevice9 *g_outerDevice;
static void **g_outerVt;
static bool g_realHooked, g_singleHook;
static IDirect3DDevice9 *g_realDevice;
static thread_local bool g_insideOuter, g_frameDrawn, g_realSeen;
static bool g_useOuter;
static volatile DWORD g_lastFrameTick;

static HWND g_window;
static WNDPROC g_prevProc;
static bool g_isUnicodeWnd;
static bool g_open;
static float g_openAnim;
static bool g_imgui, g_imguiFailed, g_overlayFaulted, g_cjkLoaded;
static thread_local const char *g_overlayStage = "idle";
static DWORD g_overlayExceptionCode, g_overlayExceptionEip, g_overlayExceptionAccess;
static void *g_overlayExceptionAddress, *g_overlayExceptionTarget;

static int CaptureOverlayException(EXCEPTION_POINTERS *info)
{
	if (info && info->ExceptionRecord) {
		g_overlayExceptionCode = info->ExceptionRecord->ExceptionCode;
		g_overlayExceptionAddress = info->ExceptionRecord->ExceptionAddress;
		if (info->ExceptionRecord->NumberParameters >= 2) {
			g_overlayExceptionAccess = (DWORD)info->ExceptionRecord->ExceptionInformation[0];
			g_overlayExceptionTarget = (void *)info->ExceptionRecord->ExceptionInformation[1];
		}
	}
	if (info && info->ContextRecord) g_overlayExceptionEip = info->ContextRecord->Eip;
	return EXCEPTION_EXECUTE_HANDLER;
}
static IDirect3DDevice9 *g_imguiDevice;
static int g_cursorMode = CURSOR_NONE;
static DWORD g_cursorAt;
static bool g_sampCursor;
static bool g_spLocked;
static bool g_spSaved;
static BYTE g_spOrig541DF5[5], g_spOrig53F417[5], g_spOrig53F41F[4], g_spOrig6194A0[1];

static void GreetInChat();
static void SetSingleplayerCursor(bool lock);

// ---------------------------------------------------------------- input

// Input reaches the menu before anything else in the game: a WH_GETMESSAGE hook sees every posted
// keyboard/mouse message as it leaves the queue, before SA-MP, SAMPFUNCS, MoonLoader or GTA get it
// (some of them subclass the window after us, so a WndProc alone let keys through: T opened the chat).
// While the menu is open such messages go to ImGui only and become WM_NULL for the game.
// Typed characters come from ToUnicodeEx on WM_KEYDOWN, not WM_CHAR: GTA's ANSI message loop turns
// Cyrillic into '?' when the system code page is not 1251.
static HHOOK g_msgHook;

static bool IsInput(UINT m)
{
	return (m >= WM_KEYFIRST && m <= WM_KEYLAST) || (m >= WM_MOUSEFIRST && m <= WM_MOUSELAST);
}

static void TypeChars(WPARAM vk, LPARAM lp)
{
	BYTE keys[256];
	if (!GetKeyboardState(keys)) return;
	if ((keys[VK_CONTROL] & 0x80) && !(keys[VK_MENU] & 0x80)) return; // Ctrl+C/V/A are shortcuts
	wchar_t buf[8];
	int n = ToUnicodeEx((UINT)vk, (UINT)(lp >> 16) & 0xFF, keys, buf, 8, 4 /* keep dead-key state */, GetKeyboardLayout(0));
	for (int k = 0; k < n; k++)
		if (buf[k] >= 32 && buf[k] != 127) ImGui::GetIO().AddInputCharacterUTF16(buf[k]);
}

static bool IsOverlayAlive()
{
	if (g_overlayFaulted || g_imguiFailed) return false;
	DWORD last = g_lastFrameTick;
	return last != 0 && (GetTickCount() - last) < 1500;
}

// true: the message is the menu's
static bool MenuInput(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
	if (!IsOverlayAlive()) {
		if (g_open) {
			g_open = false;
			if (g_spLocked) SetSingleplayerCursor(false);
		}
		if ((m == WM_KEYDOWN || m == WM_SYSKEYDOWN) && (int)wp == g_ui.key && !(lp & (1 << 30)))
			Log("Menu: key 0x%02X ignored (Present hook not receiving frames yet)", g_ui.key);
		return false;
	}
	if (m == WM_KEYDOWN || m == WM_SYSKEYDOWN) {
		if (g_open && g_captureKey) {
			if (wp != VK_ESCAPE) {
				g_ui.key = (int)wp;
				SaveUiConfig();
			}
			g_captureKey = false;
			return true;
		}
		if ((int)wp == g_ui.key) {
			if (!(lp & (1 << 30))) {
				g_open = !g_open;
				Log("Menu: key 0x%02X, %s", g_ui.key, g_open ? "open" : "closed");
				if (g_imgui) ImGui::GetIO().ClearInputKeys(); // keys held across the switch are not stuck in the menu
			}
			return true;
		}
		if (g_open && wp == VK_ESCAPE) {
			g_open = false;
			if (g_imgui) ImGui::GetIO().ClearInputKeys();
			return true;
		}
	}
	if ((m == WM_KEYUP || m == WM_SYSKEYUP) && (int)wp == g_ui.key) return true;
	// releases always reach the game: a key held while the menu opened would stay pressed for it
	if (m == WM_KEYUP || m == WM_SYSKEYUP) {
		if (g_open && g_imgui) ImGui_ImplWin32_WndProcHandler(w, m, wp, lp);
		return false;
	}
	if (!g_open || !IsInput(m)) return false;
	if (g_imgui) {
		if (m != WM_CHAR && m != WM_SYSCHAR && m != WM_DEADCHAR && m != WM_SYSDEADCHAR) ImGui_ImplWin32_WndProcHandler(w, m, wp, lp);
		if (m == WM_KEYDOWN && ImGui::GetIO().WantTextInput) TypeChars(wp, lp);
	}
	return true;
}

static LRESULT CALLBACK MsgHook(int code, WPARAM wp, LPARAM lp)
{
	MSG *msg = (MSG *)lp;
	if (code == HC_ACTION && wp == PM_REMOVE && msg->hwnd == g_window && MenuInput(msg->hwnd, msg->message, msg->wParam, msg->lParam))
		msg->message = WM_NULL;
	return CallNextHookEx(g_msgHook, code, wp, lp);
}

static LRESULT CALLBACK WndProc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
	if (g_open && !g_sampCursor && m == WM_SETCURSOR && LOWORD(lp) == HTCLIENT) {
		SetCursor(nullptr); // ImGui draws the cursor in singleplayer; avoid a duplicate OS cursor
		return TRUE;
	}
	if (IsInput(m)) {
		if (MenuInput(w, m, wp, lp)) return 0; // sent, not posted: the hook did not see it
	} else if (g_open && g_imgui) {
		ImGui_ImplWin32_WndProcHandler(w, m, wp, lp); // focus, keyboard layout, ...
	}
	return g_isUnicodeWnd ? CallWindowProcW(g_prevProc, w, m, wp, lp) : CallWindowProcA(g_prevProc, w, m, wp, lp);
}

static bool SetSampCursor(int mode)
{
	void **gameVar = (void **)SampAt(S_GAME);
	BYTE *setMode = SampAt(S_SET_CURSOR_MODE);
	if (!gameVar || !IsReadableRange(gameVar, sizeof(void *)) || !*gameVar || !setMode || !IsReadableRange(setMode, 4))
		return false;
	((void(__thiscall *)(void *, int, BOOL))setMode)(*gameVar, mode, mode == CURSOR_NONE);
	return true;
}

static void SetDiMouseAcquired(bool acquire)
{
	if (!IsGta10US() || !IsReadableRange((void *)0xC17054, sizeof(void *))) return;
	BYTE *psGlobal = *(BYTE **)0xC17054;
	if (!psGlobal || !IsReadableRange(psGlobal + 0x1C, sizeof(void *))) return;
	void *mouse = *(void **)(psGlobal + 0x1C);
	if (!mouse || !IsReadableRange(mouse, sizeof(void *))) return;
	void **vt = *(void ***)mouse;
	const int slot = acquire ? (0x1C / 4) : (0x20 / 4); // IDirectInputDevice8::Acquire / Unacquire
	if (!vt || !IsReadableRange(vt + slot, sizeof(void *)) || !vt[slot]) return;
	((HRESULT(__stdcall *)(void *))vt[slot])(mouse);
}

// Singleplayer / non-SA-MP cursor & input lock (mirrors SA-MP CGame::SetCursorMode on GTA SA 1.0 US,
// releases exclusive DirectInput mouse so Win32 cursor moves freely, and lets ImGui render the cursor
// reliably in both windowed and exclusive fullscreen / DXVK modes).
static void SetSingleplayerCursor(bool lock)
{
	if (lock == g_spLocked) return;
	if (!IsGta10US()) {
		g_spLocked = lock;
		return;
	}
	if (lock) {
		if (!g_spSaved) {
			if (!SafeReadBytes((void *)0x541DF5, g_spOrig541DF5, 5) ||
			    !SafeReadBytes((void *)0x53F417, g_spOrig53F417, 5) ||
			    !SafeReadBytes((void *)0x53F41F, g_spOrig53F41F, 4) ||
			    !SafeReadBytes((void *)0x6194A0, g_spOrig6194A0, 1))
				return;
			g_spSaved = true;
		}
		static const BYTE kNop5[5]  = { 0x90, 0x90, 0x90, 0x90, 0x90 };
		static const BYTE kSkip4[4] = { 0x33, 0xC0, 0x0F, 0x84 }; // xor eax, eax; jz ...
		static const BYTE kRet1[1]  = { 0xC3 };
		PatchBytes(0x541DF5, nullptr, kNop5, 5);
		PatchBytes(0x53F417, nullptr, kNop5, 5);
		PatchBytes(0x53F41F, nullptr, kSkip4, 4);
		PatchBytes(0x6194A0, nullptr, kRet1, 1);
		*(DWORD *)0xB73424 = 0;
		*(DWORD *)0xB73428 = 0;
		((void(__cdecl *)())0x541BD0)(); // CPad::ClearMouseHistory
		((void(__cdecl *)())0x541DD0)(); // CPad::UpdatePads helper
		SetDiMouseAcquired(false);
		ClipCursor(nullptr);
		g_spLocked = true;
	} else {
		if (g_spSaved) {
			PatchBytes(0x541DF5, nullptr, g_spOrig541DF5, 5);
			PatchBytes(0x53F417, nullptr, g_spOrig53F417, 5);
			PatchBytes(0x53F41F, nullptr, g_spOrig53F41F, 4);
			PatchBytes(0x6194A0, nullptr, g_spOrig6194A0, 1);
		}
		*(DWORD *)0xB73424 = 0;
		*(DWORD *)0xB73428 = 0;
		((void(__cdecl *)())0x541BD0)(); // CPad::ClearMouseHistory
		SetDiMouseAcquired(true);
		g_spLocked = false;
	}
}

// SA-MP resets the cursor itself (chat, dialogs): while the menu is open it is set again twice a second.
// In singleplayer (no SA-MP), fall back to SetSingleplayerCursor + ImGui software cursor.
static void UpdateCursor()
{
	int want = g_open ? CURSOR_LOCKCAMANDCONTROL : CURSOR_NONE;
	DWORD now = GetTickCount();
	if (want != g_cursorMode || (want != CURSOR_NONE && now - g_cursorAt > 500)) {
		if (SetSampCursor(want)) {
			if (g_spLocked) SetSingleplayerCursor(false);
			g_sampCursor = true;
		} else {
			g_sampCursor = false;
			SetSingleplayerCursor(want != CURSOR_NONE);
			SetDiMouseAcquired(want == CURSOR_NONE);
			if (want != CURSOR_NONE) {
				ClipCursor(nullptr);
			}
		}
		g_cursorMode = want;
		g_cursorAt = now;
	}
}

// ---------------------------------------------------------------- hardware telemetry (CPU & 3D GPU)

static volatile LONG g_hwWanted; // 1 while menu or corner HUD is visible

static DWORD WINAPI HwMonitorThread(LPVOID)
{
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

	// Optional PDH loader for exact Windows 10/11 3D GPU utilization (matches Task Manager; works on DXVK/Vulkan & D3D9)
	typedef PDH_STATUS(WINAPI *PdhOpenQueryW_t)(LPCWSTR, DWORD_PTR, PDH_HQUERY *);
	typedef PDH_STATUS(WINAPI *PdhAddEnglishCounterW_t)(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER *);
	typedef PDH_STATUS(WINAPI *PdhCollectQueryData_t)(PDH_HQUERY);
	typedef PDH_STATUS(WINAPI *PdhGetFormattedCounterArrayW_t)(PDH_HCOUNTER, DWORD, LPDWORD, LPDWORD, PPDH_FMT_COUNTERVALUE_ITEM_W);
	typedef PDH_STATUS(WINAPI *PdhCloseQuery_t)(PDH_HQUERY);

	HMODULE pdh = LoadLibraryA("pdh.dll");
	auto pOpen    = pdh ? (PdhOpenQueryW_t)GetProcAddress(pdh, "PdhOpenQueryW") : nullptr;
	auto pAdd     = pdh ? (PdhAddEnglishCounterW_t)GetProcAddress(pdh, "PdhAddEnglishCounterW") : nullptr;
	auto pCollect = pdh ? (PdhCollectQueryData_t)GetProcAddress(pdh, "PdhCollectQueryData") : nullptr;
	auto pArray   = pdh ? (PdhGetFormattedCounterArrayW_t)GetProcAddress(pdh, "PdhGetFormattedCounterArrayW") : nullptr;
	auto pClose   = pdh ? (PdhCloseQuery_t)GetProcAddress(pdh, "PdhCloseQuery") : nullptr;

	PDH_HQUERY hQuery = nullptr;
	PDH_HCOUNTER hGpu = nullptr;
	bool pdhReady = false;
	if (pOpen && pAdd && pCollect && pArray && pClose &&
	    pOpen(nullptr, 0, &hQuery) == ERROR_SUCCESS &&
	    pAdd(hQuery, L"\\GPU Engine(*engtype_3D)\\Utilization Percentage", 0, &hGpu) == ERROR_SUCCESS) {
		pCollect(hQuery);
		pdhReady = true;
	}

	auto to64 = [](const FILETIME &ft) -> ULONGLONG {
		return ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
	};
	ULONGLONG prevIdle = 0, prevKern = 0, prevUser = 0;
	FILETIME fi, fk, fu;
	if (GetSystemTimes(&fi, &fk, &fu)) {
		prevIdle = to64(fi);
		prevKern = to64(fk);
		prevUser = to64(fu);
	}

	std::vector<BYTE> buf;
	for (;;) {
		int period = g_ui.overlayRefreshMs;
		if (period < 200) period = 200;
		if (period > 1000) period = 1000;
		Sleep(period);

		if (!InterlockedCompareExchange(&g_hwWanted, 0, 0)) continue;

		if (GetSystemTimes(&fi, &fk, &fu)) {
			ULONGLONG id = to64(fi), kn = to64(fk), us = to64(fu);
			ULONGLONG dIdle = id - prevIdle;
			ULONGLONG dTot  = (kn - prevKern) + (us - prevUser);
			prevIdle = id; prevKern = kn; prevUser = us;
			if (dTot > 0) {
				double pct = (double)(dTot > dIdle ? dTot - dIdle : 0) * 100.0 / (double)dTot;
				if (pct < 0.0) pct = 0.0;
				if (pct > 100.0) pct = 100.0;
				g_frames.sysCpu = (float)pct;
			}
		}

		if (pdhReady && pCollect(hQuery) == ERROR_SUCCESS) {
			DWORD bufSz = 0, cnt = 0;
			PDH_STATUS st = pArray(hGpu, PDH_FMT_DOUBLE, &bufSz, &cnt, nullptr);
			if (st == (PDH_STATUS)PDH_MORE_DATA && bufSz > 0) {
				if (buf.size() < bufSz) buf.resize(bufSz);
				if (pArray(hGpu, PDH_FMT_DOUBLE, &bufSz, &cnt, (PPDH_FMT_COUNTERVALUE_ITEM_W)buf.data()) == ERROR_SUCCESS) {
					double total3d = 0.0;
					auto *items = (const PDH_FMT_COUNTERVALUE_ITEM_W *)buf.data();
					for (DWORD i = 0; i < cnt; i++) {
						if (items[i].FmtValue.CStatus == ERROR_SUCCESS && items[i].FmtValue.doubleValue > 0.0)
							total3d += items[i].FmtValue.doubleValue;
					}
					if (total3d > 100.0) total3d = 100.0;
					g_frames.gpu = (float)total3d;
				}
			}
		}
	}
	if (hQuery && pClose) pClose(hQuery);
	return 0;
}

// ---------------------------------------------------------------- frame timing

static void TrackFrame()
{
	static LONGLONG last, freq, cpuWall, dispWall;
	static ULONGLONG cpuLast;
	static double dispMsSum;
	static int dispFrames;
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	if (!freq) {
		LARGE_INTEGER f;
		QueryPerformanceFrequency(&f);
		freq = f.QuadPart;
		dispWall = t.QuadPart;
	}
	if (last) {
		float ms = float(double(t.QuadPart - last) * 1000.0 / freq);
		g_frames.frameMs[g_frames.head] = ms;
		g_frames.head = (g_frames.head + 1) % FrameStats::kHistory;
		if (g_frames.count < FrameStats::kHistory) g_frames.count++;
		dispMsSum += ms;
		dispFrames++;
	}
	last = t.QuadPart;

	// rolling fps / ms over last ~half second (used for internal dt)
	float sum = 0;
	int n = 0;
	for (int i = 1; i <= g_frames.count && sum < 500.0f; i++, n++)
		sum += g_frames.frameMs[(g_frames.head - i + FrameStats::kHistory) % FrameStats::kHistory];
	if (n) {
		g_frames.ms = sum / n;
		g_frames.fps = 1000.0f / g_frames.ms;
	}
	static int lowTick;
	if (++lowTick >= 30 && g_frames.count) {
		lowTick = 0;
		float worst[FrameStats::kHistory];
		memcpy(worst, g_frames.frameMs, sizeof(worst));
		int k = g_frames.count / 100 + 1;
		for (int j = 0; j < k; j++)
			for (int i = j + 1; i < g_frames.count; i++)
				if (worst[i] > worst[j]) std::swap(worst[i], worst[j]);
		float avg = 0;
		for (int j = 0; j < k; j++) avg += worst[j];
		g_frames.low1 = 1000.0f * k / avg;
	}

	// Latched readable readout updated at a calm user-configured cadence (default 500 ms)
	int refreshMs = g_ui.overlayRefreshMs;
	if (refreshMs < 200) refreshMs = 200;
	if (refreshMs > 1000) refreshMs = 1000;
	if (dispFrames > 0 && (t.QuadPart - dispWall) * 1000 >= freq * refreshMs) {
		double avgMs = dispMsSum / dispFrames;
		g_frames.dispMs = (float)avgMs;
		g_frames.dispFps = avgMs > 0.001 ? (float)(1000.0 / avgMs) : 0.0f;
		g_frames.dispLow1 = g_frames.low1;
		dispMsSum = 0.0;
		dispFrames = 0;
		dispWall = t.QuadPart;
	} else if (g_frames.dispFps <= 0.0f && g_frames.fps > 0.0f) {
		g_frames.dispFps = g_frames.fps;
		g_frames.dispMs = g_frames.ms;
		g_frames.dispLow1 = g_frames.low1;
	}

	if (t.QuadPart - cpuWall >= freq) {
		FILETIME c, e, kt, ut;
		if (GetThreadTimes(GetCurrentThread(), &c, &e, &kt, &ut)) {
			ULONGLONG cpu = ((ULONGLONG)kt.dwHighDateTime << 32 | kt.dwLowDateTime) + ((ULONGLONG)ut.dwHighDateTime << 32 | ut.dwLowDateTime);
			if (cpuWall) g_frames.cpu = float(double(cpu - cpuLast) / 1e7 / (double(t.QuadPart - cpuWall) / freq) * 100.0);
			cpuLast = cpu;
		}
		cpuWall = t.QuadPart;
	}
}

// ---------------------------------------------------------------- ImGui

static bool InitImGui(IDirect3DDevice9 *device)
{
	if (g_imgui) {
		if (device == g_imguiDevice) return true;
		if (g_background) { g_background->Release(); g_background = nullptr; }
		RenderResetDevice();
		if (!RenderInit(device)) {
			Log("Menu: ImGui re-init failed on new device %p", device);
			return false;
		}
		g_background = LoadResourceTexture(IDR_BACKGROUND, g_backgroundW, g_backgroundH);
		g_imguiDevice = device;
		Log("Menu: ImGui re-bound to new device %p", device);
		return true;
	}
	if (g_imguiFailed) return false;
	g_imguiFailed = true;
	g_overlayStage = "imgui.create_context";
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.ConfigDebugHighlightIdConflicts = false; // a player build: no debug popups over the menu
	io.LogFilename = nullptr;
	io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
	io.MouseDrawCursor = false;
	io.ConfigErrorRecoveryEnableAssert = false; // a UI mistake must not take the game down
	g_overlayStage = "imgui.load_fonts";
	LoadFonts();
	g_cjkLoaded = (g_ui.language == 1);
	g_overlayStage = "imgui.apply_theme";
	ApplyTheme();
	g_overlayStage = "imgui.win32_init";
	if (!ImGui_ImplWin32_Init(g_window)) {
		Log("Menu: Win32 backend init failed (window=%p error=%lu)", g_window, GetLastError());
		ImGui::DestroyContext();
		ZeroMemory(&g_fonts, sizeof(g_fonts));
		return false;
	}
	g_overlayStage = "imgui.d3d9_init";
	if (!RenderInit(device)) {
		Log("Menu: D3D9 renderer init failed (device=%p)", device);
		RenderShutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
		ZeroMemory(&g_fonts, sizeof(g_fonts));
		return false;
	}
	g_overlayStage = "imgui.background";
	g_background = LoadResourceTexture(IDR_BACKGROUND, g_backgroundW, g_backgroundH);
	if (!g_background) Log("Menu: background image not loaded");
	g_imgui = true;
	g_imguiDevice = device;
	g_imguiFailed = false;
	g_overlayStage = "frame.begin_scene";
	Log("Menu: ready (%dx%d background)", g_backgroundW, g_backgroundH);
	return true;
}

static void OnFrame(IDirect3DDevice9 *device)
{
	TrackFrame();
	FixesTickStats();

	if (!g_prevProc) {
		D3DDEVICE_CREATION_PARAMETERS params = {};
		if (SUCCEEDED(device->GetCreationParameters(&params))) g_window = params.hFocusWindow;
		if (!IsWindow(g_window) && IsGta10US() && IsReadableRange((void *)kWindowPtr, sizeof(HWND)))
			g_window = *(HWND *)kWindowPtr;
		if (IsWindow(g_window)) {
			g_isUnicodeWnd = (IsWindowUnicode(g_window) != FALSE);
			g_prevProc = g_isUnicodeWnd
				? (WNDPROC)SetWindowLongPtrW(g_window, GWLP_WNDPROC, (LONG_PTR)WndProc)
				: (WNDPROC)SetWindowLongPtrA(g_window, GWLP_WNDPROC, (LONG_PTR)WndProc);
		}
		if (!g_prevProc) {
			static bool logged;
			if (!logged) Log("Menu: input attach failed (window %p, error %lu)", g_window, GetLastError());
			logged = true;
			return;
		}
		Log("Menu: input attached to window %p", g_window);
		g_msgHook = g_isUnicodeWnd
			? SetWindowsHookExW(WH_GETMESSAGE, MsgHook, nullptr, GetWindowThreadProcessId(g_window, nullptr))
			: SetWindowsHookExA(WH_GETMESSAGE, MsgHook, nullptr, GetWindowThreadProcessId(g_window, nullptr));
		if (!g_msgHook) Log("Menu: message hook failed (%lu), keys reach the game first", GetLastError());
	}
	g_overlayStage = "frame.cursor";
	UpdateCursor();
	g_overlayStage = "frame.chat";
	GreetInChat();

	float dt = g_frames.ms > 0 ? g_frames.ms / 1000.0f : 0.016f;
	// linear in time, eased by the menu: opens in ~0.20 s, closes in ~0.15 s
	g_openAnim = g_open ? min(1.0f, g_openAnim + dt / 0.20f) : max(0.0f, g_openAnim - dt / 0.15f);
	if (!g_open && g_openAnim < 0.01f) g_openAnim = 0;
	const bool visible = (g_openAnim > 0 || g_ui.fpsOverlay);
	InterlockedExchange(&g_hwWanted, visible ? 1 : 0);
	if (!visible) return;
	// Wait until the game has rendered a few frames and the device/window are ready;
	// drawing in Present on frame #1 with fps_overlay=1 can leave a black screen during init.
	if (g_frames.count < 10 || !IsWindow(g_window) || IsIconic(g_window)) return;
	if (device->TestCooperativeLevel() != D3D_OK) return;
	if (!InitImGui(device)) return;
	if (g_ui.language == 1 && !g_cjkLoaded) {
		g_overlayStage = "imgui.reload_cjk";
		RenderShutdown();
		ImGui::GetIO().Fonts->Clear();
		LoadFonts();
		if (!RenderInit(device)) return;
		g_cjkLoaded = true;
	}
	g_overlayStage = "frame.begin_scene";
	HRESULT bhr = device->BeginScene();
	if (FAILED(bhr)) {
		static HRESULT lastErr;
		if (bhr != lastErr) {
			lastErr = bhr;
			Log("Menu: BeginScene failed (0x%08lX)", bhr);
		}
		return;
	}

	ImGuiIO &io = ImGui::GetIO();
	io.MouseDrawCursor = (g_open && !g_sampCursor);
	if (g_open) {
		io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
		ImGui_ImplWin32_NewFrame();
	} else {
		// Fast path when menu is closed (only corner telemetry strip is visible):
		// skip ImGui_ImplWin32_NewFrame() to avoid 1600/s GetCursorPos & GetForegroundWindow syscalls!
		io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
		static int s_w, s_h;
		static DWORD s_rectAt;
		DWORD now = GetTickCount();
		if (!s_w || now - s_rectAt >= 1000) {
			RECT rc = {};
			if (g_window && GetClientRect(g_window, &rc)) {
				s_w = rc.right - rc.left;
				s_h = rc.bottom - rc.top;
			}
			s_rectAt = now;
		}
		if (s_w <= 0 || s_h <= 0) {
			device->EndScene();
			return;
		}
		io.DisplaySize = ImVec2((float)s_w, (float)s_h);
		io.DeltaTime = dt > 0.0001f ? dt : (1.0f / 60.0f);
	}
	g_overlayStage = "frame.new_frame";
	ImGui::NewFrame();
	if (g_ui.fpsOverlay && g_openAnim < 0.5f) DrawFpsOverlay();
	if (g_openAnim > 0) {
		g_menuAnim = g_openAnim;
		bool open = g_open;
		g_overlayStage = "frame.draw_menu";
		DrawMenu(open);
		if (!open) g_open = false;
	}
	g_overlayStage = "frame.end_frame";
	ImGui::EndFrame();
	g_overlayStage = "frame.render";
	ImGui::Render();
	g_overlayStage = "frame.draw_data";
	RenderDrawData(ImGui::GetDrawData());
	g_overlayStage = "frame.end_scene";
	device->EndScene();
	g_overlayStage = "frame.idle";
}

static void OnFrameSafe(IDirect3DDevice9 *device)
{
	g_lastFrameTick = GetTickCount();
	if (g_overlayFaulted || !device) return;
	__try {
		OnFrame(device);
	} __except (CaptureOverlayException(GetExceptionInformation())) {
		g_overlayFaulted = true;
		g_open = false;
		if (g_spLocked) SetSingleplayerCursor(false);
		Log("Menu: exception 0x%08lX at %p (EIP=%p access=%lu target=%p stage=%s) - menu disabled, FPS fixes keep running",
		    g_overlayExceptionCode, g_overlayExceptionAddress, (void *)g_overlayExceptionEip,
		    g_overlayExceptionAccess, g_overlayExceptionTarget, g_overlayStage);
	}
}

// ---------------------------------------------------------------- chat

// One line in the SA-MP chat a few seconds after the chat exists. The chat takes cp1251 text.
static void GreetInChat()
{
	// 3 s after SA-MP's "Connected to <server>", once per connection
	static bool done;
	static DWORD connectedAt;
	if (!g_samp) return;
	BYTE **net = (BYTE **)SampAt(S_NETGAME);
	bool connected = net && IsReadableRange(net, sizeof(BYTE *)) && *net &&
	                 IsReadableRange(*net + g_sl.netState, sizeof(int)) &&
	                 *(int *)(*net + g_sl.netState) == g_sl.netConnected;
	if (!connected) {
		done = false;
		connectedAt = 0;
		return;
	}
	if (done) return;
	DWORD now = GetTickCount();
	if (!connectedAt) connectedAt = now;
	if (now - connectedAt < 3000) return;
	void **chatVar = (void **)SampAt(S_CHAT);
	BYTE *addMessage = SampAt(S_CHAT_ADD_MESSAGE);
	void *chat = (chatVar && IsReadableRange(chatVar, sizeof(void *))) ? *chatVar : nullptr;
	if (!chat || !addMessage || !IsReadableRange(addMessage, 4)) return;
	done = true;

	char keyName[64] = "F9";
	if (g_ui.key >= VK_F1 && g_ui.key <= VK_F24)
		sprintf_s(keyName, "F%d", g_ui.key - VK_F1 + 1);
	else if (!GetKeyNameTextA(MapVirtualKeyA(g_ui.key, MAPVK_VK_TO_VSC) << 16, keyName, sizeof(keyName)))
		sprintf_s(keyName, "0x%02X", g_ui.key);

	char msg[256];
	sprintf_s(msg, "{FF6EC7}<" PLUGIN_NAME "> {FFFFFF}by {FF6EC7}" PLUGIN_AUTHOR " {FFFFFF}loaded! Press {FF6EC7}%s {FFFFFF}to open the menu.", keyName);
	((void(__thiscall *)(void *, D3DCOLOR, const char *))addMessage)(chat, 0xFFFFFFFF, msg);
}

// ---------------------------------------------------------------- Present hooks

static HRESULT __stdcall hkPresentOuter(IDirect3DDevice9 *dev, const RECT *a, const RECT *b, HWND w, const RGNDATA *r);
static HRESULT __stdcall hkPresent(IDirect3DDevice9 *dev, const RECT *a, const RECT *b, HWND w, const RGNDATA *r);
static HRESULT __stdcall hkReset(IDirect3DDevice9 *dev, D3DPRESENT_PARAMETERS *pp);

// Windows d3d9.dll keeps a writable copy of the device vtable inside CD3DHal (this+0x2F5C) and
// overwrites it back to clean d3d9 functions on Reset() / mode switch / Alt-Tab. Re-applying the
// vtable entries on Reset and before outer Present keeps hkPresent alive across every Reset.
static bool EnsureRealHooks(IDirect3DDevice9 *dev, bool singleHook, bool logRestore)
{
	void **vt = dev ? *(void ***)dev : nullptr;
	if (!vt || !IsReadableRange(vt, (kVtPresent + 1) * sizeof(void *))) return false;
	void *wantPresent = singleHook ? (void *)hkPresentOuter : (void *)hkPresent;
	if (!singleHook && (vt == g_outerVt || vt[kVtPresent] == (void *)hkPresentOuter))
		return false;

	Present_t &origPresent = singleHook ? oPresentOuter : oPresent;
	bool restoredPresent = false;
	if (vt[kVtPresent] != wantPresent) {
		void *cur = vt[kVtPresent];
		// On restore checks, only re-apply if d3d9.dll reset the vtable back to our saved original
		// Present. Never stomp or re-wrap a third-party hook (which would cause vtable fighting,
		// VirtualProtect thrashing on CD3DHal heap memory, or infinite hook recursion).
		if (logRestore && origPresent && cur != (void *)origPresent)
			return false;
		if (cur != (void *)hkPresent && cur != (void *)hkPresentOuter && (!logRestore || !origPresent))
			origPresent = (Present_t)cur;
		MemoryBarrier();
		if (!PatchBytes((DWORD)&vt[kVtPresent], nullptr, &wantPresent, 4))
			return false;
		restoredPresent = true;
	}
	if (vt[kVtReset] != (void *)hkReset) {
		void *cur = vt[kVtReset];
		if (!logRestore || (oReset && cur == (void *)oReset)) {
			if (cur != (void *)hkReset && (!logRestore || !oReset)) oReset = (Reset_t)cur;
			MemoryBarrier();
			void *hookReset = (void *)hkReset;
			PatchBytes((DWORD)&vt[kVtReset], nullptr, &hookReset, 4);
		}
	}
	if (restoredPresent && logRestore)
		Log("Menu: real device Present restored after vtable reset (vt %p)", vt);
	return true;
}

static HRESULT __stdcall hkReset(IDirect3DDevice9 *dev, D3DPRESENT_PARAMETERS *pp)
{
	RenderOnLostDevice();
	Reset_t orig = oReset;
	if (!orig) return D3D_OK;
	HRESULT hr = orig(dev, pp);
	EnsureRealHooks(dev, g_singleHook, true);
	return hr;
}

static HRESULT __stdcall hkPresent(IDirect3DDevice9 *dev, const RECT *a, const RECT *b, HWND w, const RGNDATA *r)
{
	if (g_insideOuter) {
		g_realSeen = true;
		if (!g_frameDrawn) {
			g_frameDrawn = true;
			OnFrameSafe(dev);
		}
		return oPresent(dev, a, b, w, r);
	}
	OnFrameSafe(dev);
	SpikePresentBegin();
	HRESULT hr = oPresent(dev, a, b, w, r);
	SpikePresentEnd(g_openAnim > 0);
	return hr;
}

static IDirect3DDevice9 *ResolveRealDevice(IDirect3DDevice9 *outer)
{
	IDirect3DSwapChain9 *chain = nullptr;
	IDirect3DDevice9 *real = nullptr;
	if (SUCCEEDED(outer->GetSwapChain(0, &chain)) && chain) {
		chain->GetDevice(&real);
		chain->Release();
	}
	if (real) real->Release(); // the game keeps it alive
	return real;
}

static void HookRealPresent(IDirect3DDevice9 *outer)
{
	IDirect3DDevice9 *real = ResolveRealDevice(outer);
	void **vt = real ? *(void ***)real : nullptr;
	if (!vt || vt == g_outerVt || vt[kVtPresent] == (void *)hkPresentOuter) {
		g_singleHook = true;
		g_realDevice = real ? real : outer;
		EnsureRealHooks(g_realDevice, true, false);
		Log("Menu: no proxy in front of the device, drawing in its Present");
		return;
	}
	if (!EnsureRealHooks(real, false, false)) {
		g_singleHook = true;
		Log("Menu: real device Present not hooked, drawing under SA-MP");
		return;
	}
	g_realDevice = real;
	Log("Menu: drawing in the real device's Present (proxy %p, real %p)", outer, real);
}

static HRESULT __stdcall hkPresentOuter(IDirect3DDevice9 *dev, const RECT *a, const RECT *b, HWND w, const RGNDATA *r)
{
	if (!g_realHooked) {
		g_realHooked = true;
		HookRealPresent(dev);
	}
	if (g_insideOuter) return oPresentOuter(dev, a, b, w, r);
	if (!g_singleHook && g_realDevice) {
		void **vt = *(void ***)g_realDevice;
		if (vt && ((oPresent && vt[kVtPresent] == (void *)oPresent) || (oReset && vt[kVtReset] == (void *)oReset)))
			EnsureRealHooks(g_realDevice, false, true);
	}
	g_insideOuter = true;
	g_frameDrawn = false;
	g_realSeen = false;
	if (g_singleHook || g_useOuter) {
		g_frameDrawn = true;
		OnFrameSafe(g_realDevice ? g_realDevice : dev);
	}
	SpikePresentBegin();
	HRESULT hr = oPresentOuter(dev, a, b, w, r);
	SpikePresentEnd(g_openAnim > 0);
	if (!g_singleHook) {
		if (!g_realSeen) {
			IDirect3DDevice9 *curReal = ResolveRealDevice(dev);
			if (curReal && curReal != g_realDevice && EnsureRealHooks(curReal, false, false)) {
				g_realDevice = curReal;
				g_useOuter = false;
				Log("Menu: re-attached to new real device %p", curReal);
			} else if (!g_useOuter) {
				g_useOuter = true;
				Log("Menu: render path outer Present fallback");
			}
		} else if (g_useOuter) {
			g_useOuter = false;
			Log("Menu: render path real device Present");
		}
	}
	g_insideOuter = false;
	return hr;
}

static DWORD WINAPI HookThread(LPVOID)
{
	if (!IsGta10US() || !IsReadableRange((void *)kDevicePtr, sizeof(void *))) {
		Log("Menu: gta_sa.exe is not 1.0 US - menu overlay skipped");
		return 0;
	}
	IDirect3DDevice9 *dev = nullptr;
	for (int i = 0; i < 3000 && !(dev = *(IDirect3DDevice9 **)kDevicePtr); i++) Sleep(100);
	if (!dev) return 0;
	Sleep(1000); // SA-MP / SAMPFUNCS put their proxies in when the device is created
	dev = *(IDirect3DDevice9 **)kDevicePtr;
	if (!dev || !IsReadableRange(dev, sizeof(void *))) return 0;
	void **vt = *(void ***)dev;
	if (!vt || !IsReadableRange(vt, (kVtPresent + 1) * sizeof(void *))) return 0;
	g_outerDevice = dev;
	g_outerVt = vt;
	oPresentOuter = (Present_t)g_outerVt[kVtPresent];
	if (!oPresentOuter) return 0;
	MemoryBarrier();
	void *hook = (void *)hkPresentOuter;
	if (PatchBytes((DWORD)&g_outerVt[kVtPresent], nullptr, &hook, 4))
		Log("Menu: Present hooked, key 0x%02X opens the menu", g_ui.key);

	// Lightweight watchdog: Windows d3d9.dll resets CD3DHal's writable vtable back to clean Present
	// on Reset()/mode switches, and late-loading proxies (SA-MP/SAMPFUNCS/ENB) can swap kDevicePtr
	// after our initial 1s delay. Re-attach automatically without disturbing chaining third-party hooks.
	for (;;) {
		Sleep(500);
		if (g_overlayFaulted) return 0;
		IDirect3DDevice9 *curOuter = *(IDirect3DDevice9 **)kDevicePtr;
		if (!curOuter || !IsReadableRange(curOuter, sizeof(void *))) continue;
		void **curVt = *(void ***)curOuter;
		if (!curVt || !IsReadableRange(curVt, (kVtPresent + 1) * sizeof(void *))) continue;

		void *curPresent = curVt[kVtPresent];
		if (curOuter != g_outerDevice || curVt != g_outerVt) {
			if (curPresent && curPresent != (void *)hkPresentOuter && curPresent != (void *)hkPresent) {
				// CRITICAL: unhook the previous outer/real vtables BEFORE re-binding oPresentOuter
				// to the new proxy device. Otherwise, when the new proxy's Present calls the old
				// underlying device's Present, it would re-enter hkPresentOuter and call the NEW
				// proxy's oPresentOuter with the OLD device's `this` pointer -> instant crash / stack overflow!
				if (g_outerVt && IsReadableRange(g_outerVt, (kVtPresent + 1) * sizeof(void *))) {
					if (g_outerVt[kVtPresent] == (void *)hkPresentOuter && oPresentOuter) {
						void *origP = (void *)oPresentOuter;
						PatchBytes((DWORD)&g_outerVt[kVtPresent], nullptr, &origP, 4);
					}
					if (g_outerVt[kVtReset] == (void *)hkReset && oReset) {
						void *origR = (void *)oReset;
						PatchBytes((DWORD)&g_outerVt[kVtReset], nullptr, &origR, 4);
					}
				}
				if (g_realDevice && IsReadableRange(g_realDevice, sizeof(void *))) {
					void **rvt = *(void ***)g_realDevice;
					if (rvt && IsReadableRange(rvt, (kVtPresent + 1) * sizeof(void *))) {
						if (rvt[kVtPresent] == (void *)hkPresent && oPresent) {
							void *origP = (void *)oPresent;
							PatchBytes((DWORD)&rvt[kVtPresent], nullptr, &origP, 4);
						}
						if (rvt[kVtReset] == (void *)hkReset && oReset) {
							void *origR = (void *)oReset;
							PatchBytes((DWORD)&rvt[kVtReset], nullptr, &origR, 4);
						}
					}
				}
				Sleep(20); // let any in-flight Present/Reset on the main thread finish with old pointers
				g_outerDevice = curOuter;
				g_outerVt = curVt;
				oPresentOuter = (Present_t)curPresent;
				oPresent = nullptr;
				oReset = nullptr;
				g_realHooked = false;
				g_singleHook = false;
				g_realDevice = nullptr;
				g_useOuter = false;
				MemoryBarrier();
				if (PatchBytes((DWORD)&curVt[kVtPresent], nullptr, &hook, 4))
					Log("Menu: Present re-hooked after device/vtable change (dev %p, vt %p)", curOuter, curVt);
			}
			continue;
		}

		if (curPresent == (void *)oPresentOuter) {
			if (PatchBytes((DWORD)&curVt[kVtPresent], nullptr, &hook, 4))
				Log("Menu: Present hook restored after vtable reset (vt %p)", curVt);
			continue;
		}

		if (curPresent && curPresent != (void *)hkPresentOuter && curPresent != (void *)hkPresent) {
			DWORD last = g_lastFrameTick;
			bool stalled = (last == 0) || (GetTickCount() - last >= 2000);
			bool visible = !g_window || !IsIconic(g_window);
			if (stalled && visible) {
				oPresentOuter = (Present_t)curPresent;
				MemoryBarrier();
				if (PatchBytes((DWORD)&curVt[kVtPresent], nullptr, &hook, 4))
					Log("Menu: Present re-hooked after stalled third-party hook (vt %p)", curVt);
			}
		}
	}
}

void SaveUiConfig()
{
	g_ini.SetInt("Menu", "key", g_ui.key);
	g_ini.SetInt("Menu", "dim", g_ui.dim);
	g_ini.SetInt("Menu", "fps_overlay", g_ui.fpsOverlay ? 1 : 0);
	g_ini.SetInt("Menu", "overlay_refresh_ms", g_ui.overlayRefreshMs);
	g_ini.SetInt("Menu", "language", g_ui.language);
}

void MenuStart(const Ini &ini)
{
	g_ui.key = ini.Int("Menu", "key", VK_F9);
	g_ui.dim = ini.Int("Menu", "dim", 30);
	g_ui.fpsOverlay = ini.Bool("Menu", "fps_overlay", false);
	g_ui.overlayRefreshMs = ini.Int("Menu", "overlay_refresh_ms", 500);
	g_ui.language = ini.Int("Menu", "language", -1);
	if (g_ui.overlayRefreshMs < 200) g_ui.overlayRefreshMs = 200;
	if (g_ui.overlayRefreshMs > 1000) g_ui.overlayRefreshMs = 1000;
	if (g_ui.language < 0 || g_ui.language > 2) g_ui.language = g_english ? 2 : 0;
	if (g_ui.language != 1 && g_english != (g_ui.language == 2)) g_ini.SetLanguage(g_ui.language == 2);
	ini.SetInt("Menu", "language", g_ui.language);
	if (!ini.Bool("Menu", "enabled", true)) return;
	SpikeProfilerInit();
	if (ini.Bool("SpikeProfiler", "enabled", false)) SpikeProfilerStart();
	CloseHandle(CreateThread(nullptr, 0, HwMonitorThread, nullptr, 0, nullptr));
	CloseHandle(CreateThread(nullptr, 0, HookThread, nullptr, 0, nullptr));
}
