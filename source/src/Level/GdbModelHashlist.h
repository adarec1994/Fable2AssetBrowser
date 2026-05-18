#pragma once

#include <cstdint>
#include <string>

namespace Level::GdbModelHashlist {

// Curated offline from model/save/GDB dumps. These are authoritative
// overrides for GDB entity/archetype names before fuzzy model scoring.
const char* LookupParentHash(uint32_t parent_hash);
const char* LookupEntityKey(const std::string& entity_key);

}
