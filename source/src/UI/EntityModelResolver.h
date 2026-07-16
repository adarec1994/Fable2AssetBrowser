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




bool Resolve(const std::vector<std::uint32_t>& model_hashes,
             ResolvedModel& out,
             std::string* error = nullptr);

}
