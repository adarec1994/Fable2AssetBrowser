std::unordered_map<uint32_t, PropertyDetails>
ExtractPropertyDetails(
    const std::vector<const std::vector<uint8_t>*>& gdbs,
    const std::vector<std::pair<uint32_t, std::string>>& hash_to_name)
{
    constexpr uint32_t kBuildingSaleSignComponent = 0xBD0B8004u;
    constexpr uint32_t kBuildingToSell = 0x48FE32FAu;
    constexpr uint32_t kBuildingComponent = 0x2BB2DD22u;
    constexpr uint32_t kBuildingIncomeComponent = 0xFF54B615u;
    constexpr uint32_t kBuildingName = 0x6D0DD750u;
    constexpr uint32_t kBuildingAddress = 0x764CA4F3u;
    constexpr uint32_t kBuildingAnecdotes = 0x0F0F4579u;
    constexpr uint32_t kBuildingBenefits = 0xE9A32859u;
    constexpr uint32_t kBasicSalePrice = 0xF8926AC3u;
    constexpr uint32_t kCanRent = 0xCE8EC970u;
    constexpr uint32_t kBuildingType = 0x73909541u;
    constexpr uint32_t kType = 0x161478BDu;

    std::unordered_map<uint32_t, PropertyDetails> out;
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
        const auto local = LoadEmbeddedDict(*bytes);
        dict.insert(local.begin(), local.end());
    }
    std::unordered_map<uint32_t, std::string> entity_names;
    entity_names.reserve(hash_to_name.size() * 2 + 1);
    for (const auto& [hash, name] : hash_to_name) {
        entity_names.try_emplace(hash, name);
    }
    auto record_name = [&](uint32_t hash) {
        const auto found = dict.find(hash);
        if (found != dict.end() && !found->second.empty()) {
            return found->second;
        }
        return GdbHashName(hash, {});
    };
    auto format_float = [](uint32_t raw) {
        float number = 0.0f;
        std::memcpy(&number, &raw, sizeof(number));
        if (!std::isfinite(number)) return std::string();
        char buffer[48];
        std::snprintf(buffer, sizeof(buffer), "%.3f", number);
        std::string value = buffer;
        while (value.size() > 1 && value.back() == '0') value.pop_back();
        if (!value.empty() && value.back() == '.') value.pop_back();
        return value;
    };
    auto read_raw = [&](const std::vector<MultiGdbCursor>& sources,
                        uint32_t field_hash,
                        uint32_t& raw,
                        uint8_t& type) {
        for (const MultiGdbCursor& source : sources) {
            MultiGdbCursor owner;
            if (MultiFindInherited(record_index, source, field_hash, 0xFF,
                                   owner, raw, &type)) {
                return true;
            }
        }
        return false;
    };
    auto read_tag = [&](const std::vector<MultiGdbCursor>& sources,
                        uint32_t field_hash,
                        uint32_t& tag,
                        std::string& fallback) {
        uint8_t type = 0;
        tag = 0;
        if (!read_raw(sources, field_hash, tag, type) ||
            (type != 4 && type != 7) || tag == 0 || tag == kHashNull) {
            tag = 0;
            return false;
        }
        fallback = record_name(tag);
        return true;
    };
    auto type_name = [](uint32_t value) {
        struct Entry { uint32_t bit; const char* name; };
        static constexpr Entry entries[] = {
            {0x0001u, "Home"},
            {0x0002u, "Dock"},
            {0x0004u, "Farm"},
            {0x0008u, "Town hall"},
            {0x0010u, "Warehouse"},
            {0x0020u, "Shop"},
            {0x0040u, "Tavern"},
            {0x0080u, "Bordello"},
            {0x0100u, "Unique property"},
            {0x0200u, "Market stall"},
            {0x0400u, "Coach house"},
        };
        std::string label;
        uint32_t remaining = value;
        for (const Entry& entry : entries) {
            if ((value & entry.bit) == 0) continue;
            if (!label.empty()) label += " / ";
            label += entry.name;
            remaining &= ~entry.bit;
        }
        if (remaining != 0 || label.empty()) {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "Type 0x%08X", value);
            if (!label.empty()) label += " / ";
            label += buffer;
        }
        return label;
    };
    auto append_field = [&](PropertyDetails& details,
                            const std::vector<MultiGdbCursor>& sources,
                            const char* label,
                            uint32_t field_hash) {
        uint32_t raw = 0;
        uint8_t type = 0;
        if (!read_raw(sources, field_hash, raw, type)) return;
        PropertyField field;
        field.label = label;
        field.field_hash = field_hash;
        field.raw_value = raw;
        field.value_type = type;
        if (type == 0) {
            field.value = raw != 0 ? "Yes" : "No";
        } else if (type == 1 || type == 5) {
            field.value = std::to_string(static_cast<int32_t>(raw));
        } else if (type == 3) {
            field.value = format_float(raw);
        } else if (type == 4 || type == 6 || type == 7) {
            field.value = record_name(raw);
        }
        if (!field.value.empty()) details.fields.push_back(std::move(field));
    };

    struct FieldSpec { const char* label; uint32_t hash; };
    static constexpr FieldSpec kBuildingFields[] = {
        {"Building wealth offset", 0x078705A7u},
        {"Economic change modifier", 0x617E0339u},
        {"Economy value modifier", 0xB8E6AEADu},
        {"Furniture value modifier", 0x50A15F93u},
        {"Income multiplier", 0x91BDB719u},
        {"Selling multiplier", 0x9F5BE9E2u},
        {"Not-for-sale adjustment", 0x66705185u},
        {"Income-to-wealth multiplier", 0xAA1B2BC5u},
        {"Decoration-to-wealth multiplier", 0xAF5A30A6u},
        {"Rent morality modifier", 0xA90D8D54u},
        {"Beds", 0xA47B4195u},
        {"Initial furniture level", 0x96EB25B7u},
        {"Cost to sleep here", 0xBA25B9F0u},
        {"Can be decorated", 0xE4B79F49u},
        {"Can sell once purchased", 0x9572CC55u},
        {"Check buyer is standing inside", 0x70F96C57u},
        {"Is dungeon", 0x42533825u},
        {"Dungeon rent", 0x557421A8u},
        {"Populate", 0x0D37617Bu},
    };
    static constexpr FieldSpec kIncomeFields[] = {
        {"Days between income drops", 0xA0330F81u},
        {"Maximum income drops", 0xE78B07AAu},
    };

    for (const auto& [sign_hash, sign_name] : hash_to_name) {
        MultiGdbCursor sign;
        if (!MultiLookup(record_index, sign_hash, sign)) continue;
        MultiGdbCursor owner;
        uint32_t sign_component_hash = 0;
        if (!MultiFindInherited(record_index, sign,
                                kBuildingSaleSignComponent, 6,
                                owner, sign_component_hash) ||
            sign_component_hash == 0 || sign_component_hash == kHashNull) {
            continue;
        }
        MultiGdbCursor sign_component;
        if (!MultiLookup(record_index, sign_component_hash,
                         sign_component)) {
            continue;
        }
        uint32_t building_hash = 0;
        uint8_t building_ref_type = 0;
        if (!MultiFindInherited(record_index, sign_component,
                                kBuildingToSell, 0xFF, owner,
                                building_hash, &building_ref_type) ||
            (building_ref_type != 4 && building_ref_type != 6 &&
             building_ref_type != 7) ||
            building_hash == 0 || building_hash == kHashNull) {
            continue;
        }

        PropertyDetails details;
        details.sale_sign_entity_name = sign_name;
        details.sale_sign_component_record = sign_component_hash;
        details.building_entity_hash = building_hash;
        if (const auto found = entity_names.find(building_hash);
            found != entity_names.end()) {
            details.building_entity_name = found->second;
        } else {
            details.building_entity_name = record_name(building_hash);
        }

        MultiGdbCursor building;
        if (MultiLookup(record_index, building_hash, building)) {
            details.has_building_record = true;
            uint32_t building_component_hash = 0;
            if (MultiFindInherited(record_index, building,
                                   kBuildingComponent, 6, owner,
                                   building_component_hash) &&
                building_component_hash != 0 &&
                building_component_hash != kHashNull) {
                MultiGdbCursor building_component;
                if (MultiLookup(record_index, building_component_hash,
                                building_component)) {
                    details.building_component_record =
                        building_component_hash;
                    const std::vector<MultiGdbCursor> sources = {
                        building_component, building
                    };
                    read_tag(sources, kBuildingName,
                             details.building_name_tag,
                             details.display_name);
                    read_tag(sources, kBuildingAddress,
                             details.building_address_tag,
                             details.address);
                    read_tag(sources, kBuildingAnecdotes,
                             details.building_anecdotes_tag,
                             details.anecdotes);
                    read_tag(sources, kBuildingBenefits,
                             details.building_benefits_tag,
                             details.benefits);

                    uint32_t raw = 0;
                    uint8_t type = 0;
                    if (read_raw(sources, kBasicSalePrice, raw, type) &&
                        (type == 1 || type == 5)) {
                        details.basic_sale_price =
                            static_cast<int32_t>(raw);
                    }
                    if (read_raw(sources, kCanRent, raw, type) && type == 0) {
                        details.can_rent = raw != 0 ? 1 : 0;
                    }
                    if (read_raw(sources, kBuildingType, raw, type)) {
                        if (type == 5 || type == 1) {
                            details.building_type_value = raw;
                        } else if ((type == 6 || type == 7) && raw != 0 &&
                                   raw != kHashNull) {
                            details.building_type_record = raw;
                            MultiGdbCursor type_record;
                            uint32_t type_value = 0;
                            uint8_t type_value_type = 0;
                            if (MultiLookup(record_index, raw, type_record) &&
                                MultiFindInherited(record_index, type_record,
                                                   kType, 0xFF, owner,
                                                   type_value,
                                                   &type_value_type) &&
                                (type_value_type == 1 ||
                                 type_value_type == 5)) {
                                details.building_type_value = type_value;
                            }
                        }
                    }
                    if (details.building_type_value != 0) {
                        details.building_type_name =
                            type_name(details.building_type_value);
                    }
                    for (const FieldSpec& spec : kBuildingFields) {
                        append_field(details, sources, spec.label, spec.hash);
                    }
                }
            }

            uint32_t income_component_hash = 0;
            if (MultiFindInherited(record_index, building,
                                   kBuildingIncomeComponent, 6, owner,
                                   income_component_hash) &&
                income_component_hash != 0 &&
                income_component_hash != kHashNull) {
                MultiGdbCursor income_component;
                if (MultiLookup(record_index, income_component_hash,
                                income_component)) {
                    details.building_income_component_record =
                        income_component_hash;
                    const std::vector<MultiGdbCursor> income_sources = {
                        income_component, building
                    };
                    for (const FieldSpec& spec : kIncomeFields) {
                        append_field(details, income_sources, spec.label,
                                     spec.hash);
                    }
                }
            }
        }
        if (details.display_name.empty()) {
            details.display_name = details.building_entity_name;
        }
        out.emplace(sign_hash, std::move(details));
    }
    return out;
}
