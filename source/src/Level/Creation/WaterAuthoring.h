#pragma once

#include <string>

struct FlatAssetEntry;




namespace Level {
namespace Creation {

struct WaterPlaneSettings {
    float width = 0.0f;
    float length = 0.0f;
    float height = 0.0f;
    float center_x = 0.0f;
    float center_z = 0.0f;
};

bool CreateWaterPlane(const FlatAssetEntry& entry, float height,
                       std::string& error);

bool GetWaterPlaneSettings(const FlatAssetEntry& entry,
                           WaterPlaneSettings& settings,
                           std::string& error);

bool UpdateWaterPlane(const FlatAssetEntry& entry,
                      const WaterPlaneSettings& settings,
                      std::string& error);


bool RemoveWater(const FlatAssetEntry& entry, std::string& error);

}
}
