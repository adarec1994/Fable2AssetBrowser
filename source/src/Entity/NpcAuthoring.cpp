#include "NpcAuthoring.h"

#include "../GDB/GdbEdit.h"
#include "../Level/Database/TextBank.h"
#include "../Utilities/GameBackup.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace NpcAuthoring {
namespace {

constexpr uint32_t kParent = 0x5F6317D5u;
constexpr uint32_t kCreatureComponent = 0xC3B90D4Fu;
constexpr uint32_t kHealthComponent = 0x26546FBCu;
constexpr uint32_t kCombatComponent = 0xF6D5AD36u;
constexpr uint32_t kFactionComponent = 0x8CE69EBCu;
constexpr uint32_t kFaction = 0x6F3C0103u;
constexpr uint32_t kCombatBalanceParams = 0x48D1A921u;
constexpr uint32_t kNameTag = 0x9555A6FCu;

bool read_file(const std::string& path, std::vector<uint8_t>& bytes,
               std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "Could not read " + path;
        return false;
    }
    input.seekg(0, std::ios::end);
    const std::streamoff length = input.tellg();
    if (length <= 0) {
        error = "The GDB is empty: " + path;
        return false;
    }
    input.seekg(0, std::ios::beg);
    bytes.resize(static_cast<std::size_t>(length));
    input.read(reinterpret_cast<char*>(bytes.data()), length);
    if (!input) {
        error = "Short read of " + path;
        return false;
    }
    return true;
}

bool write_file_atomically(const std::string& path,
                           const std::vector<uint8_t>& bytes,
                           std::string& error) {
    namespace fs = std::filesystem;
    const std::string temporary = path + ".npc_save_tmp";
    {
        std::ofstream output(temporary,
                             std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "Could not create " + temporary;
            return false;
        }
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        if (!output) {
            error = "Could not write " + temporary;
            return false;
        }
    }
    std::error_code ec;
    fs::remove(path, ec);
    if (ec) {
        fs::remove(temporary, ec);
        error = "Could not replace " + path;
        return false;
    }
    fs::rename(temporary, path, ec);
    if (ec) {
        error = "Could not install the saved GDB: " + ec.message();
        return false;
    }
    return true;
}

GdbEdit::Field field(uint32_t hash, uint8_t type, uint32_t value) {
    GdbEdit::Field out;
    out.hash = hash;
    out.type = type;
    out.value = value;
    return out;
}

bool is_health_field(uint32_t hash) {
    return hash == 0x83632C03u || hash == 0x5B42D9DBu ||
           hash == 0xFE697294u || hash == 0x78BBA6ACu;
}

std::string name_tag_for(const std::string& internal_name) {
    std::string tag = "TEXT_CHARACTER_NAME_" + internal_name;
    std::transform(tag.begin(), tag.end(), tag.begin(),
                   [](unsigned char c) {
                       return std::isalnum(c)
                           ? static_cast<char>(std::toupper(c)) : '_';
                   });
    return tag;
}

bool add_inherited_record(GdbEdit::GdbFile& gdb,
                          uint32_t parent,
                          std::vector<GdbEdit::Field> overrides,
                          uint32_t& out_hash,
                          std::string& error) {
    if (parent != 0) overrides.push_back(field(kParent, 6, parent));
    out_hash = gdb.AllocRecordHash();
    if (!gdb.AddRecord(out_hash, std::move(overrides), 1)) {
        error = "Could not create an NPC component record";
        return false;
    }
    return true;
}

}

bool IsValidInternalName(const std::string& value) {
    if (value.empty()) return false;
    if (!std::isalpha(static_cast<unsigned char>(value.front())) &&
        value.front() != '_') return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_';
    });
}

bool Save(const std::string& root_dir,
          const Definition& definition,
          uint32_t& out_entity_hash,
          std::string& result,
          std::string& error) {
    namespace fs = std::filesystem;
    result.clear();
    error.clear();
    out_entity_hash = 0;

    if (!GameBackup::RequireBackup(error)) return false;
    if (!IsValidInternalName(definition.internal_name)) {
        error = "NPC ID must start with a letter or underscore and contain "
                "only letters, numbers, and underscores.";
        return false;
    }
    if (definition.display_name.empty()) {
        error = "Enter a display name.";
        return false;
    }
    if (definition.template_entity == 0 ||
        definition.creature_component == 0) {
        error = "Select an NPC template with a valid creature component.";
        return false;
    }

    const fs::path globals = fs::path(root_dir) / "data" / "Globals" /
                             "globals.gdb";
    std::vector<uint8_t> original;
    if (!read_file(globals.string(), original, error)) {
        return false;
    }

    GdbEdit::GdbFile gdb;
    if (!gdb.Parse(original, error)) {
        error = "Could not parse globals.gdb: " + error;
        return false;
    }
    if (gdb.ResolveNamedRecord(definition.internal_name) != 0) {
        error = "An NPC definition already uses that ID.";
        return false;
    }

    const std::string name_tag = name_tag_for(definition.internal_name);
    const uint32_t name_tag_hash = TextBank::AllocTagHash(name_tag);

    std::vector<GdbEdit::Field> creature_fields;
    creature_fields.push_back(field(kNameTag, 4, name_tag_hash));
    std::vector<GdbEdit::Field> health_fields;
    for (const FieldValue& value : definition.core_fields) {
        if (value.value_type == 0xFF) continue;
        (is_health_field(value.field_hash) ? health_fields
                                           : creature_fields)
            .push_back(field(value.field_hash, value.value_type,
                             value.raw_value));
    }

    uint32_t creature_record = 0;
    if (!add_inherited_record(gdb, definition.creature_component,
                              std::move(creature_fields), creature_record,
                              error)) return false;

    uint32_t health_record = 0;
    if (!health_fields.empty() || definition.health_component != 0) {
        if (!add_inherited_record(gdb, definition.health_component,
                                  std::move(health_fields), health_record,
                                  error)) return false;
    }

    uint32_t faction_record = 0;
    if (definition.faction_record != 0 ||
        definition.faction_component != 0) {
        std::vector<GdbEdit::Field> fields;
        if (definition.faction_record != 0) {
            fields.push_back(field(kFaction, 6,
                                   definition.faction_record));
        }
        if (!add_inherited_record(gdb, definition.faction_component,
                                  std::move(fields), faction_record,
                                  error)) return false;
    }

    uint32_t profile_record = 0;
    std::vector<GdbEdit::Field> direct_combat_fields;
    if (!definition.combat_fields.empty()) {
        std::vector<GdbEdit::Field> fields;
        fields.reserve(definition.combat_fields.size());
        for (const FieldValue& value : definition.combat_fields) {
            if (value.value_type == 0xFF) continue;
            fields.push_back(field(value.field_hash, value.value_type,
                                   value.raw_value));
        }
        if (definition.combat_profile_record != 0) {
            if (!add_inherited_record(gdb,
                                      definition.combat_profile_record,
                                      std::move(fields), profile_record,
                                      error)) return false;
        } else {



            direct_combat_fields = std::move(fields);
        }
    }

    uint32_t combat_record = 0;
    const uint32_t selected_profile = profile_record != 0
        ? profile_record : definition.combat_profile_record;
    if (definition.combat_component != 0 || selected_profile != 0 ||
        !direct_combat_fields.empty()) {
        std::vector<GdbEdit::Field> fields =
            std::move(direct_combat_fields);
        if (selected_profile != 0) {
            fields.push_back(field(kCombatBalanceParams, 6,
                                   selected_profile));
        }
        if (!add_inherited_record(gdb, definition.combat_component,
                                  std::move(fields), combat_record,
                                  error)) return false;
    }

    std::vector<GdbEdit::Field> entity_fields;
    entity_fields.push_back(field(kParent, 6, definition.template_entity));
    entity_fields.push_back(field(kCreatureComponent, 6, creature_record));
    if (health_record != 0) {
        entity_fields.push_back(field(kHealthComponent, 6, health_record));
    }
    if (combat_record != 0) {
        entity_fields.push_back(field(kCombatComponent, 6, combat_record));
    }
    if (faction_record != 0) {
        entity_fields.push_back(field(kFactionComponent, 6, faction_record));
    }

    const uint32_t entity_record = gdb.AllocRecordHash();
    if (!gdb.AddRecord(entity_record, std::move(entity_fields), 0)) {
        error = "Could not create the NPC entity record.";
        return false;
    }
    gdb.AddNameMapping(definition.internal_name, entity_record);
    gdb.AddDictString(TextBank::TagHash(definition.internal_name),
                      definition.internal_name);
    gdb.AddDictString(name_tag_hash, name_tag);

    const std::vector<uint8_t> saved = gdb.Serialize();
    if (!write_file_atomically(globals.string(), saved, error)) return false;

    std::unordered_map<uint32_t, std::string> text_edits;
    text_edits.emplace(name_tag_hash, definition.display_name);
    if (!TextBank::ApplyEdits(root_dir, text_edits, error)) {
        std::string rollback_error;
        write_file_atomically(globals.string(), original, rollback_error);
        if (!rollback_error.empty()) {
            error += " (globals.gdb rollback failed: " + rollback_error + ')';
        }
        return false;
    }

    out_entity_hash = entity_record;
    std::ostringstream message;
    message << "Saved NPC " << definition.display_name
            << " to globals.gdb. The definition inherits the full model and "
               "animation setup from " << definition.template_name << '.';
    result = message.str();
    return true;
}

}
