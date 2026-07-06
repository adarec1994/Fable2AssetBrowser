#pragma once

#ifdef _WIN32
#include <d3d11.h>
#include <dxgi.h>

void AutoPilot_Init();
void AutoPilot_Tick();
void AutoPilot_Capture(ID3D11Device* device,
                       ID3D11DeviceContext* context,
                       IDXGISwapChain* swapchain);
#endif
