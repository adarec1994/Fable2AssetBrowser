#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace BnkWriter {

bool RebuildWithReplacedEntry(const std::string& bnk_path,
                              int file_index,
                              const std::vector<uint8_t>& new_payload,
                              std::string& err);

}
