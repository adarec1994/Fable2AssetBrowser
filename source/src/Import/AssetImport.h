#pragma once




#include <cstdint>
#include <string>
#include <vector>

#include "TexWriter.h"

namespace AssetImport {

struct Options {
    
    
    std::string dest_folder;
    
    std::string asset_name;
    TexWriter::Format tex_format = TexWriter::Format::Auto;
    int  max_tex_dim   = 1024;
    bool generate_mips = true;
};

struct Result {
    std::string mdl_virtual_path;                
    std::vector<std::string> tex_virtual_paths;
    uint32_t meshes = 0, vertices = 0, triangles = 0;
    std::vector<std::string> notes;              
};


bool import_glb(const std::string& glb_path, const Options& opt,
                Result& res, std::string& err);


bool import_image(const std::string& img_path, const Options& opt,
                  Result& res, std::string& err);


bool import_folder(const std::string& folder, const Options& opt,
                   Result& res, std::string& err);


std::string sanitize_name(const std::string& raw);

}
