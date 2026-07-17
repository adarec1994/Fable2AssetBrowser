#pragma once






#include <cstdint>
#include <string>
#include <vector>

namespace GlbImport {

struct Image {
    std::string name;            
    std::string mime;            
    std::vector<uint8_t> bytes;  
};

struct Material {
    std::string name;
    int base_color = -1;         
    int normal = -1;
    int metallic_rough = -1;
    int occlusion = -1;
    int emissive = -1;
    float base_color_factor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    bool double_sided = false;
    bool alpha_blend = false;
};

struct Prim {
    std::string name;            
    int material = -1;           
    std::vector<float> positions;   
    std::vector<float> normals;     
    std::vector<float> uvs;         
    std::vector<uint32_t> indices;  
};

struct Scene {
    std::vector<Prim> prims;
    std::vector<Material> materials;
    std::vector<Image> images;
};

bool load_glb(const std::string& path, Scene& out, std::string& err);

}
