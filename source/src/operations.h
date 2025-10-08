#pragma once
#include <string>
#include "State.h"

void extract_file_one(const std::string &bnk_path, const BNKItemUI &item, const std::string &base_out_dir, bool convert_audio = true);
void on_extract_selected_raw();
void on_extract_selected_wav();
void on_dump_all_raw();
void on_export_wavs();
void on_rebuild_and_extract();
void on_rebuild_and_extract_models();
void on_rebuild_and_extract_one(const std::string &tex_name);
void on_rebuild_and_extract_one_mdl(const std::string &mdl_name);