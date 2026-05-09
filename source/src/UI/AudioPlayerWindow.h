#pragma once
//
// In-app audio player window. Drawn each frame from the main UI loop.
//
// The window auto-shows when AudioPlayer has a source loaded; the close
// button stops playback and hides it.
//

#include <string>
#include <vector>

namespace UI {

void draw_audio_player_window();

// Convenience: load a path through AudioPlayer and pop the window open
// (paused). Returns false if loading failed; the error is shown via the
// global error popup.
bool open_audio_player_for(const std::string& display_name,
                           const std::vector<unsigned char>& bytes);

} // namespace UI
