        const Gdb::EntityContents* sel_contents = nullptr;

        if (::g_selected_level_hash != 0 &&
            ::g_selected_level_hash <= 0xFFFFFFFFull) {
            auto cit = g_level_entity_contents.find(
                uint32_t(::g_selected_level_hash));
            if (cit != g_level_entity_contents.end()) {
                sel_contents = &cit->second;
            }
        }
        if (!sel_contents && sel_gdb_entity_hash != 0) {
            auto cit = g_level_entity_contents.find(sel_gdb_entity_hash);
            if (cit != g_level_entity_contents.end()) {
                sel_contents = &cit->second;
            }
        }
        const Gdb::EntityGameplayDetails* sel_gameplay = nullptr;
        if (::g_selected_level_hash != 0 &&
            ::g_selected_level_hash <= 0xFFFFFFFFull) {
            auto git = g_level_entity_gameplay.find(
                uint32_t(::g_selected_level_hash));
            if (git != g_level_entity_gameplay.end()) {
                sel_gameplay = &git->second;
            }
        }
        const Gdb::PropertyDetails* sel_property = nullptr;
        if (::g_selected_level_hash != 0 &&
            ::g_selected_level_hash <= 0xFFFFFFFFull) {
            auto pit = g_level_property_details.find(
                uint32_t(::g_selected_level_hash));
            if (pit != g_level_property_details.end()) {
                sel_property = &pit->second;
            }
        }
        if (!sel_property && sel_gdb_entity_hash != 0) {
            auto pit = g_level_property_details.find(sel_gdb_entity_hash);
            if (pit != g_level_property_details.end()) {
                sel_property = &pit->second;
            }
        }
        if (!sel_gameplay && sel_gdb_entity_hash != 0) {
            auto git = g_level_entity_gameplay.find(sel_gdb_entity_hash);
            if (git != g_level_entity_gameplay.end()) {
                sel_gameplay = &git->second;
            }
        }
        if (!sel_gameplay) {
            uint32_t creature_hash = 0;
            const int selected_marker =
                selected_level_spawn_marker_index();
            if (selected_marker >= 0) {
                creature_hash = g_level_spawn_markers[
                    size_t(selected_marker)].creature_entity_hash;
            }
            if (creature_hash == 0 && sel_gdb_entity_hash != 0) {
                for (const auto& marker : g_level_spawn_markers) {
                    if (marker.entity_hash == sel_gdb_entity_hash) {
                        creature_hash = marker.creature_entity_hash;
                        if (creature_hash != 0) break;
                    }
                }
            }
            auto git = g_level_entity_gameplay.find(creature_hash);
            if (git != g_level_entity_gameplay.end()) {
                sel_gameplay = &git->second;
            }
        }
        constexpr uint64_t kAdditionHashBase = 0xADD0000000000000ull;
        int sel_chest_addition = -1;
        int sel_readable_addition = -1;
        if (::g_selected_level_hash >= kAdditionHashBase) {
            const int add_idx =
                int(::g_selected_level_hash - kAdditionHashBase);
            if (LevelEdit::AdditionIsChest(add_idx)) {
                sel_chest_addition = add_idx;
            }
            if (LevelEdit::AdditionIsReadable(add_idx)) {
                sel_readable_addition = add_idx;
            }
        }
        const Gdb::EntityTextTags* sel_text = nullptr;
        if (::g_selected_level_hash != 0 &&
            ::g_selected_level_hash <= 0xFFFFFFFFull) {
            auto tit = g_level_entity_text.find(
                uint32_t(::g_selected_level_hash));
            if (tit != g_level_entity_text.end()) {
                sel_text = &tit->second;
            }
        }
        if (!sel_text && sel_gdb_entity_hash != 0) {
            auto tit = g_level_entity_text.find(sel_gdb_entity_hash);
            if (tit != g_level_entity_text.end()) {
                sel_text = &tit->second;
            }
        }
        if (!sel_text && sel_found) {
            float best = 3.0f * 3.0f;
            for (const auto& kv : g_level_entity_text) {
                if (!kv.second.has_pos) continue;
                const float dx = kv.second.x - sel_pos[0];
                const float dy = kv.second.y - sel_pos[1];
                const float dz = kv.second.z - sel_pos[2];
                const float d2 = dx * dx + dy * dy + dz * dz;
                if (d2 < best) {
                    best = d2;
                    sel_text = &kv.second;
                }
            }
        }
