#pragma once

#include <cstdint>
#include <string>
#include <vector>

#ifdef _WIN32
struct ID3D11Device;
struct ID3D11Buffer;
#endif

namespace TerrainEdit {

struct State {
    bool                  loaded = false;
    bool                  dirty  = false;

    int                   width  = 0;
    int                   height = 0;
    float                 tile_size = 1.f;
    float                 center_x = 0.f;
    float                 center_z = 0.f;

    std::vector<float>    heights_original;
    std::vector<float>    heights_current;

    std::vector<float>    positions;

    std::vector<uint8_t>  ghf_payload_original;

    std::string           ghf_bnk_path;
    int                   ghf_file_index = -1;
    std::string           ghf_full_path;

    uint64_t              ghf_bnk_entry_offset = 0;
    uint32_t              ghf_bnk_entry_on_disk_size = 0;
    bool                  ghf_bnk_entry_is_compressed = false;

    float                 min_x = 0.f, max_x = 0.f;
    float                 min_z = 0.f, max_z = 0.f;
};

void Init(int width, int height, float tile_size,
          float center_x, float center_z,
          std::vector<float> heights,
          std::vector<float> positions,
          std::vector<uint8_t> ghf_payload,
          std::string ghf_bnk_path, int ghf_file_index,
          std::string ghf_full_path,
          uint64_t   ghf_bnk_entry_offset       = 0,
          uint32_t   ghf_bnk_entry_on_disk_size = 0,
          bool       ghf_bnk_entry_is_compressed= false);

const State& Get();

bool IsLoaded();

bool IsDirty();

void RaiseAll(float delta);
void LowerAll(float delta);
void SmoothAll();
void FlattenAll(float target_height);
void Reset();

enum class BrushTool : int {
    None    = 0,
    Raise   = 1,
    Lower   = 2,
    Smooth  = 3,
    Flatten = 4,
};

void ApplyBrush(BrushTool tool,
                float wx, float wz,
                float radius, float strength,
                float target_h = 0.f);

float SampleHeightAtWorldXZ(float wx, float wz);

bool Raycast(float ox, float oy, float oz,
             float dx, float dy, float dz,
             float& out_hit_x, float& out_hit_y, float& out_hit_z);

#ifdef _WIN32
void ApplyToGpu(ID3D11Device* device, void* mesh);
#endif

bool Save(std::string& out_path_or_error);

void Clear();

}
