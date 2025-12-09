#pragma once
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <d3d11.h>
void draw_main(HWND hwnd, ID3D11Device* device);
#else
struct GLFWwindow;
void draw_main(GLFWwindow* window);
#endif