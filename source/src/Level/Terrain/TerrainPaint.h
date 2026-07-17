#pragma once

#include <cstdint>
#include <string>
#include <vector>






namespace TerrainPaint {



constexpr int kMaxLayers = 16;

struct Layer {
    std::string tex_path;    
    float       tiling = 8.0f;   
};


void InitForLevel(const std::string& level_key, int grid_w, int grid_h,
                  float tile_size);
void Clear();

bool Active();   
const std::vector<Layer>& Layers();
int  AddLayer(const std::string& tex_path);   
void RemoveLayer(int index);
void SetLayerTiling(int index, float metres);
int  ActiveLayer();
void SetActiveLayer(int index);

bool Dirty();
void MarkSaved();



void ApplyBrush(float wx, float wz, float radius_m, float strength01,
                float falloff01, bool erase);



bool BuildComposite(std::vector<uint8_t>& rgba, int& out_w, int& out_h);

bool SaveSidecar(std::string& error);

}
