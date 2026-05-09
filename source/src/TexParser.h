#pragma once
#include <string>
#include <vector>
#include "Utilities/State.h"

bool parse_tex_info(const std::vector<unsigned char> &d, TexInfo &out);
bool build_tex_buffer_for_name(const std::string &tex_name, std::vector<unsigned char> &out);
bool build_gui_tex_buffer_for_name(const std::string &tex_name, std::vector<unsigned char> &out);
// preferred_bnk: when non-empty, BNKs that are siblings of this BNK (same
// parent nested archive, or this BNK itself) are searched first so a texture
// clicked inside a nested region archive previews from THAT region's BNKs
// instead of the top-level globals.
bool build_any_tex_buffer_for_name(const std::string &tex_name, std::vector<unsigned char> &out,
                                   const std::string &preferred_bnk = std::string());