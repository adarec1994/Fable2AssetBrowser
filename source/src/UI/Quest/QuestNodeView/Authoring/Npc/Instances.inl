std::string current_quest_level_id(std::string path) {
    std::replace(path.begin(), path.end(), '/', '\\');
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    const std::string root = "worlds\\albion\\";
    const std::size_t root_pos = lower.find(root);
    if (root_pos != std::string::npos) {
        path.erase(0, root_pos + root.size());
        lower.erase(0, root_pos + root.size());
    }
    const std::string suffix =
        "\\defaultscenario\\defaultscenario.engine_level";
    const std::size_t suffix_pos = lower.rfind(suffix);
    if (suffix_pos != std::string::npos) path.resize(suffix_pos);
    if (path.empty()) path = g_pending_terrain_label;
    return path;
}

struct PlacedNpcChoice {
    Quest::WorldReference reference;
    std::string label;
    std::string searchable;
};

std::vector<PlacedNpcChoice> current_level_npc_instances() {
    std::vector<PlacedNpcChoice> choices;
    const std::string level_path =
        g_pending_terrain_level_entry.full_path;
    const std::string level_id = current_quest_level_id(level_path);
    if (level_id.empty()) return choices;

    choices.reserve(g_level_spawn_markers.size());
    for (std::size_t marker_index = 0;
         marker_index < g_level_spawn_markers.size(); ++marker_index) {
        const LevelSpawnMarker& marker =
            g_level_spawn_markers[marker_index];
        if ((marker.kind != 3 && marker.kind != 6) ||
            marker.name.empty() ||
            (marker.entity_hash != 0 &&
             LevelEdit::EntityRemovalPending(marker.entity_hash))) {
            continue;
        }

        Quest::WorldReference reference;
        reference.level_path = level_path;
        reference.level_id = level_id;
        reference.entity_name = marker.name;
        reference.entity_hash = marker.entity_hash;
        reference.x = marker.x;
        reference.y = marker.y;
        reference.z = marker.z;
        float position_delta[3]{};
        float rotation_delta[3]{};
        if (LevelEdit::EditFor(
                0x70000000u | static_cast<uint32_t>(marker_index),
                position_delta, rotation_delta)) {
            reference.x += position_delta[0];
            reference.y += position_delta[1];
            reference.z += position_delta[2];
        }
        reference.model_hashes = marker.model_hashes;
        reference.authored_instance =
            marker.pending_addition_index >= 0 &&
            LevelEdit::AdditionIsNamedEntity(
                marker.pending_addition_index);
        if (!reference.valid()) continue;

        char coords[128]{};
        std::snprintf(coords, sizeof(coords),
                      "  |  %s  |  (%.2f, %.2f, %.2f)",
                      level_id.c_str(), reference.x, reference.y,
                      reference.z);
        PlacedNpcChoice choice;
        choice.reference = std::move(reference);
        choice.label = marker.name;
        if (!marker.creature_name.empty() &&
            marker.creature_name != marker.name) {
            choice.label += "  [" + marker.creature_name + "]";
        }
        choice.label += coords;
        choice.searchable = choice.label;
        std::transform(choice.searchable.begin(), choice.searchable.end(),
                       choice.searchable.begin(), [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
        choices.push_back(std::move(choice));
    }
    std::sort(choices.begin(), choices.end(),
              [](const PlacedNpcChoice& a, const PlacedNpcChoice& b) {
                  if (a.reference.entity_name != b.reference.entity_name) {
                      return a.reference.entity_name < b.reference.entity_name;
                  }
                  if (a.reference.x != b.reference.x) {
                      return a.reference.x < b.reference.x;
                  }
                  if (a.reference.y != b.reference.y) {
                      return a.reference.y < b.reference.y;
                  }
                  return a.reference.z < b.reference.z;
              });
    return choices;
}

bool draw_npc_instance_picker(Quest::AuthoredNode& node) {
    const std::string preview = node.entity.valid()
        ? node.entity.entity_name + "  |  " + node.entity.level_id
        : std::string("Select named entity...");
    bool changed = false;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##npc_instance", preview.c_str(),
                          ImGuiComboFlags_HeightLarge)) {
        if (ImGui::IsWindowAppearing()) g_npc_instance_filter[0] = 0;
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##npc_instance_search",
                                 "Search placed named entities...",
                                 g_npc_instance_filter,
                                 sizeof(g_npc_instance_filter));
        std::string filter = g_npc_instance_filter;
        std::transform(filter.begin(), filter.end(), filter.begin(),
                       [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
        const std::vector<PlacedNpcChoice> choices =
            current_level_npc_instances();
        ImGui::Separator();
        if (choices.empty()) {
            ImGui::TextDisabled(
                "No named NPC or static-prop instances in the loaded level.");
        } else {
            ImGui::BeginChild("##npc_instance_results",
                              ImVec2(0.0f, 260.0f), false);
            for (std::size_t index = 0; index < choices.size(); ++index) {
                const PlacedNpcChoice& choice = choices[index];
                if (!filter.empty() &&
                    choice.searchable.find(filter) == std::string::npos) {
                    continue;
                }
                ImGui::PushID(static_cast<int>(index));
                const bool selected =
                    node.entity.entity_name == choice.reference.entity_name &&
                    node.entity.level_id == choice.reference.level_id &&
                    std::abs(node.entity.x - choice.reference.x) < 0.001 &&
                    std::abs(node.entity.y - choice.reference.y) < 0.001 &&
                    std::abs(node.entity.z - choice.reference.z) < 0.001;
                if (ImGui::Selectable(choice.label.c_str(), selected)) {
                    node.entity = choice.reference;
                    CancelPendingNpcCreation();
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
        }
        ImGui::EndCombo();
    }
    return changed;
}
