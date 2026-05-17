#pragma once

#ifdef _WIN32
struct ID3D11Device;
#else
struct GLFWwindow;
#endif

namespace Splash {

#ifdef _WIN32
void draw(ID3D11Device* device);
#else
void draw(GLFWwindow* window);
#endif

void release_resources();

}

void Splashscreen_init_icon_font_at_startup();
