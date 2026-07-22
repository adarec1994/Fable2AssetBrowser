        std::vector<std::pair<uint32_t, std::string>> save_hash_to_name;
        struct SavePhysicsPlacement {
            uint32_t hash = 0;
            std::string entity_name;
            float x = 0.0f, y = 0.0f, z = 0.0f;
            float qx = 0.0f, qy = 0.0f, qz = 0.0f, qw = 1.0f;
        };
        std::vector<SavePhysicsPlacement> save_physics_placements;
        {
            std::vector<uint8_t> save_bytes;
            const std::string save_path = sibling_with_ext(".save");
            if (load_text_sibling(save_path, save_bytes)) {
                std::string xml(reinterpret_cast<const char*>(save_bytes.data()),
                                save_bytes.size());
                auto tag_payload = [](const std::string& s,
                                      const char* tag,
                                      std::string& out) -> bool {
                    const std::string open = std::string("<") + tag;
                    const std::string close = std::string("</") + tag + ">";
                    size_t a = s.find(open);
                    if (a == std::string::npos) return false;
                    a = s.find('>', a);
                    if (a == std::string::npos) return false;
                    size_t b = s.find(close, a + 1);
                    if (b == std::string::npos) return false;
                    out = s.substr(a + 1, b - (a + 1));
                    return true;
                };
                auto parse_float_text = [](const std::string& s,
                                           float& out) -> bool {
                    const char* p = s.c_str();
                    char* end = nullptr;
                    float v = std::strtof(p, &end);
                    if (end == p || !std::isfinite(v)) return false;
                    out = v;
                    return true;
                };
                auto read_float_tag = [&](const std::string& s,
                                          const char* tag,
                                          float& out) -> bool {
                    std::string payload;
                    return tag_payload(s, tag, payload) &&
                           parse_float_text(payload, out);
                };
                auto read_vec3_tag = [&](const std::string& s,
                                         const char* tag,
                                         float& x,
                                         float& y,
                                         float& z) -> bool {
                    std::string payload;
                    return tag_payload(s, tag, payload) &&
                           read_float_tag(payload, "X", x) &&
                           read_float_tag(payload, "Y", y) &&
                           read_float_tag(payload, "Z", z);
                };
                auto read_quat_tag = [&](const std::string& s,
                                         const char* tag,
                                         float& x,
                                         float& y,
                                         float& z,
                                         float& w) -> bool {
                    std::string payload;
                    if (!tag_payload(s, tag, payload)) return false;
                    bool ok = read_float_tag(payload, "X", x) &&
                              read_float_tag(payload, "Y", y) &&
                              read_float_tag(payload, "Z", z);
                    float rw = 1.0f;
                    if (read_float_tag(payload, "W", rw)) w = rw;
                    return ok;
                };
                const std::string tag_open  = "<Entity name=\"";
                const std::string tag_close = "</Entity>";
                size_t pos = 0;
                while (true) {
                    size_t a = xml.find(tag_open, pos);
                    if (a == std::string::npos) break;
                    a += tag_open.size();
                    size_t name_end = xml.find('"', a);
                    if (name_end == std::string::npos) break;
                    std::string name = xml.substr(a, name_end - a);
                    size_t hash_start = xml.find("0x", name_end);
                    if (hash_start == std::string::npos) break;
                    size_t hash_end = xml.find('<', hash_start);
                    if (hash_end == std::string::npos) break;
                    std::string hex = xml.substr(hash_start + 2, hash_end - hash_start - 2);
                    size_t entity_close = xml.find(tag_close, hash_end);
                    if (entity_close == std::string::npos) break;
                    uint32_t h = 0;
                    for (char c : hex) {
                        h <<= 4;
                        if (c >= '0' && c <= '9') h |= (c - '0');
                        else if (c >= 'A' && c <= 'F') h |= (c - 'A' + 10);
                        else if (c >= 'a' && c <= 'f') h |= (c - 'a' + 10);
                    }
                    std::string entity_xml =
                        xml.substr(name_end + 1, entity_close - (name_end + 1));
                    std::string physics_xml;
                    SavePhysicsPlacement sp;
                    sp.hash = h;
                    sp.entity_name = name;
                    if (tag_payload(entity_xml, "PhysicsData", physics_xml) &&
                        read_vec3_tag(physics_xml, "Position", sp.x, sp.y, sp.z)) {
                        read_quat_tag(physics_xml, "Orientation",
                                      sp.qx, sp.qy, sp.qz, sp.qw);
                        save_physics_placements.push_back(std::move(sp));
                    }
                    save_hash_to_name.emplace_back(h, std::move(name));
                    pos = entity_close + tag_close.size();
                    if ((save_hash_to_name.size() & 0x7fu) == 0 &&
                        bail_if_cancelled("save parse"))
                    {
                        return false;
                    }
                }
                OutputLog::info("save: " + std::to_string(save_hash_to_name.size())
                                + " entity hash->name mappings");
                if (!save_physics_placements.empty()) {
                    OutputLog::info("save: " +
                                    std::to_string(save_physics_placements.size()) +
                                    " PhysicsData transform(s)");
                }
            } else {
                OutputLog::warn("no .save sibling in BNK");
            }
        }
        if (bail_if_cancelled("after save parse")) return false;

        struct SupplementalGdb {
            std::string path;
            std::vector<uint8_t> bytes;
        };
        std::vector<SupplementalGdb> supplemental_gdbs;
        {
            const char* game_gdb_paths[] = {
                "data\\Globals\\Globals.gdb",
                "data\\Globals\\SpeechAction.gdb",
                "data\\InteractiveCutscenes\\InteractiveCutscenes.gdb",
                "data\\Entity\\Entity.gdb",
                "data\\EnvironmentThemes\\EnvironmentThemes.gdb",
            };
            for (const char* game_gdb_path : game_gdb_paths) {
                std::vector<uint8_t> bytes;
                if (load_text_sibling(game_gdb_path, bytes)) {
                    supplemental_gdbs.push_back(
                        SupplementalGdb{game_gdb_path, std::move(bytes)});
                }
            }
            if (!supplemental_gdbs.empty()) {
                OutputLog::info(
                    "gdb supplemental resolver: loaded " +
                    std::to_string(supplemental_gdbs.size()) +
                    " game DB(s)");
                for (const auto& db : supplemental_gdbs) {
                    OutputLog::info("  gdb-supplement: " + db.path +
                                    " (" + std::to_string(db.bytes.size()) +
                                    " bytes)");
                }
            }
        }
        if (bail_if_cancelled("after supplemental gdb load")) return false;

        auto& level_prop_blocks = info.prop_blocks;
        std::vector<uint8_t> gdb_bytes;
        const std::string gdb_path = sibling_with_ext(".gdb");
        std::string gdb_src_bnk;
        int gdb_src_idx = -1;
        std::string gdb_src_file;
        if (load_text_sibling(gdb_path, gdb_bytes,
                              &gdb_src_bnk, &gdb_src_idx, &gdb_src_file)) {
            LevelEdit::SetGdbSource(gdb_src_bnk, gdb_src_idx, gdb_src_file);
            loader_progress_update(23, 100, "Parsing level GDB records...");
            auto info = Gdb::ParseWithSaveMap(gdb_bytes, save_hash_to_name);
            if (bail_if_cancelled("after gdb parse")) return false;
            {
                std::vector<const std::vector<uint8_t>*> contents_gdbs;
                contents_gdbs.reserve(1 + supplemental_gdbs.size());
                contents_gdbs.push_back(&gdb_bytes);
                for (const auto& db : supplemental_gdbs) {
                    contents_gdbs.push_back(&db.bytes);
                }
                loader_progress_update(23, 100, "Extracting entity contents...");
                g_level_entity_contents =
                    Gdb::ExtractEntityContents(contents_gdbs,
                                               save_hash_to_name);
                auto gameplay_entities = save_hash_to_name;
                const auto gameplay_creatures =
                    Gdb::CollectCreatureNames(contents_gdbs);
                g_level_creature_catalog = gameplay_creatures;
                gameplay_entities.reserve(gameplay_entities.size() +
                                           gameplay_creatures.size());
                for (const auto& creature : gameplay_creatures) {
                    gameplay_entities.emplace_back(creature.entity_hash,
                                                   creature.name);
                }
                loader_progress_update(24, 100, "Extracting gameplay details...");
                g_level_entity_gameplay =
                    Gdb::ExtractEntityGameplayDetails(contents_gdbs,
                                                      gameplay_entities);
                g_level_property_details =
                    Gdb::ExtractPropertyDetails(contents_gdbs,
                                                save_hash_to_name);
                g_level_transition_points = Gdb::ExtractTransitionPoints(
                    contents_gdbs, save_hash_to_name);
                if (!g_level_transition_points.empty()) {
                    OutputLog::info(
                        "gdb transitions: " +
                        std::to_string(g_level_transition_points.size()) +
                        " level exit point(s)");
                }
                if (!g_level_entity_gameplay.empty()) {
                    OutputLog::info(
                        "gdb entities: " +
                        std::to_string(g_level_entity_gameplay.size()) +
                        " creature/NPC definition(s) with gameplay data");
                }
                if (!g_level_property_details.empty()) {
                    OutputLog::info(
                        "gdb properties: " +
                        std::to_string(g_level_property_details.size()) +
                        " sale sign(s) linked to their authored buildings");
                }
                if (!g_level_entity_contents.empty()) {
                    size_t chests = 0;
                    size_t dig_spots = 0;
                    size_t with_items = 0;
                    for (const auto& kv : g_level_entity_contents) {
                        if (kv.second.has_chest_component) ++chests;
                        if (kv.second.is_dig_spot) ++dig_spots;
                        if (!kv.second.initial_items.empty()) ++with_items;
                    }
                    OutputLog::info(
                        "gdb contents: " +
                        std::to_string(g_level_entity_contents.size()) +
                        " container entit(ies) (" + std::to_string(chests) +
                        " chest(s), " + std::to_string(dig_spots) +
                        " dig spot(s), " + std::to_string(with_items) +
                        " with authored items)");
                }
                g_level_item_catalog =
                    Gdb::BuildItemCatalog(contents_gdbs);
                if (!g_level_item_catalog.empty()) {
                    OutputLog::info(
                        "gdb contents: item catalog has " +
                        std::to_string(g_level_item_catalog.size()) +
                        " inventory item template(s)");
                }
                if (g_item_details.empty()) {
                    g_item_details =
                        Gdb::BuildItemDetails(contents_gdbs);
                }
                {

                    std::vector<const std::vector<uint8_t>*> template_gdbs;
                    for (const auto& db : supplemental_gdbs) {
                        template_gdbs.push_back(&db.bytes);
                    }
                    template_gdbs.push_back(&gdb_bytes);
                    g_level_prop_entity_templates =
                        Gdb::BuildPropTemplateIndex(template_gdbs);
                    size_t with_phys = 0;
                    for (const auto& kv : g_level_prop_entity_templates) {
                        if (kv.second.physics_file_hash) ++with_phys;
                    }
                    OutputLog::info(
                        "gdb templates: " +
                        std::to_string(
                            g_level_prop_entity_templates.size()) +
                        " model(s) map to an object template (" +
                        std::to_string(with_phys) +
                        " with a physics shape) - placed models bake as "
                        "real entities");
                }
                {
                    TextBank::LoadForRoot(S.root_dir);
                    for (auto& [sign_hash, property] :
                         g_level_property_details) {
                        (void)sign_hash;
                        std::string localized;
                        if (property.building_name_tag != 0 &&
                            TextBank::Lookup(property.building_name_tag,
                                             localized) &&
                            !localized.empty()) {
                            property.display_name = std::move(localized);
                        }
                        localized.clear();
                        if (property.building_address_tag != 0 &&
                            TextBank::Lookup(property.building_address_tag,
                                             localized) &&
                            !localized.empty()) {
                            property.address = std::move(localized);
                        }
                        localized.clear();
                        if (property.building_anecdotes_tag != 0 &&
                            TextBank::Lookup(
                                property.building_anecdotes_tag,
                                localized) && !localized.empty()) {
                            property.anecdotes = std::move(localized);
                        }
                        localized.clear();
                        if (property.building_benefits_tag != 0 &&
                            TextBank::Lookup(property.building_benefits_tag,
                                             localized) &&
                            !localized.empty()) {
                            property.benefits = std::move(localized);
                        }
                    }
                    loader_progress_update(24, 100, "Extracting entity text...");
                    g_level_entity_text = Gdb::ExtractEntityTextTags(
                        contents_gdbs, save_hash_to_name);
                    if (!g_level_entity_text.empty()) {
                        OutputLog::info(
                            "gdb text: " +
                            std::to_string(g_level_entity_text.size()) +
                            " readable entit(ies) with text tags");
                    }
                }
                {



                    auto resolve_names =
                        [&](std::vector<Gdb::EntityContentsItem>& items) {
                            for (auto& it : items) {
                                if (it.name_tag_hash) {
                                    std::string nm;
                                    if (TextBank::Lookup(it.name_tag_hash,
                                                         nm)) {
                                        while (!nm.empty() &&
                                               nm.back() == '\0') {
                                            nm.pop_back();
                                        }
                                        if (!nm.empty()) {
                                            it.display_name = nm;
                                            continue;
                                        }
                                    }
                                }
                                for (const auto& d : g_item_details) {
                                    if (d.record_hash == it.record_hash &&
                                        !d.display_name.empty()) {
                                        it.display_name = d.display_name;
                                        break;
                                    }
                                }
                            }
                        };
                    for (auto& kv : g_level_entity_contents) {
                        resolve_names(kv.second.initial_items);
                        resolve_names(kv.second.potential_items);
                    }
                }
                {
                    g_level_spawn_donor = Gdb::SpawnDonorInfo{};
                    g_level_npc_donor = Gdb::NpcDonorInfo{};
                    if (!g_level_creature_catalog.empty()) {
                        OutputLog::info(
                            "gdb creatures: " +
                            std::to_string(
                                g_level_creature_catalog.size()) +
                            " creature definition(s) available for "
                            "generators");
                    }
                    Gdb::NpcDonorInfo detected_npc_donor;
                    loader_progress_update(25, 100, "Collecting spawn entities...");
                    auto spawn_ents = Gdb::CollectSpawnEntities(
                        contents_gdbs, save_hash_to_name,
                        &g_level_spawn_donor, &detected_npc_donor);
                    auto exact_models_for_entity =
                        [&](uint32_t entity_hash)
                            -> const std::vector<uint32_t>* {
                            if (entity_hash == 0) return nullptr;
                            for (const auto& creature :
                                 g_level_creature_catalog) {
                                if (creature.entity_hash == entity_hash &&
                                    !creature.model_hashes.empty()) {
                                    return &creature.model_hashes;
                                }
                            }
                            auto global =
                                g_global_entity_model_hashes.find(entity_hash);
                            return global ==
                                       g_global_entity_model_hashes.end()
                                ? nullptr
                                : &global->second;
                        };
                    for (auto& [entity_hash, spawn] : spawn_ents) {
                        if (!spawn.model_hashes.empty()) continue;
                        if (spawn.kind == 3) {
                            if (const auto* models =
                                    exact_models_for_entity(entity_hash)) {
                                spawn.model_hashes = *models;
                            }
                            continue;
                        }
                        if (spawn.kind != 1) continue;
                        std::vector<uint32_t> candidates =
                            spawn.creature_entity_candidates;
                        if (spawn.creature_entity_hash != 0 &&
                            std::find(candidates.begin(), candidates.end(),
                                      spawn.creature_entity_hash) ==
                                candidates.end()) {
                            candidates.insert(candidates.begin(),
                                              spawn.creature_entity_hash);
                        }
                        for (uint32_t candidate : candidates) {
                            const auto* models =
                                exact_models_for_entity(candidate);
                            if (models && !models->empty()) {
                                spawn.creature_entity_hash = candidate;
                                spawn.model_hashes = *models;
                                break;
                            }
                            const auto* authored =
                                Loading::FindAuthoredEntityBinding(candidate);
                            if (!authored || authored->model_hashes.empty()) {
                                continue;
                            }
                            spawn.creature_entity_hash =
                                authored->entity_hash;
                            spawn.creature_name = authored->name;
                            spawn.model_hashes =
                                authored->model_hashes;
                            break;
                        }
                    }
                    g_level_npc_donor = detected_npc_donor;
                    if (!g_level_npc_donor.valid()) {





                        g_level_npc_donor.transform_field = 0xC5A11B7Au;
                    }





#include "Level/Loading/Stages/LoadContainers.inl"
            }
            if (bail_if_cancelled("after contents extract")) return false;
#include "Level/Loading/Stages/LoadEnvironment.inl"
            if (bail_if_cancelled("after environment theme parse")) return false;
            if (!supplemental_gdbs.empty()) {
                const bool is_bwsslums_level_for_supplemental =
                    lower_slash(entry.full_path).find("bwsslums") !=
                    std::string::npos;
                size_t supplemental_model_hits = 0;
                size_t supplemental_model_augments = 0;
                size_t supplemental_parent_hits = 0;
                size_t supplemental_model_parts = 0;
                size_t supplemental_misses = 0;
                size_t supplemental_poll = 0;
                for (auto& p : info.placements) {
                    if ((++supplemental_poll & 0x7fu) == 0 &&
                        bail_if_cancelled("supplemental model resolve"))
                    {
                        return false;
                    }
                    const std::string supplemental_entity_key =
                        compact_match_key(p.entity_name);
                    const bool allow_slums_shell_augment =
                        is_bwsslums_level_for_supplemental &&
                        p.model_path_hash != 0 &&
                        (supplemental_entity_key.find("slumstreethouse") !=
                             std::string::npos ||
                         supplemental_entity_key.find("townhouse") !=
                             std::string::npos);
                    if (p.hash_a == 0 ||
                        (p.model_path_hash != 0 &&
                         !allow_slums_shell_augment))
                    {
                        continue;
                    }
                    std::vector<uint32_t> model_hashes;
                    uint32_t parent_hash = 0;
                    bool found = false;
                    std::string source_gdb;
                    uint32_t lookup_hash = 0;
                    for (const auto& db : supplemental_gdbs) {
                        if (Gdb::LookupModelPathHashes(
                                db.bytes, p.hash_a, model_hashes,
                                &parent_hash))
                        {
                            found = true;
                            source_gdb = db.path;
                            lookup_hash = p.hash_a;
                            break;
                        }
                        if (p.parent_hash != 0 &&
                            Gdb::LookupModelPathHashes(
                                db.bytes, p.parent_hash, model_hashes,
                                nullptr))
                        {
                            found = true;
                            source_gdb = db.path;
                            lookup_hash = p.parent_hash;
                            break;
                        }
                    }
                    if (found && !model_hashes.empty()) {
                        const bool augmenting_existing_hash =
                            p.model_path_hash != 0;
                        if (augmenting_existing_hash) {
                            if (p.model_path_hashes.empty()) {
                                p.model_path_hashes.push_back(
                                    p.model_path_hash);
                            }
                            for (uint32_t h : model_hashes) {
                                if (std::find(
                                        p.model_path_hashes.begin(),
                                        p.model_path_hashes.end(), h) ==
                                    p.model_path_hashes.end())
                                {
                                    p.model_path_hashes.push_back(h);
                                }
                            }
                            ++supplemental_model_augments;
                        } else {
                            p.model_path_hashes = model_hashes;
                            p.model_path_hash = model_hashes.front();
                        }





                        for (LevelSpawnMarker& marker :
                             g_level_spawn_markers) {
                            if ((marker.kind == 3 ||
                                 marker.kind == 6) &&
                                marker.entity_hash == p.hash_a) {
                                marker.model_hashes = p.model_path_hashes;
                                break;
                            }
                        }
                        ++supplemental_model_hits;
                        supplemental_model_parts +=
                            p.model_path_hashes.size();
                        if (p.parent_hash == 0 && parent_hash != 0) {
                            p.parent_hash = parent_hash;
                            ++supplemental_parent_hits;
                        }
                    } else if (!p.indexed_record) {
                        ++supplemental_misses;
                    }
                }
                if (supplemental_model_parts > supplemental_model_hits) {
                    std::vector<Gdb::Placement> extra_parts;
                    size_t extra_poll = 0;
                    for (auto& p : info.placements) {
                        if ((++extra_poll & 0x7fu) == 0 &&
                            bail_if_cancelled("supplemental part expansion"))
                        {
                            return false;
                        }
                        if (p.model_path_hashes.size() <= 1) continue;
                        const std::vector<uint32_t> hashes =
                            p.model_path_hashes;
                        p.model_path_hash = hashes.front();
                        p.model_path_hashes.clear();
                        p.model_path_hashes.push_back(hashes.front());
                        for (size_t i = 1; i < hashes.size(); ++i) {
                            Gdb::Placement part = p;
                            part.model_path_hash = hashes[i];
                            part.model_path_hashes.clear();
                            part.model_path_hashes.push_back(hashes[i]);
                            extra_parts.push_back(std::move(part));
                        }
                    }
                    info.placements.insert(
                        info.placements.end(),
                        std::make_move_iterator(extra_parts.begin()),
                        std::make_move_iterator(extra_parts.end()));
                }
                if (supplemental_model_hits > 0 ||
                    supplemental_misses > 0)
                {
                    OutputLog::info(
                        "gdb supplemental model path hashes: hit=" +
                        std::to_string(supplemental_model_hits) +
                        ", augment=" +
                        std::to_string(supplemental_model_augments) +
                        ", parts=" +
                        std::to_string(supplemental_model_parts) +
                        ", miss=" + std::to_string(supplemental_misses) +
                        ", parent-filled=" +
                        std::to_string(supplemental_parent_hits));
                }
            }

            if (!supplemental_gdbs.empty())
            {
                std::unordered_set<uint32_t> existing_hashes;
                existing_hashes.reserve(info.placements.size() * 2);
                for (const auto& p : info.placements) {
                    if (p.hash_a != 0) existing_hashes.insert(p.hash_a);
                }

                size_t supplemental_entity_hits = 0;
                size_t supplemental_entity_parts = 0;
                size_t supplemental_entity_misses = 0;
                size_t supplemental_entity_poll = 0;
                for (const auto& kv : save_hash_to_name) {
                    if ((++supplemental_entity_poll & 0x7fu) == 0 &&
                        bail_if_cancelled("supplemental entity resolve"))
                    {
                        return false;
                    }
                    if (kv.first == 0 ||
                        existing_hashes.find(kv.first) !=
                            existing_hashes.end())
                    {
                        continue;
                    }

                    Gdb::Placement supplemental;
                    bool found = false;
                    for (const auto& db : supplemental_gdbs) {
                        if (Gdb::LookupPlacement(db.bytes, kv.first,
                                                 kv.second, supplemental,
                                                 true) &&
                            !supplemental.model_path_hashes.empty())
                        {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        ++supplemental_entity_misses;
                        continue;
                    }

                    ++supplemental_entity_hits;
                    existing_hashes.insert(kv.first);
                    if (supplemental.model_path_hashes.size() > 1) {
                        const std::vector<uint32_t> hashes =
                            supplemental.model_path_hashes;
                        for (uint32_t h : hashes) {
                            Gdb::Placement part = supplemental;
                            part.model_path_hash = h;
                            part.model_path_hashes.clear();
                            part.model_path_hashes.push_back(h);
                            info.placements.push_back(std::move(part));
                            ++supplemental_entity_parts;
                        }
                    } else {
                        info.placements.push_back(std::move(supplemental));
                        ++supplemental_entity_parts;
                    }
                }
                if (supplemental_entity_hits > 0 ||
                    supplemental_entity_misses > 0)
                {
                    OutputLog::info(
                        "gdb supplemental direct entity records: hit=" +
                        std::to_string(supplemental_entity_hits) +
                        ", parts=" +
                        std::to_string(supplemental_entity_parts) +
                        ", miss=" +
                        std::to_string(supplemental_entity_misses));
                }
            }
            {
                struct SpawnCreatureBinding {
                    uint32_t creature_entity_hash = 0;
                    std::string creature_name;
                    std::vector<uint32_t> model_hashes;
                };
                std::unordered_map<uint32_t, SpawnCreatureBinding>
                    creature_by_spawn_point;
                for (const LevelSpawnMarker& marker :
                     g_level_spawn_markers) {
                    if (marker.kind != 1 || marker.model_hashes.empty()) {
                        continue;
                    }
                    SpawnCreatureBinding binding;
                    binding.creature_entity_hash =
                        marker.creature_entity_hash;
                    binding.creature_name = marker.creature_name;
                    binding.model_hashes = marker.model_hashes;
                    for (uint32_t spawn_point :
                         marker.spawn_point_entities) {
                        if (spawn_point != 0) {
                            creature_by_spawn_point.try_emplace(
                                spawn_point, binding);
                        }
                    }
                }
                for (LevelSpawnMarker& marker : g_level_spawn_markers) {
                    if (marker.kind != 2) continue;
                    auto binding =
                        creature_by_spawn_point.find(marker.entity_hash);
                    if (binding == creature_by_spawn_point.end()) continue;
                    marker.creature_entity_hash =
                        binding->second.creature_entity_hash;
                    marker.creature_name = binding->second.creature_name;
                    marker.model_hashes = binding->second.model_hashes;
                }
            }
            g_level_gdb_placements.reserve(info.placements.size());
