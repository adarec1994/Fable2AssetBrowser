#pragma once

#include "../MDL/ModelParser.h"

#include <cstdint>
#include <string>
#include <vector>

namespace EntityModels {

struct ResolvedModel {
    MDLInfo info;
    std::vector<MDLMeshGeom> meshes;
    std::string primary_model_path;
    std::uint32_t primary_model_hash = 0;
};

struct Attachment {
    std::uint32_t model_hash = 0;
    std::vector<std::string> bone_candidates;
};

struct ResolveOptions {
    std::uint32_t preferred_primary_hash = 0;
    std::vector<Attachment> attachments;
};




bool Resolve(const std::vector<std::uint32_t>& model_hashes,
             ResolvedModel& out,
             std::string* error = nullptr,
             const ResolveOptions* options = nullptr);

}
