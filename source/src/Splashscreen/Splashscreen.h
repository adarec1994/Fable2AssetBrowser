// Splash screen — shown when no Fable 2 root directory has been picked yet.
// Renders the scrolling background, animated sparkly logo, and the
// "Browse Folder" / "Browse ISO" buttons.
//
// Drives `S.root_dir` once the user picks something:
//   - Folder browse  → directly sets S.root_dir
//   - ISO browse     → extracts the ISO into a cache folder, then sets S.root_dir
//
// This file replaces what used to live inline in UI_Main.cpp; the logic is
// split across several .cpp files in this folder for readability.

#pragma once

#ifdef _WIN32
struct ID3D11Device;
#else
struct GLFWwindow;
#endif

namespace Splash {

// Drive one frame of the splash screen. Pulls textures lazily on first call,
// runs the sparkle animation, draws background+logo+buttons, and dispatches
// the file/ISO dialogs.
#ifdef _WIN32
void draw(ID3D11Device* device);
#else
void draw(GLFWwindow* window);
#endif

// Release any GPU resources held by the splash. Safe to call repeatedly.
void release_resources();

} // namespace Splash

// Call once after ImGui's renderer backend is initialised, BEFORE the
// first NewFrame. Pulls the FontAwesome ttf out of the exe and merges
// it into the default font. Defined here (outside namespace Splash) to
// keep main.cpp's forward-declare-light style.
void Splashscreen_init_icon_font_at_startup();

