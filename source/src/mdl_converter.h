#pragma once
#include <string>

// Converts a Fable 2 .mdl to a skeleton-only GLB (nodes only).
// Mirrors convert.py's bone parsing & filtering, but with detailed debug
// info packed into `err_msg` when something goes wrong.
bool mdl_to_glb_file_ex(const std::string& mdl_path,
                        const std::string& glb_path,
                        std::string& err_msg);
