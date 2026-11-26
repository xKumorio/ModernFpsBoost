// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// Shared between the overlay (hooks, input, frame timing) and the menu drawing.
#pragma once

#include <windows.h>

struct ImFont;
struct IDirect3DTexture9;

struct UiConfig {
	int  key = VK_F9;            // [Menu] key: opens/closes the menu
	int  dim = 85;               // [Menu] dim: background darkening, %
	bool fpsOverlay = false;     // [Menu] fps_overlay: telemetry strip in the corner while menu is closed
	int  overlayRefreshMs = 500; // [Menu] overlay_refresh_ms: HUD / monitor readout refresh cadence (200..1000 ms)
	int  language = 0;           // [Menu] language: RU=0, ZH=1, EN=2
};
extern UiConfig g_ui;

struct FrameStats {
	static const int kHistory = 240;
	float frameMs[kHistory] = {}; // ring, newest at head - 1
	int   head = 0, count = 0;
	float fps = 0, ms = 0, low1 = 0;       // rolling real-time values (for internal dt / sparkline)
	float dispFps = 0, dispMs = 0, dispLow1 = 0; // latched readable values updated every overlayRefreshMs
	float cpu = 0;                         // game main thread CPU time, % of one core
	float sysCpu = 0;                      // total system CPU usage, % (GetSystemTimes)
	float gpu = -1.0f;                     // total 3D GPU usage, % (PDH GPU Engine, -1 while unavailable)
};
extern FrameStats g_frames;

struct UiFonts {
	ImFont *body, *medium, *title, *number, *tiny, *mono; // Segoe UI / Consolas
	ImFont *icon14, *icon16, *icon20, *icon28;            // Lucide
};
extern UiFonts g_fonts;

extern IDirect3DTexture9 *g_background;
extern int g_backgroundW, g_backgroundH;

// key capture for the hotkey setting
extern bool g_captureKey;

extern float g_menuAnim; // 0 closed .. 1 open (eased by the menu)

void DrawMenu(bool &open);
void DrawFpsOverlay();
void SaveUiConfig();
