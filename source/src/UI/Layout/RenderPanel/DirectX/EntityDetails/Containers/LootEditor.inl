static void draw_dig_spot_level_selector(uint32_t current_table,
                                         bool editable,
                                         bool is_addition,
                                         uint32_t entity_hash,
                                         int addition_index)
{
    struct LevelRow {
        int level = 0;
        uint32_t table = 0;
    };
    std::vector<LevelRow> rows;
    constexpr char kPrefix[] = "MarkerDiggingSpotLevel";
    constexpr size_t kPrefixLen = sizeof(kPrefix) - 1;
    for (const auto& [hash, c] : g_level_entity_contents) {
        (void)hash;
        if (!c.potential_items_record) continue;
        if (c.entity_name.rfind(kPrefix, 0) != 0) continue;
        const int level = std::atoi(c.entity_name.c_str() + kPrefixLen);
        if (level <= 0) continue;
        bool duplicate = false;
        for (const LevelRow& row : rows) {
            if (row.level == level) { duplicate = true; break; }
        }
        if (!duplicate) rows.push_back({level, c.potential_items_record});
    }
    if (rows.empty()) return;
    std::sort(rows.begin(), rows.end(),
              [](const LevelRow& a, const LevelRow& b) {
                  return a.level < b.level;
              });

    std::string current_label = "-";
    for (const LevelRow& row : rows) {
        if (row.table == current_table) {
            current_label = "Level " + std::to_string(row.level);
            break;
        }
    }

    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                       "Dig spot level:");
    ImGui::BeginDisabled(!editable);
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::BeginCombo("##dig_spot_level", current_label.c_str())) {
        for (const LevelRow& row : rows) {
            const std::string label = "Level " + std::to_string(row.level);
            if (ImGui::Selectable(label.c_str(),
                                  row.table == current_table)) {
                if (is_addition) {
                    LevelEdit::SetAdditionLootTable(addition_index,
                                                    row.table);
                } else {
                    LevelEdit::SetContainerLootTable(entity_hash,
                                                     row.table);
                }
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
}

static void draw_container_loot_table_editor(
    uint32_t entity_hash,
    const Gdb::EntityContents& contents)
{
    uint32_t current = contents.potential_items_record;
    const bool staged =
        LevelEdit::GetContainerLootTable(entity_hash, current);
    std::string current_label = "None";
    const Gdb::EntityContents* current_source = nullptr;
    if (current != 0) {
        for (const auto& [hash, candidate] : g_level_entity_contents) {
            (void)hash;
            if (candidate.potential_items_record != current) continue;
            current_source = &candidate;
            current_label = candidate.entity_name.empty()
                ? "Authored loot table" : candidate.entity_name;
            break;
        }
        if (!current_source) current_label = "Unknown authored table";
    }

    const bool editable = LevelEdit::Enabled() && !LevelEdit::Saving();
    if (contents.is_dig_spot) {
        draw_dig_spot_level_selector(current, editable, false,
                                     entity_hash, -1);
    }
    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                       staged ? "Random loot (edited):" : "Random loot:");
    ImGui::BeginDisabled(!editable);
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::BeginCombo("##container_random_loot",
                          current_label.c_str())) {
        if (ImGui::Selectable("None", current == 0)) {
            LevelEdit::SetContainerLootTable(entity_hash, 0);
        }
        std::unordered_set<uint32_t> seen;
        for (const auto& [hash, candidate] : g_level_entity_contents) {
            (void)hash;
            if (!candidate.potential_items_record ||
                !seen.insert(candidate.potential_items_record).second) {
                continue;
            }
            std::string label = candidate.entity_name.empty()
                ? "Authored loot table" : candidate.entity_name;
            label += "##" + std::to_string(
                candidate.potential_items_record);
            if (ImGui::Selectable(
                    label.c_str(),
                    current == candidate.potential_items_record)) {
                LevelEdit::SetContainerLootTable(
                    entity_hash, candidate.potential_items_record);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    if (staged && editable) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Revert##container_loot")) {
            LevelEdit::ClearContainerLootTable(entity_hash);
        }
    }
    if (current_source) {
        draw_container_potential_items(*current_source);
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                           "Potential items:");
        ImGui::TextDisabled("  (none)");
    }
}
