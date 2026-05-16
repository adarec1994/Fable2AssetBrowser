#pragma once

#include <string>
#include <vector>
#include <cstdint>


struct FlatAssetEntry;

namespace Level {

struct HeightfieldHeader {
    std::string magic;        
    bool        ok = false;   
    uint32_t    version = 0;
    float       prefix_float = 0.f;   
    float       f0 = 0.f;             
    float       f1 = 0.f;             
    uint32_t    u0 = 0;               
    uint32_t    u1 = 0;               
    float       f2 = 0.f;             
    float       f3 = 0.f;             
    float       f4 = 0.f;             
    uint32_t    body_offset = 0;      
    uint32_t    body_size   = 0;      
};

struct HeightfieldFiles {
    bool                  ok = false;
    std::string           error;

    HeightfieldHeader     ehf_header;
    std::vector<uint8_t>  ehf_bytes;            
    std::vector<uint8_t>  ghf_bytes_compressed; 
    std::vector<uint8_t>  ghf_bytes_raw;        
};

struct GhfHeights {
    bool                  ok        = false;
    uint32_t              width     = 0;   
    uint32_t              height    = 0;   
    float                 tile_size = 64.f;
    float                 min_height = 0.f;
    float                 max_height = 0.f;
    std::vector<float>    heights;         
    std::string           error;
};

const FlatAssetEntry* FindHeightfieldByPath(const std::string& relative_path);

bool LoadHeightfieldFiles(const std::string& ehf_path,
                          const std::string& ghf_path,
                          const std::string& hdb_path,
                          const std::string& genv_path,
                          HeightfieldFiles&  out);

bool DecodeGhfHeights(const std::vector<uint8_t>& ghf_bytes_raw,
                      GhfHeights&                 out);

struct TerrainMesh {
    bool                  ok = false;
    uint32_t              width  = 0;
    uint32_t              height = 0;

    std::vector<float>    positions;   
    std::vector<float>    normals;     
    std::vector<float>    uvs;         
    std::vector<uint32_t> indices;     

    float                 min_height = 0.f;
    float                 max_height = 0.f;
};

bool BuildTerrainMesh(const GhfHeights& heights, TerrainMesh& out);

}  
