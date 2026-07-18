static void draw_entity_gameplay_details(
    const Gdb::EntityGameplayDetails& details,
    bool show_entity_name)
{
    ImGui::Spacing();
    ImGui::Separator();
    if (show_entity_name && !details.entity_name.empty()) {
        ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f), "%s",
                           details.entity_name.c_str());
    }
    ImGui::TextColored(ImVec4(0.55f, 0.9f, 1.0f, 1.0f),
                       "Entity gameplay");
    if (!details.faction_name.empty()) {
        ImGui::Text("Faction / allegiance: %s",
                    details.faction_name.c_str());
    }
    if (!details.combat_profile_name.empty()) {
        ImGui::TextWrapped("Combat profile: %s",
                           details.combat_profile_name.c_str());
    }

    if (!details.core_fields.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                           "Core stats:");
        for (const auto& field : details.core_fields) {
            ImGui::Text("%s: %s", field.label.c_str(),
                        field.value.c_str());
        }
    }
    if (!details.combat_fields.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                           "Combat:");
        for (const auto& field : details.combat_fields) {
            ImGui::Text("%s: %s", field.label.c_str(),
                        field.value.c_str());
        }
    }

}
