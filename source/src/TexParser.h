#pragma once
#include <string>
#include <vector>
#include "State.h"

bool parse_tex_info(const std::vector<unsigned char> &d, TexInfo &out);
bool build_tex_buffer_for_name(const std::string &tex_name, std::vector<unsigned char> &out);
bool build_gui_tex_buffer_for_name(const std::string &tex_name, std::vector<unsigned char> &out);