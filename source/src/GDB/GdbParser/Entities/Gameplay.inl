std::unordered_map<uint32_t, EntityGameplayDetails>
ExtractEntityGameplayDetails(
    const std::vector<const std::vector<uint8_t>*>& gdbs,
    const std::vector<std::pair<uint32_t, std::string>>& hash_to_name)
{
    std::unordered_map<uint32_t, EntityGameplayDetails> out;

    std::vector<std::unique_ptr<GdbView>> owned;
    std::vector<const GdbView*> views;
    for (const auto* bytes : gdbs) {
        if (!bytes || bytes->empty()) continue;
        auto view = std::make_unique<GdbView>(*bytes);
        if (!view->ok) continue;
        views.push_back(view.get());
        owned.push_back(std::move(view));
    }
    if (views.empty()) return out;
    const MultiGdbRecordIndex record_index =
        BuildMultiGdbRecordIndex(views);

    std::unordered_map<uint32_t, std::string> dict;
    for (const auto* bytes : gdbs) {
        if (!bytes || bytes->empty()) continue;
        auto local = LoadEmbeddedDict(*bytes);
        dict.insert(local.begin(), local.end());
    }
    auto record_name = [&](uint32_t hash) {
        auto found = dict.find(hash);
        if (found != dict.end() && !found->second.empty()) {
            return found->second;
        }
        return GdbHashName(hash, {});
    };

    struct FieldSpec {
        const char* label;
        uint32_t hash;
    };
    static constexpr FieldSpec kCoreFields[] = {
        {"Starting health", 0x83632C03u},
        {"Maximum health", 0x5B42D9DBu},
        {"Health modifier", 0xFE697294u},
        {"Maximum-health modifier", 0x78BBA6ACu},
        {"Base level", 0x820BD770u},
        {"Combat rating", 0x67687AC2u},
        {"Danger level", 0x4B251BA6u},
        {"Can be attacked", 0x77235EE5u},
    };
    static constexpr FieldSpec kCombatFields[] = {
        {"Creature damage", 0xF7BDA1F9u},
        {"Base damage", 0x103BEC7Au},
        {"Damage amount", 0x95DBBE26u},
        {"Damage multiplier", 0x53E9D10Bu},
        {"Damage taken multiplier", 0x17122D36u},
        {"Melee damage multiplier", 0x11C89DB1u},
        {"Ranged damage multiplier", 0x541BA4E6u},
        {"Attack speed multiplier", 0xCC6F1577u},
        {"Melee attack speed multiplier", 0x9355A989u},
        {"Knockdown damage threshold", 0x1637A977u},
        {"Damage reaction threshold", 0x3D24BC1Du},
        {"Combat style", 0xB1F6B9C8u},
        {"Combat behaviour group", 0x3753CB77u},
    };

    auto read_value = [&](const std::vector<MultiGdbCursor>& sources,
                          uint32_t field_hash,
                          std::string& value,
                          uint32_t* out_raw = nullptr,
                          uint8_t* out_type = nullptr) {
        for (const MultiGdbCursor& source : sources) {
            MultiGdbCursor owner;
            uint32_t raw = 0;
            uint8_t type = 0;
            if (!MultiFindInherited(record_index, source, field_hash, 0xFF,
                                    owner, raw, &type)) {
                continue;
            }
            if (type == 0) {
                value = raw != 0 ? "Yes" : "No";
                if (out_raw) *out_raw = raw;
                if (out_type) *out_type = type;
                return true;
            }
            if (type == 1 || type == 5) {
                value = std::to_string(static_cast<int32_t>(raw));
                if (out_raw) *out_raw = raw;
                if (out_type) *out_type = type;
                return true;
            }
            if (type == 3) {
                float number = 0.0f;
                std::memcpy(&number, &raw, sizeof(number));
                if (!std::isfinite(number)) continue;
                char buffer[48];
                std::snprintf(buffer, sizeof(buffer), "%.3f", number);
                value = buffer;
                while (value.size() > 1 && value.back() == '0') {
                    value.pop_back();
                }
                if (!value.empty() && value.back() == '.') value.pop_back();
                if (out_raw) *out_raw = raw;
                if (out_type) *out_type = type;
                return true;
            }
            if (type == 4 || type == 6 || type == 7) {
                value = record_name(raw);
                if (value.empty()) {
                    char buffer[16];
                    std::snprintf(buffer, sizeof(buffer), "0x%08X", raw);
                    value = buffer;
                }
                if (out_raw) *out_raw = raw;
                if (out_type) *out_type = type;
                return true;
            }
        }
        return false;
    };

    auto read_reference = [&](const std::vector<MultiGdbCursor>& sources,
                              uint32_t field_hash,
                              uint32_t& record_hash,
                              std::string& name) {
        for (const MultiGdbCursor& source : sources) {
            MultiGdbCursor owner;
            uint8_t type = 0;
            uint32_t raw = 0;
            if (!MultiFindInherited(record_index, source, field_hash, 0xFF,
                                    owner, raw, &type) ||
                (type != 4 && type != 6 && type != 7) || raw == 0 ||
                raw == kHashNull) {
                continue;
            }
            record_hash = raw;
            name = record_name(raw);
            return true;
        }
        return false;
    };

    for (const auto& [entity_hash, entity_name] : hash_to_name) {
        MultiGdbCursor entity;
        if (!MultiLookup(record_index, entity_hash, entity)) continue;

        EntityGameplayDetails details;
        details.entity_name = entity_name;
        MultiGdbCursor owner;

        auto find_component = [&](uint32_t field_hash,
                                  uint32_t& component_hash,
                                  MultiGdbCursor& component) {
            component_hash = 0;
            if (!MultiFindInherited(record_index, entity, field_hash, 6,
                                    owner, component_hash) ||
                component_hash == 0 || component_hash == kHashNull) {
                return false;
            }
            return MultiLookup(record_index, component_hash, component);
        };

        MultiGdbCursor creature, health, combat, faction, profile;
        const bool has_creature = find_component(
            kHashCreatureComponent, details.creature_component_record,
            creature);
        const bool has_health = find_component(
            kHashHealthComponent, details.health_component_record, health);
        const bool has_combat = find_component(
            kHashCombatComponent, details.combat_component_record, combat);
        const bool has_faction = find_component(
            kHashFactionComponent, details.faction_component_record,
            faction);
        if (!has_creature && !has_health && !has_combat && !has_faction) {
            continue;
        }

        std::vector<MultiGdbCursor> core_sources;
        if (has_health) core_sources.push_back(health);
        if (has_creature) core_sources.push_back(creature);
        if (has_combat) core_sources.push_back(combat);
        core_sources.push_back(entity);

        {
            std::vector<MultiGdbCursor> profile_sources;
            if (has_combat) profile_sources.push_back(combat);
            if (has_creature) profile_sources.push_back(creature);
            profile_sources.push_back(entity);
            if (read_reference(profile_sources, kHashCombatBalanceParams,
                               details.combat_profile_record,
                               details.combat_profile_name)) {
                MultiLookup(record_index, details.combat_profile_record,
                            profile);
            }
            if (details.combat_profile_name.empty() && has_combat) {
                std::string table_name;
                if (read_value({combat}, kHashCombatBalanceTable,
                               table_name)) {
                    details.combat_profile_name = std::move(table_name);
                }
            }
        }

        {
            std::vector<MultiGdbCursor> faction_sources;
            if (has_faction) faction_sources.push_back(faction);
            if (has_creature) faction_sources.push_back(creature);
            if (has_combat) faction_sources.push_back(combat);
            faction_sources.push_back(entity);
            read_reference(faction_sources, kHashFaction,
                           details.faction_record, details.faction_name);
        }

        for (const FieldSpec& spec : kCoreFields) {
            EntityGameplayField field;
            field.label = spec.label;
            field.field_hash = spec.hash;
            if (read_value(core_sources, spec.hash, field.value,
                           &field.raw_value, &field.value_type)) {
                details.core_fields.push_back(std::move(field));
            }
        }

        std::vector<MultiGdbCursor> combat_sources;
        if (profile.view != nullptr) combat_sources.push_back(profile);
        if (has_combat) combat_sources.push_back(combat);
        if (has_creature) combat_sources.push_back(creature);
        combat_sources.push_back(entity);
        for (const FieldSpec& spec : kCombatFields) {
            EntityGameplayField field;
            field.label = spec.label;
            field.field_hash = spec.hash;
            if (read_value(combat_sources, spec.hash, field.value,
                           &field.raw_value, &field.value_type)) {
                details.combat_fields.push_back(std::move(field));
            }
        }

        out.emplace(entity_hash, std::move(details));
    }
    return out;
}
