#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace NpcAuthoring {

struct FieldValue {
    std::string label;
    std::string display_value;
    uint32_t field_hash = 0;
    uint32_t raw_value = 0;
    uint8_t value_type = 0xFF;
};

struct Definition {
    std::string internal_name;
    std::string display_name;
    std::string template_name;
    uint32_t template_entity = 0;
    std::vector<uint32_t> model_hashes;

    uint32_t creature_component = 0;
    uint32_t health_component = 0;
    uint32_t combat_component = 0;
    uint32_t faction_component = 0;
    uint32_t faction_record = 0;
    std::string faction_name;
    uint32_t combat_profile_record = 0;
    std::string combat_profile_name;

    std::vector<FieldValue> core_fields;
    std::vector<FieldValue> combat_fields;
};

bool IsValidInternalName(const std::string& value);




bool Save(const std::string& root_dir,
          const Definition& definition,
          uint32_t& out_entity_hash,
          std::string& result,
          std::string& error);

}
