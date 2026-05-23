#pragma once

#include <string>
#include <vector>

bool mdl_to_fbx_full(const std::vector<unsigned char>& mdl_data,
                     const std::string& fbx_path,
                     const std::string& mdl_source_path,
                     std::string& err_msg,
                     bool include_animations = true);
