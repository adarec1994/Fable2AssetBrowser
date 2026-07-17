#pragma once






#include <cstdint>
#include <string>
#include <vector>

namespace MdlWriter {

struct MeshInput {
    std::string name;                 
    std::vector<float> positions;     
    std::vector<float> normals;       
    std::vector<float> uvs;           
    std::vector<uint32_t> indices;    
    
    std::string tex_diffuse;
    std::string tex_specular;
    std::string tex_normal;
    std::string tex_metallic;
    std::string tex_extra;
};

struct BuiltMdl {
    std::vector<uint8_t> header;  
    std::vector<uint8_t> body;    
    uint32_t mesh_count = 0;
    uint32_t vertex_count = 0;
    uint32_t triangle_count = 0;
};


bool build(const std::vector<MeshInput>& meshes, BuiltMdl& out,
           std::string& err);

}
