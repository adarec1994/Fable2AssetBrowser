#pragma once
#include <string>
#include <vector>

bool mdl_to_glb_file_ex(const std::string& mdl_path,
                        const std::string& glb_path,
                        std::string& err_msg);

bool mdl_to_glb_full(const std::vector<unsigned char>& mdl_data,
                     const std::string& glb_path,
                     std::string& err_msg);

bool mdl_to_glb_file_ex(const std::string& mdl_path,
                        const std::string& glb_path,
                        std::string& err_msg);