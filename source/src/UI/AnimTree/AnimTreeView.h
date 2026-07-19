#pragma once

#include <cstdint>
#include <string>

namespace AnimTreeUI {

bool Available(uint32_t entity_hash);

void Open(uint32_t entity_hash, const std::string& title);

void Draw();
void Shutdown();

}
