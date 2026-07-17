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
    // After a successful .glb import, also author a spawnable static-prop
    // template in globals.gdb (StaticPropAuthoring) pointing at the model.
    bool create_gdb_template = true;
    // Entity ID for the template; empty derives PROP_<asset>. Auto-derived
    // IDs get a numeric suffix on collision, explicit ones fail instead.
    std::string entity_id;
};

struct Result {
    std::string mdl_virtual_path;
    std::vector<std::string> tex_virtual_paths;
    uint32_t meshes = 0, vertices = 0, triangles = 0;
    std::vector<std::string> notes;
    // GDB template outcome (glb imports with create_gdb_template)
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
