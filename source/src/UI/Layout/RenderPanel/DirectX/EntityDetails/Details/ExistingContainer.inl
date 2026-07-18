static void draw_level_container_details(uint32_t entity_hash)
{
    auto found = g_level_entity_contents.find(entity_hash);
    if (found == g_level_entity_contents.end()) return;
    const Gdb::EntityContents& contents = found->second;

    ImGui::Separator();
    draw_container_authored_rules(contents);

    std::vector<uint32_t> staged_items;
    const bool staged =
        LevelEdit::GetChestContents(entity_hash, staged_items);
    if (staged || !contents.initial_items.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                           staged ? "Initial items (edited):"
                                  : "Initial items:");
        const size_t item_count = staged ? staged_items.size()
                                         : contents.initial_items.size();
        for (size_t i = 0; i < item_count; ++i) {
            const std::string label = staged
                ? container_catalog_label(staged_items[i])
                : container_item_label(contents.initial_items[i]);
            ImGui::BulletText("%s", label.c_str());
        }
        if (item_count == 0) ImGui::TextDisabled("  (empty)");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                           "Initial items:");
        ImGui::TextDisabled("  (none)");
    }
    draw_container_loot_table_editor(entity_hash, contents);
}
