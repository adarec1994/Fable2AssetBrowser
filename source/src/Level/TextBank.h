#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace TextBank {

bool LoadForRoot(const std::string& root_dir);
bool Loaded();
const std::string& LoadedPath();

bool HasTag(uint32_t tag_hash);

bool Lookup(uint32_t tag_hash, std::string& out_utf8);

uint32_t AllocTagHash(const std::string& seed);

bool ApplyEdits(const std::string& root_dir,
                const std::unordered_map<uint32_t, std::string>& edits,
                std::string& err);

}
