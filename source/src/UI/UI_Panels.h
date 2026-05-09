#pragma once
#include <string>
#ifdef _WIN32
#include <d3d11.h>
void draw_left_panel(ID3D11Device* device);
void draw_right_panel(ID3D11Device* device);
#else
void draw_left_panel();
void draw_right_panel();
#endif
void draw_folder_dialog();
void draw_file_table();
void draw_global_results_table();
void refresh_file_table();
void pick_bnk(const std::string &path);
void open_folder_logic(const std::string &sel);
void open_iso_logic(const std::string& iso_path);

// Tree-build state introspection — used by the LoadingScreen to draw
// a real progress bar with the current BNK being processed.
bool tree_build_in_progress();
bool tree_build_finished();
float tree_build_elapsed_seconds();
float tree_build_progress();          // [0,1] — portion of work done
int   tree_build_done_units();        // BNKs processed so far
int   tree_build_total_units();       // total BNKs (incl. nested) to process
std::string tree_build_current_label();