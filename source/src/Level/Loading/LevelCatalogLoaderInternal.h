#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Level::Loading {

struct AuthoredEntityBinding {
    uint32_t entity_hash = 0;
    std::string name;
    std::vector<uint32_t> model_hashes;
};

const AuthoredEntityBinding* FindAuthoredEntityBinding(
    uint32_t authored_name_hash);

}
