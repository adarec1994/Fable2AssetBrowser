
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace ISO {

bool extract_iso(const std::string& iso_path,
                 const std::string& dest_dir,
                 std::string* err_out = nullptr,
                 std::function<void(uint64_t, uint64_t)> progress = nullptr);

bool is_xbox_iso(const std::string& iso_path);

}
