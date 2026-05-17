#pragma once

#include <string>
#include <vector>

namespace Level {

struct VfsConfig {
    std::vector<std::string> bnk_paths;

    std::vector<std::string> texture_body_bnks;

    std::vector<std::string> model_bnks;
    std::vector<std::string> streaming_bnks;
};

VfsConfig ParseVfsConfig(const std::vector<uint8_t>& bytes);

}
