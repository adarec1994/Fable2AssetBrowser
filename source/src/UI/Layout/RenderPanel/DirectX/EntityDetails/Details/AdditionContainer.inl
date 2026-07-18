static void draw_addition_container_details(int addition_index)
{
    const bool editable = LevelEdit::Enabled() && !LevelEdit::Saving();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f), "%s",
                       LevelEdit::AdditionIsDigSpot(addition_index)
                           ? "New dig spot (unsaved)"
                           : "New container (unsaved)");
    std::vector<uint32_t> items;
    LevelEdit::GetAdditionChestItems(addition_index, items);
    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                       "Initial items:");
    int remove_index = -1;
    for (size_t i = 0; i < items.size(); ++i) {
        ImGui::PushID(int(i) + 0x7300);
        if (editable) {
            if (ImGui::SmallButton("x")) remove_index = int(i);
            ImGui::SameLine();
        } else {
            ImGui::Bullet();
            ImGui::SameLine();
        }
        ImGui::TextUnformatted(container_catalog_label(items[i]).c_str());
        ImGui::PopID();
    }
    if (items.empty()) ImGui::TextDisabled("  (empty)");
    if (remove_index >= 0 && size_t(remove_index) < items.size()) {
        items.erase(items.begin() + remove_index);
        LevelEdit::SetAdditionChestItems(addition_index, items);
    }

    static int s_addition_picker = -1;
    static char s_addition_filter[64] = {};
    if (editable && ImGui::SmallButton("+ Add item")) {
        s_addition_picker = addition_index;
        s_addition_filter[0] = 0;
        ImGui::OpenPopup("##marker_container_item_picker");
    }
    if (ImGui::BeginPopup("##marker_container_item_picker")) {
        if (s_addition_picker != addition_index) {
            ImGui::CloseCurrentPopup();
        } else {
            ImGui::SetNextItemWidth(260.0f);
            ImGui::InputTextWithHint("##marker_item_filter",
                                     "Search items...",
                                     s_addition_filter,
                                     sizeof(s_addition_filter));
            std::string filter = s_addition_filter;
            std::transform(filter.begin(), filter.end(), filter.begin(),
                           ::tolower);
            ImGui::BeginChild("##marker_item_rows",
                              ImVec2(300.0f, 260.0f), true);
            for (const auto& item : g_item_details) {
                const std::string label = item.display_name.empty()
                    ? item.label : item.display_name;
                std::string low = label;
                std::transform(low.begin(), low.end(), low.begin(),
                               ::tolower);
                if (!filter.empty() &&
                    low.find(filter) == std::string::npos) {
                    continue;
                }
                ImGui::PushID(int(item.record_hash));
                if (ImGui::Selectable(label.c_str())) {
                    items.push_back(item.record_hash);
                    LevelEdit::SetAdditionChestItems(addition_index, items);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
        }
        ImGui::EndPopup();
    }

    const uint32_t current =
        LevelEdit::GetAdditionLootTable(addition_index);
    if (LevelEdit::AdditionIsDigSpot(addition_index)) {
        draw_dig_spot_level_selector(current, editable, true, 0,
                                     addition_index);
    }
    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                       "Random loot:");
    std::string current_label = "None";
    for (const auto& [hash, contents] : g_level_entity_contents) {
        (void)hash;
        if (contents.potential_items_record == current && current != 0) {
            current_label = contents.entity_name.empty()
                ? "Authored loot table" : contents.entity_name;
            break;
        }
    }
    ImGui::BeginDisabled(!editable);
    if (ImGui::BeginCombo("##marker_random_loot", current_label.c_str())) {
        if (ImGui::Selectable("None", current == 0)) {
            LevelEdit::SetAdditionLootTable(addition_index, 0);
        }
        std::unordered_set<uint32_t> seen_tables;
        for (const auto& [hash, contents] : g_level_entity_contents) {
            (void)hash;
            if (!contents.potential_items_record ||
                !seen_tables.insert(contents.potential_items_record).second) {
                continue;
            }
            const std::string label = contents.entity_name.empty()
                ? "Authored loot table" : contents.entity_name;
            if (ImGui::Selectable(
                    label.c_str(), current == contents.potential_items_record)) {
                LevelEdit::SetAdditionLootTable(
                    addition_index, contents.potential_items_record);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("authored into the level GDB on Save");
}
