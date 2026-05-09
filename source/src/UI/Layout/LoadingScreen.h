#pragma once
//
// Full-window loading screen shown after the user picks a folder/ISO and
// while the file-tree builder is running in the background. Replaces the
// inline "Loading file tree..." text that used to live inside the File
// Tree tab — now we block the main layout entirely until the tree is
// ready, so the user doesn't see an empty/half-populated UI.
//

namespace UI {

// True while the file-tree build is in progress (used by MainLayout to
// decide between drawing this screen vs. the regular 3-column layout).
bool loading_in_progress();

// Draw the full-viewport loading screen. Call from inside the main
// fullscreen window; expects to occupy the entire available area.
void draw_loading_screen();

} // namespace UI
