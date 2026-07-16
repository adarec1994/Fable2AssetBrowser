#pragma once
#include <string>
#ifdef _WIN32
#include <d3d11.h>
void draw_left_panel(ID3D11Device* device);
void draw_right_panel(ID3D11Device* device);

void process_pending_loads(ID3D11Device* device);
#else
void draw_left_panel();
void draw_right_panel();
void process_pending_loads();
#endif
void draw_folder_dialog();
void draw_file_table();
void draw_global_results_table();
void refresh_file_table();
void pick_bnk(const std::string &path);
void open_folder_logic(const std::string &sel);
void open_iso_logic(const std::string& iso_path);
bool select_quest_script_by_query(const std::string& query);
bool select_entity_by_query(const std::string& query);
void request_open_create_npc();

float left_panel_min_width();

bool tree_build_in_progress();
bool tree_build_finished();
float tree_build_elapsed_seconds();
float tree_build_progress();
int   tree_build_done_units();
int   tree_build_total_units();
std::string tree_build_current_label();
