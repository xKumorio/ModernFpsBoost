// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// Draws ImGui with a D3D9 device (fixed function, cached state blocks) and loads textures.
#pragma once

struct IDirect3DDevice9;
struct IDirect3DTexture9;
struct ImDrawData;

bool RenderInit(IDirect3DDevice9 *device); // font atlas texture; ImGui fonts must be added before
void RenderOnLostDevice();                 // release cached D3D9 state blocks before Reset()
void RenderDrawData(ImDrawData *data);

// JPEG/PNG from an RCDATA resource of this module -> managed texture; width/height of the image
IDirect3DTexture9 *LoadResourceTexture(int id, int &width, int &height);
