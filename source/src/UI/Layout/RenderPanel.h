#pragma once

#include <cstddef>

#ifdef _WIN32
struct ID3D11Device;
#endif

struct LevelSpawnMarker;
bool is_player_start_marker(const LevelSpawnMarker& marker);

namespace UI {

#ifdef _WIN32
void draw_render_panel(ID3D11Device* device);
bool select_level_marker(std::size_t marker_index);
bool select_level_entity_model(unsigned int entity_hash);
void request_level_add_menu_at(const float engine_pos[3]);
#else
void draw_render_panel();
#endif

}
