// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// Menu preview: draws the real menu code (src/ui/menu.cpp, theme.cpp, render_dx9.cpp) with made-up
// game data into an offscreen D3D9 window and saves a PNG per tab, to look at the design without
// starting the game. Not part of the plugin.
//
//   preview.exe <out dir> [width height] [mouse_x mouse_y]
//   -> out\tab0.png .. tab5.png (3 = freezes and input lag)

#include <windows.h>
#include <d3d9.h>
#include <wincodec.h>
#include <math.h>
#include <stdio.h>
#include <string>
#include <vector>

#include "imgui.h"
#include "backends/imgui_impl_win32.h"

#include "../../res/resource.h"
#include "../../src/core/ini.h"
#include "../../src/core/plugin.h"
#include "../../src/fixes/registry.h"
#include "../../src/profilers/profilers.h"
#include "../../src/ui/render_dx9.h"
#include "../../src/ui/theme.h"
#include "../../src/ui/ui.h"

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

// ---------------------------------------------------------------- what overlay.cpp / the profilers give the menu

UiConfig g_ui;
FrameStats g_frames;
UiFonts g_fonts;
IDirect3DTexture9 *g_background;
int g_backgroundW, g_backgroundH;
bool g_captureKey;
void SaveUiConfig() {}

void MenuSelectTab(int tab, bool openRows);

namespace samp_timers {
#include "../../src/profilers/samp_timers_gen.h"
}

static std::vector<ProfRow> g_lua;
static std::vector<TimerRow> g_timers;
static std::vector<OpRow> g_ops;

bool LuaProfilerRunning() { return true; }
void LuaProfilerStartNow() {}
const std::vector<ProfRow> &LuaProfilerRows(double &total)
{
	total = 0;
	for (const ProfRow &r : g_lua) total += r.pct;
	return g_lua;
}
bool SampProfilerRunning() { return true; }
bool SampProfilerStarting() { return false; }
void SampProfilerStartNow() {}
const std::vector<TimerRow> &SampProfilerRows() { return g_timers; }
const std::vector<OpRow> &SampProfilerOps() { return g_ops; }
// [SpikeProfiler]: a made-up minute of play with a few freezes
bool SpikeProfilerRunning() { return true; }
void SpikeProfilerStart() {}
void SpikeProfilerStop() {}
void SpikeProfilerReset() {}
const char *SpikeCauseName(int cause)
{
	static const char *const names[SPIKE_CAUSES] = { "код игры и плагинов", "ожидание: диск, блокировка, Sleep", "Present: видеокарта, DXVK, лимит FPS" };
	return cause >= 0 && cause < SPIKE_CAUSES ? names[cause] : "";
}
const SpikeSummary &SpikeProfilerSummary()
{
	static SpikeSummary s;
	s.seconds = 74, s.frames = 31450, s.spikes = 9;
	s.avgMs = 2.35f, s.low1Ms = 7.7f, s.low01Ms = 19.4f, s.worstMs = 41.0f;
	s.inputMs = 2.1f, s.inputWorstMs = 7.2f, s.inputMeasured = true;
	s.cpuShare = 0.62f, s.waitShare = 0.05f, s.presentShare = 0.33f;
	s.causeCount[0] = 5, s.causeCount[1] = 3, s.causeCount[2] = 1;
	return s;
}
const std::vector<SpikeRow> &SpikeProfilerRows()
{
	static std::vector<SpikeRow> rows;
	if (rows.empty()) {
		SpikeRow r;
		r.at = 12.4, r.ms = 19.6f, r.normal = 2.3f, r.cpu = 17.1f, r.wait = 0.4f, r.present = 2.1f, r.streamKb = 2310, r.faults = 40, r.cause = SPIKE_CPU;
		r.top = "CStreaming::LoadRequestedModels 13.2 мс, CRenderer::ScanSectorList 1.9 мс";
		rows.push_back(r);
		r = {};
		r.at = 31.0, r.ms = 41.0f, r.normal = 2.4f, r.cpu = 1.9f, r.wait = 37.6f, r.present = 1.5f, r.faults = 5120, r.cause = SPIKE_WAIT;
		rows.push_back(r);
		r = {};
		r.at = 58.7, r.ms = 9.8f, r.normal = 2.3f, r.cpu = 1.6f, r.wait = 0.1f, r.present = 8.1f, r.cause = SPIKE_PRESENT;
		rows.push_back(r);
	}
	return rows;
}
int SpikeProfilerFrames(float *out, int max)
{
	for (int i = 0; i < max; i++) out[i] = 2.2f + (i * 37 % 11) * 0.06f + (i % 173 == 90 ? 17.0f : 0.0f) + (i % 401 == 250 ? 38.0f : 0.0f);
	return max;
}
std::string SpikeProfilerText() { return ""; }


static const char *g_watch;
static std::vector<LineRow> g_lines;
void LuaLinesWatch(const char *script) { g_watch = script ? "bully.lua" : nullptr; }
const char *LuaLinesScript() { return g_watch; }
const std::vector<LineRow> &LuaLinesRows(double &samples, double &seconds)
{
	samples = 2480;
	seconds = 31;
	return g_lines;
}
void LuaLinesReset() {}
const std::vector<std::string> &LuaSource(const std::string &)
{
	static std::vector<std::string> src;
	if (src.empty())
		for (int i = 1; i <= 400; i++)
			src.push_back(i % 7 == 0   ? "    for id = 0, sampGetMaxPlayerId(false) do"
			              : i % 7 == 1 ? "        local ok, ped = sampGetCharHandleBySampPlayerId(id)"
			              : i % 7 == 2 ? "        if ok and doesCharExist(ped) then"
			              : i % 7 == 3 ? "            local x, y, z = getCharCoordinates(ped)"
			              : i % 7 == 4 ? "            renderFontDrawText(font, sampGetPlayerNickname(id), sx, sy, 0xFFFFFFFF)"
			              : i % 7 == 5 ? "        end"
			                           : "    end -- Проверка всех игроков");
	return src;
}

Fix g_fixLuaProfiler = { .section = "LuaProfiler", .title = "Lua профайлер" };
Fix g_fixSampProfiler = { .section = "SampProfiler", .title = "SA-MP профайлер" };

static float Rand01()
{
	static unsigned s = 12345;
	s = s * 1103515245 + 12345;
	return ((s >> 8) & 0xFFFF) / 65535.0f;
}

static void MakeData()
{
	for (int i = 0; i < FrameStats::kHistory; i++) {
		float ms = 6.6f + 0.6f * sinf(i * 0.21f) + Rand01() * 0.8f + (i % 47 == 0 ? 5.0f : 0.0f);
		g_frames.frameMs[i] = ms;
	}
	g_frames.head = 0;
	g_frames.count = FrameStats::kHistory;
	g_frames.fps = 148.0f;
	g_frames.ms = 6.76f;
	g_frames.low1 = 97.0f;
	g_frames.cpu = 71.0f;

	const char *scripts[] = { "bully.lua", "euphoria.lua", "Sweet Connect.lua", "ugenrl.lua", "pathfinder.lua", "timecyc-editor.lua" };
	for (int i = 0; i < 6; i++) {
		ProfRow r;
		r.name = scripts[i];
		r.pct = 4.0 / (i + 1);
		r.ms = r.pct * 10;
		r.calls = 150 * (i + 1);
		for (int k = 0; k < 3; k++) {
			ProfRow p;
			p.name = k == 0 ? "main (thread)" : k == 1 ? "onReceiveRpc" : "imgui.OnFrame";
			p.pct = r.pct / (k + 2);
			p.ms = p.pct * 10;
			p.calls = 150;
			r.parts.push_back(p);
		}
		g_lua.push_back(r);
	}

	using namespace samp_timers;
	const int n = sizeof(kSampTimerFuncs) / sizeof(kSampTimerFuncs[0]);
	std::vector<double> incl(n, 0), self(n, 0);
	for (int i = n - 1; i >= 0; i--) {
		self[i] = Rand01() < 0.6f ? Rand01() * Rand01() * 0.8 : 0;
		incl[i] += self[i];
		int p = kSampTimerFuncs[i].parent;
		if (p >= 0) incl[p] += incl[i];
	}
	for (int i = 0; i < n; i++) {
		if (incl[i] <= 0.001) continue;
		g_timers.push_back({ i, kSampTimerFuncs[i].parent, kSampTimerFuncs[i].rva, kSampTimerFuncs[i].label, kSampTimerFuncs[i].gta != 0,
		                     incl[i], self[i], 60.0 + Rand01() * 3000 });
	}
	for (int k = 0; k < 6; k++) g_ops.push_back({ (WORD)(0x06BD + k), 0x67577u + k * 0x40, 2.0 / (k + 1), 400.0 / (k + 1) });

	Log("SA-MP 0.3.7-R1");
	Log("ColPlaneCache: collision plane cache 50 -> 1000");
	Log("RefPool: reference pool grows instead of pruning every call");
	Log("NametagLOSCache: 3/3 line-of-sight checks cached (cache_ms=100)");
	Log("PlayerLoop: only players with a ped are processed");
	Log("SampfuncsDrawFast: SAMPFUNCS not found - skipped");
	Log("attached: ID3DXFont::DrawText cached (check_every=256, verify_all=0)");
	Log("Menu: ready (1600x900 background)");
}

// ---------------------------------------------------------------- D3D9 + PNG

static bool SavePng(IDirect3DDevice9 *dev, const wchar_t *path)
{
	IDirect3DSurface9 *rt = nullptr, *sys = nullptr;
	if (FAILED(dev->GetRenderTarget(0, &rt))) return false;
	D3DSURFACE_DESC d;
	rt->GetDesc(&d);
	dev->CreateOffscreenPlainSurface(d.Width, d.Height, d.Format, D3DPOOL_SYSTEMMEM, &sys, nullptr);
	bool ok = sys && SUCCEEDED(dev->GetRenderTargetData(rt, sys));
	D3DLOCKED_RECT lr;
	if (ok && SUCCEEDED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
		IWICImagingFactory *wic = nullptr;
		CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
		IWICStream *stream = nullptr;
		IWICBitmapEncoder *enc = nullptr;
		IWICBitmapFrameEncode *frame = nullptr;
		wic->CreateStream(&stream);
		stream->InitializeFromFilename(path, GENERIC_WRITE);
		wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
		enc->Initialize(stream, WICBitmapEncoderNoCache);
		enc->CreateNewFrame(&frame, nullptr);
		frame->Initialize(nullptr);
		frame->SetSize(d.Width, d.Height);
		// 24-bit BGR rows: what the PNG encoder takes without converting
		WICPixelFormatGUID fmt = GUID_WICPixelFormat24bppBGR;
		frame->SetPixelFormat(&fmt);
		UINT stride = (d.Width * 3 + 3) & ~3u;
		std::vector<BYTE> rgb(stride * d.Height);
		for (UINT y = 0; y < d.Height; y++)
			for (UINT x = 0; x < d.Width; x++)
				memcpy(&rgb[y * stride + x * 3], (BYTE *)lr.pBits + y * lr.Pitch + x * 4, 3);
		frame->WritePixels(d.Height, stride, (UINT)rgb.size(), rgb.data());
		frame->Commit();
		enc->Commit();
		frame->Release();
		enc->Release();
		stream->Release();
		wic->Release();
		sys->UnlockRect();
	}
	if (sys) sys->Release();
	rt->Release();
	return ok;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		printf("preview.exe <out dir> [w h] [mx my]\n");
		return 1;
	}
	std::string out = argv[1];
	int w = argc > 3 ? atoi(argv[2]) : 1600, h = argc > 3 ? atoi(argv[3]) : 900;
	float mx = argc > 5 ? (float)atof(argv[4]) : -1, my = argc > 5 ? (float)atof(argv[5]) : -1;
	CoInitialize(nullptr);

	g_module = GetModuleHandleA(nullptr);
	GetModuleFileNameA(nullptr, g_dir, MAX_PATH);
	if (char *slash = strrchr(g_dir, '\\')) *slash = 0;
	g_mainThread = GetCurrentThreadId();
	g_ini.Open();
	g_ui.language = argc > 6 ? atoi(argv[6]) : g_ini.Int("Menu", "language", g_english ? 2 : 0);
	if (g_ui.language != 1) g_english = g_ui.language == 2;
	FixesLoad(g_ini);
	int n;
	Fix *const *fixes = FixList(n);
	for (int i = 0; i < n; i++) fixes[i]->active = fixes[i]->enabled && (i % 7 != 3);

	WNDCLASSA wc = { 0, DefWindowProcA, 0, 0, g_module, nullptr, nullptr, nullptr, nullptr, "mfbpreview" };
	RegisterClassA(&wc);
	HWND wnd = CreateWindowA("mfbpreview", "preview", WS_POPUP, 0, 0, w, h, nullptr, nullptr, g_module, nullptr);
	IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
	D3DPRESENT_PARAMETERS pp = {};
	pp.Windowed = TRUE;
	pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	pp.BackBufferFormat = D3DFMT_X8R8G8B8;
	pp.BackBufferWidth = w;
	pp.BackBufferHeight = h;
	pp.hDeviceWindow = wnd;
	IDirect3DDevice9 *dev = nullptr;
	if (FAILED(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, wnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev))) {
		printf("no d3d9 device\n");
		return 1;
	}

	MakeData();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.ConfigErrorRecoveryEnableAssert = false;
	LoadFonts();
	ApplyTheme();
	ImGui_ImplWin32_Init(wnd);
	RenderInit(dev);
	g_background = LoadResourceTexture(IDR_BACKGROUND, g_backgroundW, g_backgroundH);

	// a game-ish frame behind the menu
	auto frame = [&](bool save, const std::string &name) {
		ImGui_ImplWin32_NewFrame();
		io.DisplaySize = ImVec2((float)w, (float)h);
		io.DeltaTime = 1.0f / 60.0f;
		io.AddMousePosEvent(mx, my);
		ImGui::NewFrame();
		bool open = true;
		g_menuAnim = 1.0f;
		DrawMenu(open);
		ImGui::EndFrame();
		ImGui::Render();
		dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(70, 92, 120), 1, 0);
		dev->BeginScene();
		RenderDrawData(ImGui::GetDrawData());
		dev->EndScene();
		if (save) {
			std::wstring p(name.begin(), name.end());
			printf("%s %s\n", name.c_str(), SavePng(dev, p.c_str()) ? "ok" : "FAILED");
		}
	};
	for (int tab = 0; tab < 6; tab++) {
		MenuSelectTab(tab, false);
		for (int f = 0; f < 90; f++) frame(false, "");
		frame(true, out + "\\tab" + std::to_string(tab) + ".png");
	}
	g_watch = "bully.lua";
	for (int k = 0; k < 14; k++) {
		LineRow r;
		r.path = "C:\game\moonloader\bully.lua";
		r.file = "bully.lua";
		r.line = 120 + k * 17;
		r.samples = 900.0 / (k + 1);
		r.share = 0.36 / (k + 1);
		r.pct = 4.0 * r.share;
		g_lines.push_back(r);
	}
	for (int tab : { 0, 1, 2 }) { // rows opened
		MenuSelectTab(tab, true);
		for (int f = 0; f < 90; f++) frame(false, "");
		frame(true, out + "\\tab" + std::to_string(tab) + "_open.png");
	}
	return 0;
}
