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

struct AutoRule {
    bool  enabled = false;
    float h_min = -100.0f;
    float h_max = 100.0f;
    float h_fade = 2.0f;
    float slope_min = 0.0f;
    float slope_max = 90.0f;
    float slope_fade = 5.0f;
    float noise_amount = 0.0f;
    float noise_scale = 16.0f;
};

struct Layer {
    std::string tex_path;
    std::string normal_path;
    float       tiling = 8.0f;
    AutoRule    rule;
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
void SetLayerNormal(int index, const std::string& normal_path);
int  ActiveLayer();
void SetActiveLayer(int index);

bool Dirty();
void MarkSaved();



void ApplyBrush(float wx, float wz, float radius_m, float strength01,
                float falloff01, bool erase,
                float noise_amount = 0.0f, float noise_scale = 16.0f);
void EndStroke();

void SetLayerRule(int index, const AutoRule& rule);
bool ApplyAutoMaterial(std::string& err);



bool BuildComposite(std::vector<uint8_t>& rgba, int& out_w, int& out_h);
bool BuildNormalComposite(std::vector<uint8_t>& rgba,
                          int& out_w, int& out_h);

bool SaveSidecar(std::string& error);

}
