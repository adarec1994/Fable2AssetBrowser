#pragma once

#include "QuestGraph.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace Quest {



struct WorldIndexAsset {
    std::string name;
    std::string full_path;
    std::string bnk_path;
    int file_index = -1;
};




std::vector<std::string> FindWorldReferenceNames(
    const std::string& decompiled_lua);

std::unordered_map<std::string, std::vector<WorldEntityPlacement>>
IndexWorldPlacements(const std::vector<WorldIndexAsset>& level_assets,
                     const std::vector<std::string>& entity_names);

}
