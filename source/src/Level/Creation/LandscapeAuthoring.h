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

bool CreateFlatLandscape(const FlatAssetEntry& entry,
                         const LandscapeParams& params,
                         std::string& error);




bool ImportHeightmapLandscape(const FlatAssetEntry& entry,
                              const LandscapeParams& params,
                              const std::string& image_path,
                              std::string& error);



bool SaveSculptedHeights(const FlatAssetEntry& entry, std::string& error);

}
}
