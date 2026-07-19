#pragma once




#include <cstdint>
#include <string>
#include <vector>

#include "GlbImport.h"
#include "TexWriter.h"

namespace AssetImport {

bool model_extension_supported(const std::string& path);

bool load_model_scene(const std::string& path, GlbImport::Scene& scene,
                      std::string& err);

struct MaterialTextureAssignment {
    int diffuse = -1;
    int normal = -1;
    int specular = -1;
    int metallic = -1;
    int extra = -1;
};

struct Options {


    std::string dest_folder;

    std::string asset_name;
    TexWriter::Format tex_format = TexWriter::Format::Auto;
    int  max_tex_dim   = 1024;
    bool generate_mips = true;
    bool create_gdb_template = true;
    std::string entity_id;
    std::vector<MaterialTextureAssignment> material_textures;
};

struct Result {
    std::string mdl_virtual_path;
    std::vector<std::string> tex_virtual_paths;
    uint32_t meshes = 0, vertices = 0, triangles = 0;
    std::vector<std::string> notes;
    std::string entity_id;
    bool gdb_template_created = false;
};


bool import_glb(const std::string& glb_path, const Options& opt,
                Result& res, std::string& err);


bool import_image(const std::string& img_path, const Options& opt,
                  Result& res, std::string& err);

bool replace_texture(const std::string& img_path,
                     const std::string& target_bnk_path,
                     int target_file_index, const Options& opt,
                     Result& res, std::string& err);


bool import_folder(const std::string& folder, const Options& opt,
                   Result& res, std::string& err);


std::string sanitize_name(const std::string& raw);

}
