#pragma once

#include <string>
#include <vector>

namespace UI {

void draw_audio_player_window();

bool open_audio_player_for(const std::string& display_name,
                           const std::vector<unsigned char>& bytes);

}
