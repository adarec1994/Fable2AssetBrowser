#pragma once

#include <string>
#include <vector>

struct FlatAssetEntry;

namespace Level {
namespace Creation {

struct NewLevelParams {
    std::string name;
    std::string donor_region = "chamberofseasons";
};

struct NewLevelResult {
    bool        ok = false;
    std::string error;
    std::string engine_level_virtual_path;
    std::vector<std::string> written_files;
};

bool ValidateLevelName(const std::string& name, std::string& error);

bool LevelNameExists(const std::string& name);

NewLevelResult CreateNewLevel(const NewLevelParams& params);




bool DeleteCustomLevel(const FlatAssetEntry& entry, std::string& error);

std::string ResolveGameDataDir();

}
}
