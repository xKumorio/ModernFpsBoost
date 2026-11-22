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
static void **g_outerVt;
static bool g_realHooked, g_singleHook;
static IDirect3DDevice9 *g_realDevice;
static thread_local bool g_insideOuter, g_frameDrawn, g_realSeen;
static bool g_useOuter;

static HWND g_window;
static WNDPROC g_prevProc;
static bool g_open;
static float g_openAnim;
static bool g_imgui, g_imguiFailed;
static int g_cursorMode = CURSOR_NONE;
static DWORD g_cursorAt;

static void GreetInChat();

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

// true: the message is the menu's
static bool MenuInput(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
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
	if (IsInput(m)) {
		if (MenuInput(w, m, wp, lp)) return 0; // sent, not posted: the hook did not see it
	} else if (g_open && g_imgui) {
		ImGui_ImplWin32_WndProcHandler(w, m, wp, lp); // focus, keyboard layout, ...
	}
	return CallWindowProcW(g_prevProc, w, m, wp, lp);
}

static void SetSampCursor(int mode)
{
	void **gameVar = (void **)SampAt(S_GAME);
	BYTE *setMode = SampAt(S_SET_CURSOR_MODE);
	if (!gameVar || !*gameVar || !setMode) return;
	((void(__thiscall *)(void *, int, BOOL))setMode)(*gameVar, mode, mode == CURSOR_NONE);
}

// SA-MP resets the cursor itself (chat, dialogs): while the menu is open it is set again twice a second
static void UpdateCursor()
{
	int want = g_open ? CURSOR_LOCKCAMANDCONTROL : CURSOR_NONE;
	DWORD now = GetTickCount();
	if (want != g_cursorMode || (want != CURSOR_NONE && now - g_cursorAt > 500)) {
		SetSampCursor(want);
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
	if (g_imgui) return true;
	if (g_imguiFailed) return false;
	g_imguiFailed = true;
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.ConfigDebugHighlightIdConflicts = false; // a player build: no debug popups over the menu
	io.LogFilename = nullptr;
	io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
	io.MouseDrawCursor = false;
	io.ConfigErrorRecoveryEnableAssert = false; // a UI mistake must not take the game down
	LoadFonts();
	ApplyTheme();
	if (!ImGui_ImplWin32_Init(g_window) || !RenderInit(device)) {
		Log("Menu: ImGui init failed");
		return false;
	}
	g_background = LoadResourceTexture(IDR_BACKGROUND, g_backgroundW, g_backgroundH);
	if (!g_background) Log("Menu: background image not loaded");
	g_imgui = true;
	g_imguiFailed = false;
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
		if (!IsWindow(g_window)) g_window = *(HWND *)kWindowPtr;
		if (g_window) g_prevProc = (WNDPROC)SetWindowLongPtrW(g_window, GWLP_WNDPROC, (LONG_PTR)WndProc);
		if (!g_prevProc) {
			static bool logged;
			if (!logged) Log("Menu: input attach failed (window %p, error %lu)", g_window, GetLastError());
			logged = true;
			return;
		}
		Log("Menu: input attached to window %p", g_window);
		g_msgHook = SetWindowsHookExW(WH_GETMESSAGE, MsgHook, nullptr, GetWindowThreadProcessId(g_window, nullptr));
		if (!g_msgHook) Log("Menu: message hook failed (%lu), keys reach the game first", GetLastError());
	}
	UpdateCursor();
	GreetInChat();

	float dt = g_frames.ms > 0 ? g_frames.ms / 1000.0f : 0.016f;
	// linear in time, eased by the menu: opens in ~0.20 s, closes in ~0.15 s
	g_openAnim = g_open ? min(1.0f, g_openAnim + dt / 0.20f) : max(0.0f, g_openAnim - dt / 0.15f);
	if (!g_open && g_openAnim < 0.01f) g_openAnim = 0;
	const bool visible = (g_openAnim > 0 || g_ui.fpsOverlay);
	InterlockedExchange(&g_hwWanted, visible ? 1 : 0);
	if (!visible) return;
	if (!InitImGui(device)) return;
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
		io.DisplaySize = ImVec2((float)(s_w > 0 ? s_w : 1920), (float)(s_h > 0 ? s_h : 1080));
		io.DeltaTime = dt > 0.0001f ? dt : (1.0f / 60.0f);
	}
	ImGui::NewFrame();
	if (g_ui.fpsOverlay && g_openAnim < 0.5f) DrawFpsOverlay();
	if (g_openAnim > 0) {
		g_menuAnim = g_openAnim;
		bool open = g_open;
		DrawMenu(open);
		if (!open) g_open = false;
	}
	ImGui::EndFrame();
	ImGui::Render();
	RenderDrawData(ImGui::GetDrawData());
	device->EndScene();
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
	bool connected = net && *net && *(int *)(*net + g_sl.netState) == g_sl.netConnected;
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
	void *chat = chatVar ? *chatVar : nullptr;
	if (!chat || !addMessage) return;
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
	if (!vt) return false;
	void *wantPresent = singleHook ? (void *)hkPresentOuter : (void *)hkPresent;
	if (!singleHook && (vt == g_outerVt || vt[kVtPresent] == (void *)hkPresentOuter))
		return false;

	bool restoredPresent = false;
	if (vt[kVtPresent] != wantPresent) {
		void *cur = vt[kVtPresent];
		if (cur != (void *)hkPresent && cur != (void *)hkPresentOuter) {
			if (singleHook) oPresentOuter = (Present_t)cur;
			else oPresent = (Present_t)cur;
		}
		if (!PatchBytes((DWORD)&vt[kVtPresent], nullptr, &wantPresent, 4))
			return false;
		restoredPresent = true;
	}
	if (vt[kVtReset] != (void *)hkReset) {
		void *cur = vt[kVtReset];
		if (cur != (void *)hkReset) oReset = (Reset_t)cur;
		void *hookReset = (void *)hkReset;
		PatchBytes((DWORD)&vt[kVtReset], nullptr, &hookReset, 4);
	}
	if (restoredPresent && logRestore)
		Log("Menu: real device Present restored after vtable reset (vt %p)", vt);
	return true;
}

static HRESULT __stdcall hkReset(IDirect3DDevice9 *dev, D3DPRESENT_PARAMETERS *pp)
{
	RenderOnLostDevice();
	HRESULT hr = oReset(dev, pp);
	EnsureRealHooks(dev, g_singleHook, true);
	return hr;
}

static HRESULT __stdcall hkPresent(IDirect3DDevice9 *dev, const RECT *a, const RECT *b, HWND w, const RGNDATA *r)
{
	if (g_insideOuter) {
		g_realSeen = true;
		if (!g_frameDrawn) {
			g_frameDrawn = true;
			OnFrame(dev);
		}
		return oPresent(dev, a, b, w, r);
	}
	OnFrame(dev);
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
		if (vt && (vt[kVtPresent] != (void *)hkPresent || vt[kVtReset] != (void *)hkReset))
			EnsureRealHooks(g_realDevice, false, true);
	}
	g_insideOuter = true;
	g_frameDrawn = false;
	g_realSeen = false;
	if (g_singleHook || g_useOuter) {
		g_frameDrawn = true;
		OnFrame(g_realDevice ? g_realDevice : dev);
	}
	SpikePresentBegin();
	HRESULT hr = oPresentOuter(dev, a, b, w, r);
	SpikePresentEnd(g_openAnim > 0);
	if (!g_singleHook) {
		if (!g_realSeen) {
			IDirect3DDevice9 *curReal = ResolveRealDevice(dev);
			if (curReal && curReal != g_realDevice && EnsureRealHooks(curReal, false, true)) {
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
	IDirect3DDevice9 *dev = nullptr;
	for (int i = 0; i < 3000 && !(dev = *(IDirect3DDevice9 **)kDevicePtr); i++) Sleep(100);
	if (!dev) return 0;
	Sleep(1000); // SA-MP / SAMPFUNCS put their proxies in when the device is created
	dev = *(IDirect3DDevice9 **)kDevicePtr;
	g_outerVt = *(void ***)dev;
	oPresentOuter = (Present_t)g_outerVt[kVtPresent];
	void *hook = (void *)hkPresentOuter;
	if (PatchBytes((DWORD)&g_outerVt[kVtPresent], nullptr, &hook, 4))
		Log("Menu: Present hooked, key 0x%02X opens the menu", g_ui.key);
	return 0;
}

void SaveUiConfig()
{
	g_ini.SetInt("Menu", "key", g_ui.key);
	g_ini.SetInt("Menu", "dim", g_ui.dim);
	g_ini.SetInt("Menu", "fps_overlay", g_ui.fpsOverlay ? 1 : 0);
	g_ini.SetInt("Menu", "overlay_refresh_ms", g_ui.overlayRefreshMs);
}

void MenuStart(const Ini &ini)
{
	g_ui.key = ini.Int("Menu", "key", VK_F9);
	g_ui.dim = ini.Int("Menu", "dim", 30);
	g_ui.fpsOverlay = ini.Bool("Menu", "fps_overlay", false);
	g_ui.overlayRefreshMs = ini.Int("Menu", "overlay_refresh_ms", 500);
	if (g_ui.overlayRefreshMs < 200) g_ui.overlayRefreshMs = 200;
	if (g_ui.overlayRefreshMs > 1000) g_ui.overlayRefreshMs = 1000;
	if (!ini.Bool("Menu", "enabled", true)) return;
	SpikeProfilerInit();
	if (ini.Bool("SpikeProfiler", "enabled", false)) SpikeProfilerStart();
	CloseHandle(CreateThread(nullptr, 0, HwMonitorThread, nullptr, 0, nullptr));
	CloseHandle(CreateThread(nullptr, 0, HookThread, nullptr, 0, nullptr));
}
