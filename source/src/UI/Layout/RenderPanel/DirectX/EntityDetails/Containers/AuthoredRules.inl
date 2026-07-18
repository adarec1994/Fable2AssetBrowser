static void draw_container_authored_rules(
    const Gdb::EntityContents& contents)
{
    const char* kind = "Inventory container";
    if (contents.is_dig_spot) kind = "Dig spot";
    else if (contents.has_chest_component) kind = "Chest";
    ImGui::TextColored(ImVec4(0.55f, 0.9f, 1.0f, 1.0f), "%s", kind);

    if (contents.has_chest_component &&
        contents.silver_keys_needed > 0) {
        ImGui::Text("Silver keys required: %d",
                    contents.silver_keys_needed);
    }

    if (contents.item_repopulation_record != 0) {
        ImGui::Text("Can be stolen from: %s",
                    container_yes_no_unknown(
                        contents.can_be_stolen_from));
        const char* mode = "Never";
        if (contents.can_respawn_items_repeatedly > 0) {
            mode = "Repeatedly";
        } else if (contents.can_respawn_items_once > 0) {
            mode = "Once";
        } else if (contents.can_respawn_items_repeatedly < 0 &&
                   contents.can_respawn_items_once < 0) {
            mode = "Not authored";
        }
        ImGui::Text("Loot repopulation: %s", mode);
        if (contents.chance_of_respawning >= 0.0f) {
            ImGui::Text("Authored repopulation chance: %.1f%%",
                        contents.chance_of_respawning);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "The game applies its global item-respawn multiplier "
                    "and any active context multiplier at runtime.");
            }
        }
    } else {
        ImGui::Text("Loot repopulation: Not configured");
    }

    if (contents.is_dig_spot) {
        ImGui::Text("Dog can lead to: %s",
                    contents.dog_can_lead_to ? "Yes" : "No");
        if (contents.dog_lead_radius >= 0.0f) {
            ImGui::Text("Dog search radius: %.1f",
                        contents.dog_lead_radius);
        }
        if (contents.dog_lead_priority >= 0) {
            ImGui::Text("Dog lead priority: %d",
                        contents.dog_lead_priority);
        }
    }

}

static void draw_container_potential_items(
    const Gdb::EntityContents& contents,
    size_t max_rows = 12)
{
    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                       "Potential items:");
    if (contents.potential_items.empty()) {
        ImGui::TextDisabled("  (none)");
        return;
    }

    const bool scroll = contents.potential_items.size() > max_rows;
    if (scroll) {
        ImGui::BeginChild("##potential_items_scroll",
                          ImVec2(0.0f, 240.0f), true);
    }
    const size_t count = contents.potential_items.size();
    for (size_t i = 0; i < count; ++i) {
        const auto& item = contents.potential_items[i];
        ImGui::PushID(int(i) + 0x5A00);
        ImGui::BulletText("%s", container_item_label(item).c_str());
        ImGui::PopID();
    }
    if (scroll) ImGui::EndChild();
}
