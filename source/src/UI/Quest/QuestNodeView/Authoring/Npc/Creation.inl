std::string quest_npc_role(const Quest::AuthoredNode& node) {
    switch (node.kind) {
        case Quest::AuthoredNodeKind::ApproachNpc: return "QuestGiver";
        case Quest::AuthoredNodeKind::Dialogue: return "Speaker";
        case Quest::AuthoredNodeKind::ReturnToNpc: return "ReturnNpc";
        default: return "Npc";
    }
}

std::string unique_quest_npc_name(const Quest::AuthoredQuest& quest,
                                  const Quest::AuthoredNode& node) {
    const std::string base = quest.quest_id + "_" + quest_npc_role(node);
    auto used = [&](const std::string& name) {
        if (g_pending_npc_creation.instance_name == name) return true;
        for (const Quest::AuthoredQuest& authored : g_authored_quests) {
            for (const Quest::AuthoredNode& authored_node : authored.nodes) {
                if (authored_node.entity.entity_name == name) return true;
            }
        }
        for (const LevelSpawnMarker& marker : g_level_spawn_markers) {
            if (marker.name == name) return true;
        }
        return false;
    };
    if (!used(base)) return base;
    for (int suffix = 2; suffix < 10000; ++suffix) {
        const std::string candidate =
            base + "_" + std::to_string(suffix);
        if (!used(candidate)) return candidate;
    }
    return base + "_New";
}

void draw_create_npc_picker(Quest::AuthoredQuest& quest,
                            Quest::AuthoredNode& node) {
    if (ImGui::Button("Place new NPC instance...", ImVec2(-1.0f, 0.0f))) {
        g_npc_creation_filter[0] = 0;
        ImGui::OpenPopup("Create quest NPC instance");
    }
    ImGui::SetNextWindowSize(ImVec2(460.0f, 520.0f),
                             ImGuiCond_Appearing);
    if (ImGui::BeginPopup("Create quest NPC instance")) {
        ImGui::TextUnformatted("Choose the NPC definition");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##npc_definition_search",
                                 "Search entities...",
                                 g_npc_creation_filter,
                                 sizeof(g_npc_creation_filter));
        std::string filter = g_npc_creation_filter;
        std::transform(filter.begin(), filter.end(), filter.begin(),
                       [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
        std::vector<std::size_t> matches;
        matches.reserve(g_global_entity_catalog.size());
        for (std::size_t index = 0;
             index < g_global_entity_catalog.size(); ++index) {
            const Gdb::CreatureCatalogEntry& entity =
                g_global_entity_catalog[index];
            if (entity.kind != Gdb::EntityCatalogKind::Creature) continue;
            std::string searchable = entity.display_name + " " + entity.name;
            std::transform(searchable.begin(), searchable.end(),
                           searchable.begin(), [](unsigned char c) {
                               return static_cast<char>(std::tolower(c));
                           });
            if (filter.empty() ||
                searchable.find(filter) != std::string::npos) {
                matches.push_back(index);
            }
        }
        ImGui::Separator();
        ImGui::BeginChild("##npc_definition_results", ImVec2(0.0f, 0.0f));
        if (matches.empty()) {
            ImGui::TextDisabled("No matching NPC definitions.");
        } else {
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(matches.size()));
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart;
                     row < clipper.DisplayEnd; ++row) {
                    const Gdb::CreatureCatalogEntry& entity =
                        g_global_entity_catalog[matches[
                            static_cast<std::size_t>(row)]];
                    const std::string label = entity.display_name.empty()
                        ? entity.name : entity.display_name;
                    ImGui::PushID(static_cast<int>(entity.entity_hash));
                    if (ImGui::Selectable(label.c_str())) {
                        g_pending_npc_creation = NpcCreationRequest{};
                        g_pending_npc_creation.instance_name =
                            unique_quest_npc_name(quest, node);
                        g_pending_npc_creation.creature_name = entity.name;
                        g_pending_npc_creation.display_name = label;
                        g_pending_npc_creation.creature_entity =
                            entity.entity_hash;
                        g_pending_npc_creation.model_hashes =
                            entity.model_hashes;
                        g_pending_npc_quest_id = quest.quest_id;
                        g_pending_npc_node = node.id;
                        g_level_reference_target =
                            LevelReferenceTarget::QuestGiver;
                        g_level_reference_node = node.id;
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::IsItemHovered() && label != entity.name) {
                        ImGui::SetTooltip("%s", entity.name.c_str());
                    }
                    ImGui::PopID();
                }
            }
            clipper.End();
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    if (g_pending_npc_node == node.id &&
        g_pending_npc_quest_id == quest.quest_id &&
        g_pending_npc_creation.creature_entity != 0) {
        ImGui::TextWrapped("Ready to place: %s",
                           g_pending_npc_creation.display_name.c_str());
        ImGui::TextDisabled("Right-click its position in an editable level.");
        if (ImGui::Button("Cancel NPC placement", ImVec2(-1.0f, 0.0f))) {
            CancelPendingNpcCreation();
        }
    }
}
