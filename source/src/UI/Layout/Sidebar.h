#pragma once
//
// Left sidebar column. Modeled after the Twinsanity Editor layout: a
// fixed-width 180 px vertical strip on the far left with the app's logo
// at the top and branding/version text below. Action buttons (Open,
// Refresh, Settings, etc.) belong here too — wired up later.
//

#ifdef _WIN32
struct ID3D11Device;
#endif

namespace UI {

constexpr float kSidebarWidth = 180.0f;

#ifdef _WIN32
void draw_sidebar(ID3D11Device* device);
#else
void draw_sidebar();
#endif

} // namespace UI
