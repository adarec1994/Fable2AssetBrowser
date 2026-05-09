#pragma once
//
// Central render panel — fills the left/center column of the main layout.
// Hosts the 3D model preview, the texture preview, or a placeholder when
// nothing is loaded. Replaces the old draw_right_panel() which was full
// of action buttons and the file table — all of those went away with
// the TT-Lab-style overhaul; the user clicks files in the tree and the
// result lands here.
//

#ifdef _WIN32
struct ID3D11Device;
#endif

namespace UI {

#ifdef _WIN32
void draw_render_panel(ID3D11Device* device);
#else
void draw_render_panel();
#endif

} // namespace UI
