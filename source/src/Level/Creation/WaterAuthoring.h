#pragma once

#include <string>

struct FlatAssetEntry;




namespace Level {
namespace Creation {

bool CreateWaterPlane(const FlatAssetEntry& entry, float height,
                      std::string& error);


bool RemoveWater(const FlatAssetEntry& entry, std::string& error);

}
}
