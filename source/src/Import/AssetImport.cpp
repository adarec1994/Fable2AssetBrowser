#include "AssetImport.h"

#include "GlbImport.h"
#include "ImageLoad.h"
#include "MdlWriter.h"
#include "ObjImport.h"

#include "Entity/StaticPropAuthoring.h"
#include "MDL/ModelParser.h"
#include "textures/TexParser.h"
#include "UI/OutputLog.h"
#include "Utilities/Progress.h"
#include "Utilities/DebugLog.h"
#include "Utilities/GameBackup.h"
#include "Utilities/State.h"
#include "Utilities/Utils.h"

#include "BNKCore.cpp"
#include "ISO/IsoMount.h"
#include "ISO/IsoWriteback.h"
#include "Level/IO/BnkWriter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>


extern void tree_register_injected_file(const std::string& bnk_path,
                                        const std::string& virtual_path);

namespace AssetImport {
namespace {

bool read_mutable_file(const std::string& path,
                       std::vector<uint8_t>& bytes,
                       std::string& error) {
    if (ISO::IsoMount::is_iso_path(path)) {
        const std::string member = ISO::IsoMount::strip_iso_prefix(path);
        const ISO::MountedFile* file =
            ISO::IsoMount::instance().find(member);
        if (!file) {
            error = "Could not find " + path;
            return false;
        }
        bytes = ISO::IsoMount::instance().read_file(member);
        if (bytes.size() != file->size) {
            error = "Short read of " + path;
            return false;
        }
        return true;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "Could not read " + path;
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(input),
                 std::istreambuf_iterator<char>());
    return input.good() || input.eof();
}

bool restore_mutable_file(const std::string& path,
                          const std::vector<uint8_t>& bytes,
                          std::string& error) {
    if (ISO::IsoMount::is_iso_path(path)) {
        return ISO::Writeback::WriteMember(path, bytes, false, error);
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "Could not restore " + path;
        return false;
    }
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        error = "Short restore write to " + path;
        return false;
    }
    return true;
}

#include "AssetImport/Bank/InjectionAndValidation.inl"
#include "AssetImport/Textures/Targets.inl"
#include "AssetImport/Textures/Resolve.inl"
#include "AssetImport/Textures/Replace.inl"
#include "AssetImport/Models/SpawnTemplate.inl"

}

bool model_extension_supported(const std::string& path)
{
    const std::string ext = to_lower(
        std::filesystem::path(path).extension().string());
    return ext == ".glb" || ext == ".obj";
}

bool load_model_scene(const std::string& path, GlbImport::Scene& scene,
                      std::string& err)
{
    const std::string ext = to_lower(
        std::filesystem::path(path).extension().string());
    if (ext == ".obj") return ObjImport::load_obj(path, scene, err);
    return GlbImport::load_glb(path, scene, err);
}

#include "AssetImport/Naming/Sanitize.inl"
#include "AssetImport/Models/ImportGlb.inl"
#include "AssetImport/Textures/ImportImage.inl"
#include "AssetImport/Textures/ReplaceSelected.inl"
#include "AssetImport/Folders/Import.inl"

}
