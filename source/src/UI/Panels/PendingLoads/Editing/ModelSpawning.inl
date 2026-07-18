bool spawn_level_model_at(ID3D11Device* device,
                          const std::string& model_path,
                          const float engine_pos[3])
{
    extern int g_selected_level_mesh_idx;
    extern uint32_t g_selected_level_pick_id;
    extern uint64_t g_selected_level_hash;

    if (!device || !g_mp.has_model || !g_mp.no_tilt) return false;

    CachedPropModel cached;
    if (!load_cached_prop_model(model_path,
                                g_pending_level_model_body_bnk, cached) ||
        cached.geoms.empty()) {
        OutputLog::error("level edit: could not load model '" +
                         model_path + "' for placement");
        return false;
    }

    const int add_idx = LevelEdit::AddPlacement(model_path, engine_pos);
    if (add_idx < 0) {
        OutputLog::error("level edit: placement rejected (no level?)");
        return false;
    }

    {

        std::string leaf = model_path;
        const size_t sl = leaf.find_last_of("/\\");
        if (sl != std::string::npos) leaf = leaf.substr(sl + 1);
        std::transform(leaf.begin(), leaf.end(), leaf.begin(), ::tolower);
        const int silver_chest_keys =
            LevelEdit::SilverKeyChestRequirement(model_path);
        if (silver_chest_keys > 0) {
            std::string mp2 = model_path;
            std::transform(mp2.begin(), mp2.end(), mp2.begin(),
                           ::tolower);
            std::replace(mp2.begin(), mp2.end(), '/', '\\');
            uint32_t mh2 = 0x811C9DC5u;
            for (unsigned char c : mp2) {
                mh2 *= 0x01000193u;
                mh2 ^= uint32_t(c);
            }
            auto tit2 = g_level_prop_entity_templates.find(mh2);
            if (tit2 != g_level_prop_entity_templates.end()) {
                const auto& t2 = tit2->second;
                LevelEdit::MarkAdditionAsPropEntity(
                    add_idx, t2.template_hash, t2.comp_field_hash,
                    t2.comp_template_hash, t2.physics_file_hash,
                    t2.has_text_tags);
            }
            LevelEdit::MarkAdditionAsSilverKeyChest(
                add_idx, silver_chest_keys);
            OutputLog::info(
                "level edit: placed as a REAL silver-key chest (" +
                std::to_string(silver_chest_keys) +
                " key lock; Save bakes it into the level)");
        } else if (leaf.find("chest") == std::string::npos &&
            (leaf.find("silverkey") != std::string::npos ||
             leaf.find("silver_key") != std::string::npos)) {
            LevelEdit::MarkAdditionEntityKind(
                add_idx, LevelEdit::AdditionEntityKind::SilverKey);
            OutputLog::info(
                "level edit: placed as a REAL silver key pickup (Save "
                "bakes it into the level)");
        } else if (leaf.find("chest") != std::string::npos) {
            std::string mp2 = model_path;
            std::transform(mp2.begin(), mp2.end(), mp2.begin(),
                           ::tolower);
            std::replace(mp2.begin(), mp2.end(), '/', '\\');
            uint32_t mh2 = 0x811C9DC5u;
            for (unsigned char c : mp2) {
                mh2 *= 0x01000193u;
                mh2 ^= uint32_t(c);
            }
            auto tit2 = g_level_prop_entity_templates.find(mh2);
            if (tit2 != g_level_prop_entity_templates.end()) {
                const auto& t2 = tit2->second;
                LevelEdit::MarkAdditionAsPropEntity(
                    add_idx, t2.template_hash, t2.comp_field_hash,
                    t2.comp_template_hash, t2.physics_file_hash,
                    t2.has_text_tags);
            }
            LevelEdit::MarkAdditionEntityKind(
                add_idx, LevelEdit::AdditionEntityKind::Chest);
            OutputLog::info(
                "level edit: placed as a REAL chest entity - click it to "
                "edit its contents (Save bakes it into the level)");
        } else {

            std::string mp = model_path;
            std::transform(mp.begin(), mp.end(), mp.begin(), ::tolower);
            std::replace(mp.begin(), mp.end(), '/', '\\');
            uint32_t mh = 0x811C9DC5u;
            for (unsigned char c : mp) {
                mh *= 0x01000193u;
                mh ^= uint32_t(c);
            }
            auto tit = g_level_prop_entity_templates.find(mh);
            if (tit != g_level_prop_entity_templates.end()) {
                const auto& t = tit->second;
                LevelEdit::MarkAdditionAsPropEntity(
                    add_idx, t.template_hash, t.comp_field_hash,
                    t.comp_template_hash, t.physics_file_hash,
                    t.has_text_tags);
                OutputLog::info(
                    std::string("level edit: placed as a REAL prop entity") +
                    (t.physics_file_hash ? " with collision"
                                         : " (template has no physics "
                                           "shape)") +
                    (t.has_text_tags ? " - readable: click it to edit "
                                       "its text"
                                     : ""));
            } else {
                OutputLog::info(
                    "level edit: no object template for this model - "
                    "baked as graphics-only (no collision)");
            }
        }
    }

    Level::PropInstance inst;
    inst.hash = 0xADD0000000000000ull + (uint64_t)add_idx;
    inst.values[0] = engine_pos[0];
    inst.values[1] = engine_pos[1];
    inst.values[2] = engine_pos[2];
    inst.values[7] = 1.0f;
    inst.values[9] = inst.values[10] = inst.values[11] = 1.0f;
    inst.lev_rec_kind = 5;
    inst.pos_file_offset = (uint32_t)add_idx + 1;

    static uint32_t s_added_sel_seed = 0x40000000u;
    const uint32_t selection_id = ++s_added_sel_seed;

    std::vector<MDLMeshGeom> out;
    for (const auto& src : cached.geoms) {
        if (src.positions.empty() || src.indices.empty()) continue;
        MDLMeshGeom cg;
        init_combined_prop_geom(cg, src, model_path, 1, 0xB3, 0);
        merge_transformed_instance_into(cg, src, inst, selection_id);
        if (!cg.positions.empty() && !cg.indices.empty()) {
            out.push_back(std::move(cg));
        }
    }
    if (out.empty()) {
        OutputLog::error("level edit: model '" + model_path +
                         "' produced no geometry");
        return false;
    }

    MDLInfo dummy_info;
    MP_Build(device, out, dummy_info, g_mp, true);

    g_selected_level_pick_id = selection_id;
    g_selected_level_hash = inst.hash;
    g_selected_level_mesh_idx = -1;
    for (size_t i = g_mp.meshes.size(); i-- > 0;) {
        for (const auto& pr : g_mp.meshes[i].pick_ranges) {
            if (pr.selection_id == selection_id) {
                g_selected_level_mesh_idx = (int)i;
                break;
            }
        }
        if (g_selected_level_mesh_idx >= 0) break;
    }
    if (g_selected_level_mesh_idx < 0) {
        g_selected_level_pick_id = 0;
        g_selected_level_hash = 0;
    }

    OutputLog::success("level edit: placed '" + model_path +
                       "' at (" + std::to_string(engine_pos[0]) + ", " +
                       std::to_string(engine_pos[1]) + ", " +
                       std::to_string(engine_pos[2]) + ")");
    return true;
}
