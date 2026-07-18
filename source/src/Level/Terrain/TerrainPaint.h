#pragma once

#include <cstdint>
#include <string>
#include <vector>

#ifdef _WIN32
struct ID3D11Device;
struct ID3D11ShaderResourceView;
#endif





namespace TerrainPaint {



constexpr int kMaxLayers = 16;

struct Layer {
    std::string tex_path;
    std::string normal_path;
    float       tiling = 8.0f;   
};

#ifdef _WIN32
struct RenderResources {
    ID3D11ShaderResourceView* diffuse[kMaxLayers] = {};
    ID3D11ShaderResourceView* normal[kMaxLayers] = {};
    ID3D11ShaderResourceView* weights = nullptr;
    float tile_scale[kMaxLayers] = {};
    uint32_t normal_mask = 0;
    uint32_t generation = 0;
    int layer_count = 0;
    int weight_w = 0;
    int weight_h = 0;
    bool ok = false;
};

bool SyncRenderResources(ID3D11Device* device);
const RenderResources& GetRenderResources();
#endif


void InitForLevel(const std::string& level_key, int grid_w, int grid_h,
                  float tile_size);
void Clear();

bool Active();   
const std::vector<Layer>& Layers();
int  AddLayer(const std::string& tex_path,
              const std::string& normal_path = {});
void RemoveLayer(int index);
void SetLayerTiling(int index, float metres);
int  ActiveLayer();
void SetActiveLayer(int index);

bool Dirty();
void MarkSaved();



void ApplyBrush(float wx, float wz, float radius_m, float strength01,
                float falloff01, bool erase);
void EndStroke();



bool BuildComposite(std::vector<uint8_t>& rgba, int& out_w, int& out_h);
bool BuildNormalComposite(std::vector<uint8_t>& rgba,
                          int& out_w, int& out_h);

bool SaveSidecar(std::string& error);

}
