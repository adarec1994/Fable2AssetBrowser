#pragma once
#include <string>
#include <vector>
#include <optional>

bool is_audio_file(const std::string &n);
bool is_tex_file(const std::string &n);
bool is_mdl_file(const std::string &n);
bool is_texture_bnk_selected();
bool is_model_bnk_selected();
std::vector<std::string> filtered_bnk_paths();
bool name_matches_filter(const std::string &name, const std::string &filter);
int count_visible_files();
bool any_wav_in_bnk();
bool any_tex_in_bnk();
bool any_mdl_in_bnk();
std::optional<std::string> find_bnk_by_filename(const std::string &fname_lower);