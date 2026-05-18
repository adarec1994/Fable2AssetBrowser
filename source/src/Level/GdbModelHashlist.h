#pragma once

#include <cstdint>
#include <string>

namespace Level::GdbModelHashlist {

const char* LookupParentHash(uint32_t parent_hash);
const char* LookupEntityKey(const std::string& entity_key);

}
