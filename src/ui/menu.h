// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// In-game menu (F9): fixes, profilers, FPS monitor, logs, settings.
#pragma once

class Ini;

void MenuStart(const Ini &ini); // DllMain: reads [Menu], hooks Present once the device exists
