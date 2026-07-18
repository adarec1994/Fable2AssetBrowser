std::vector<CreatureCatalogEntry> CollectCreatureNames(
    const std::vector<const std::vector<uint8_t>*>& gdbs,
    const std::vector<std::pair<uint32_t, std::string>>& named_entities)
{
    std::vector<CreatureCatalogEntry> out;
    constexpr uint32_t kCreatureComponent = 0xC3B90D4Fu;

    std::vector<std::unique_ptr<GdbView>> owned;
    std::vector<const GdbView*> views;
    std::vector<std::unordered_map<uint32_t, std::string>> dicts;
    for (const auto* g : gdbs) {
        if (!g || g->empty()) continue;
        auto v = std::make_unique<GdbView>(*g);
        if (v->ok) {
            views.push_back(v.get());
            owned.push_back(std::move(v));
            dicts.push_back(LoadEmbeddedDict(*g));
        }
    }
    if (views.empty()) return out;
    const MultiGdbRecordIndex record_index =
        BuildMultiGdbRecordIndex(views);
    std::unordered_map<uint32_t, std::string> all_names;
    for (const auto& dict : dicts) {
        all_names.insert(dict.begin(), dict.end());
    }

    std::unordered_set<uint32_t> seen;
    std::unordered_map<uint32_t, std::string> authored_names;
    std::unordered_map<uint32_t, uint32_t> authored_name_hashes;
    for (size_t vi = 0; vi < views.size(); ++vi) {
        const GdbView* vw = views[vi];
        const auto& dict = dicts[vi];
        if (vw->bytes.size() < 0x18) continue;
        const uint32_t pairs = ReadBeU32(vw->bytes.data() + 0x10);
        const size_t meta_end =
            vw->offset_base + size_t(vw->count) * 2;
        const size_t name_base = (meta_end + 3) & ~size_t(3);
        if (name_base + size_t(pairs) * 8 > vw->bytes.size()) continue;
        for (uint32_t i = 0; i < pairs; ++i) {
            const uint32_t a =
                ReadBeU32(vw->bytes.data() + name_base + size_t(i) * 8);
            const uint32_t b = ReadBeU32(vw->bytes.data() + name_base +
                                         size_t(i) * 8 + 4);
            const std::pair<uint32_t, uint32_t> combos[2] = {{a, b},
                                                             {b, a}};
            for (const auto& c : combos) {
                auto dit = dict.find(c.first);
                if (dit == dict.end()) continue;




                if (record_index.find(c.second) != record_index.end()) {
                    authored_names.try_emplace(c.second, dit->second);
                    authored_name_hashes.try_emplace(c.second, c.first);
                }
                break;
            }
        }
    }
    for (const auto& [entity_hash, name] : named_entities) {
        if (!name.empty() &&
            record_index.find(entity_hash) != record_index.end()) {
            authored_names[entity_hash] = name;
        }
    }

    auto readable_identifier = [](std::string value) {
        constexpr const char* kPrefixes[] = {
            "SO_", "CREATURE_", "TEXT_CHARACTER_NAME_"
        };
        for (const char* prefix : kPrefixes) {
            const size_t n = std::strlen(prefix);
            if (value.size() > n && value.compare(0, n, prefix) == 0) {
                value.erase(0, n);
                break;
            }
        }
        std::replace(value.begin(), value.end(), '_', ' ');
        bool upper_next = true;
        for (char& ch : value) {
            if (ch == ' ') {
                upper_next = true;
            } else if (upper_next) {
                ch = char(std::toupper(static_cast<unsigned char>(ch)));
                upper_next = false;
            } else {
                ch = char(std::tolower(static_cast<unsigned char>(ch)));
            }
        }
        return value;
    };
    constexpr uint32_t kSoundEventPrefix = 0xABEAEBD2u;
    constexpr uint32_t kNameTag = 0x9555A6FCu;
    for (const GdbView* view : views) {
        const size_t count = std::min<size_t>(
            view->count, view->record_data_offsets.size());
        for (size_t i = 0; i < count; ++i) {
            const uint32_t entity_hash = ReadBeU32(
                view->bytes.data() + view->hash_base + i * 4);
            if (seen.find(entity_hash) != seen.end()) continue;
            MultiGdbCursor entity{view, view->record_data_offsets[i]};
            MultiGdbCursor component_owner;
            uint32_t component_hash = 0;
            if (!MultiFindInherited(record_index, entity,
                                    kCreatureComponent, 6,
                                    component_owner, component_hash) ||
                component_hash == 0 || component_hash == kHashNull) {
                continue;
            }

            std::string name;
            auto authored_name = authored_names.find(entity_hash);
            if (authored_name != authored_names.end()) {
                name = authored_name->second;
            } else {
                auto direct_name = all_names.find(entity_hash);
                if (direct_name != all_names.end()) name = direct_name->second;
            }
            MultiGdbCursor component;
            if (name.empty() &&
                MultiLookup(record_index, component_hash, component)) {
                for (uint32_t field : {kSoundEventPrefix, kNameTag}) {
                    MultiGdbCursor value_owner;
                    uint32_t value = 0;
                    uint8_t value_type = 0;
                    if (!MultiFindInherited(record_index, component, field,
                                            0xFF, value_owner, value,
                                            &value_type) ||
                        (value_type != 4 && value_type != 7)) {
                        continue;
                    }
                    auto value_name = all_names.find(value);
                    if (value_name != all_names.end() &&
                        !value_name->second.empty()) {
                        name = readable_identifier(value_name->second);
                        break;
                    }
                }
            }
            if (name.empty()) {
                char buffer[32];
                std::snprintf(buffer, sizeof(buffer),
                              "Creature 0x%08X", entity_hash);
                name = buffer;
            }

            std::vector<uint32_t> model_hashes =
                ModelsForEntityMulti(record_index, entity_hash);
            if (model_hashes.empty()) continue;

            seen.insert(entity_hash);
            CreatureCatalogEntry entry;
            entry.name = name;
            entry.display_name = name;
            entry.entity_hash = entity_hash;
            if (const auto authored_hash =
                    authored_name_hashes.find(entity_hash);
                authored_hash != authored_name_hashes.end()) {
                entry.authored_name_hash = authored_hash->second;
            }
            entry.model_hashes = std::move(model_hashes);
            MultiGdbCursor creature;
            if (MultiLookup(record_index, component_hash, creature)) {
                MultiGdbCursor tag_owner;
                uint32_t tag = 0;
                uint8_t tag_type = 0;
                if (MultiFindInherited(record_index, creature, kNameTag,
                                       0xFF, tag_owner, tag, &tag_type) &&
                    (tag_type == 4 || tag_type == 7) &&
                    tag != 0 && tag != kHashNull) {
                    entry.name_tag_hash = tag;
                }
            }
            out.push_back(std::move(entry));
        }
    }
    std::sort(out.begin(), out.end(),
              [](const CreatureCatalogEntry& a,
                 const CreatureCatalogEntry& b) {
                  return a.name < b.name;
              });
    return out;
}
