#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace StaticPropAuthoring {

struct Definition {
    std::string internal_name;
    std::string model_path;
};

struct CatalogEntry {
    std::string internal_name;
    std::string model_path;
    uint32_t entity_hash = 0;
    uint32_t model_path_hash = 0;
    uint32_t transform_component_field = 0;
    uint32_t transform_component_template = 0;
    uint32_t position_template = 0;
    uint32_t rotation_template = 0;
};

bool IsValidInternalName(const std::string& value);

bool Save(const std::string& root_dir,
          const Definition& definition,
          CatalogEntry& saved,
          std::string& result,
          std::string& error);

bool LoadCatalog(const std::string& root_dir,
                 std::vector<CatalogEntry>& entries,
                 std::string& error);

}
