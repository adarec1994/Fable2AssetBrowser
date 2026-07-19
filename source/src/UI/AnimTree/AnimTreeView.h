#pragma once

#include <cstdint>
#include <string>

namespace AnimTreeUI {

// True when the indexed catalogs hold an animation tree for this entity
// (directly or via its spawn-marker creature template).
bool Available(uint32_t entity_hash);

void Open(uint32_t entity_hash, const std::string& title);

void Draw();
void Shutdown();

}
