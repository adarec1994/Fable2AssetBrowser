#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct FlatAssetEntry;




namespace Level {
namespace Creation {

struct SkyThemeOption {
    uint32_t    day_set_hash = 0;
    std::string name;
};

bool ListSkyThemes(std::vector<SkyThemeOption>& out, std::string& error);


uint32_t CurrentSkyTheme(const FlatAssetEntry& entry);

bool ApplySkyTheme(const FlatAssetEntry& entry, uint32_t day_set_hash,
                   std::string& error);

}
}
