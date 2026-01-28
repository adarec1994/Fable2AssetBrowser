#pragma once
#include <string>
#include <vector>
#include "State.h"

struct GlobalHit {
    std::string bnk_path;
    std::string file_name;
    int index;
    uint32_t size;
};

void extract_file_one(const std::string &bnk_path, const BNKItemUI &item, const std::string &base_out_dir, bool convert_audio = true);
void on_extract_selected_raw();
void on_extract_selected_wav();
void on_dump_all_raw();
void on_export_wavs();
void on_rebuild_and_extract();
void on_rebuild_and_extract_models();
void on_rebuild_and_extract_one(const std::string &tex_name);
void on_rebuild_and_extract_one_mdl(const std::string &mdl_name);
void on_dump_all_global(const std::vector<GlobalHit>& hits);
void on_export_wavs_global(const std::vector<GlobalHit>& hits);
void on_rebuild_and_extract_global_tex(const std::vector<GlobalHit>& hits);
void on_rebuild_and_extract_global_mdl(const std::vector<GlobalHit>& hits);
void on_extract_adb_selected();
void on_extract_all_adb();
void on_export_mdl_to_glb();
void on_export_all_mdl_to_glb();
void on_export_global_mdl_to_glb(const std::vector<GlobalHit>& hits);