#pragma once

#include <cstdint>
#include <string>

struct FlatAssetEntry;




namespace Level {
namespace Creation {

struct LandscapeParams {
    int   grid_w      = 129;    
    int   grid_h      = 129;    
    float tile_size   = 2.0f;   
    float base_height = 3.0f;   
    float min_height  = 0.0f;   
    float max_height  = 60.0f;  
};



bool IsCustomLooseLevel(const FlatAssetEntry& entry,
                        std::string* region = nullptr);




bool GetNativeLandscapeLayout(const FlatAssetEntry& entry, int& grid_w,
                              int& grid_h, float& sample_spacing,
                              std::string& error);

bool CreateFlatLandscape(const FlatAssetEntry& entry,
                         const LandscapeParams& params,
                         std::string& error);




bool ProbeHeightmapSize(const std::string& image_path, int& out_w,
                        int& out_h);

bool ImportHeightmapLandscape(const FlatAssetEntry& entry,
                              const LandscapeParams& params,
                              const std::string& image_path,
                              std::string& error);



bool RepairLandscapeForGame(const FlatAssetEntry& entry,
                            std::string& error);



bool UpgradeLandscapeToLarge(const FlatAssetEntry& entry,
                             std::string& error);



bool SaveSculptedHeights(const FlatAssetEntry& entry, std::string& error);

bool EnsureEhfInStreamingBank(const std::string& data_dir,
                              const std::string& region,
                              std::string& error);
bool EnsureEhfInStreamingBank(const FlatAssetEntry& entry,
                              std::string& error);

}
}
