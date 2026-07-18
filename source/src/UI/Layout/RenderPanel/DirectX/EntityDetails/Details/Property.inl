static void draw_property_details(const Gdb::PropertyDetails& details)
{
    ImGui::Spacing();
    ImGui::Separator();
    const std::string& title = !details.display_name.empty()
        ? details.display_name : details.building_entity_name;
    if (!title.empty()) {
        ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.55f, 1.0f), "%s",
                           title.c_str());
    }
    ImGui::TextColored(ImVec4(0.55f, 0.9f, 1.0f, 1.0f),
                       "Property");
    if (!details.building_entity_name.empty() &&
        details.building_entity_name != title) {
        ImGui::TextWrapped("Level instance: %s",
                           details.building_entity_name.c_str());
    }
    if (!details.building_type_name.empty()) {
        ImGui::Text("Type: %s", details.building_type_name.c_str());
    }
    if (!details.address.empty()) {
        ImGui::TextWrapped("Address: %s", details.address.c_str());
    }
    if (details.basic_sale_price >= 0) {
        ImGui::Text("Base sale price: %d gold",
                    details.basic_sale_price);
    }
    if (details.can_rent >= 0) {
        ImGui::Text("Can rent: %s", details.can_rent ? "Yes" : "No");
    }
    if (!details.has_building_record) {
        ImGui::TextDisabled(
            "The linked building data is not loaded in this scenario.");
        return;
    }
    const bool has_more =
        (!details.benefits.empty() &&
         details.benefits != "BUILDING_BENEFITS_NONE") ||
        (!details.anecdotes.empty() &&
         details.anecdotes != "BUILDING_ANECDOTES_NONE") ||
        !details.fields.empty();
    if (has_more) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.55f, 0.9f, 1.0f, 1.0f),
                           "Property Data:");
        if (!details.benefits.empty() &&
            details.benefits != "BUILDING_BENEFITS_NONE") {
            ImGui::TextWrapped("Benefits: %s", details.benefits.c_str());
        }
        if (!details.anecdotes.empty() &&
            details.anecdotes != "BUILDING_ANECDOTES_NONE") {
            ImGui::TextWrapped("Details: %s", details.anecdotes.c_str());
        }
        for (const auto& field : details.fields) {
            ImGui::TextWrapped("%s: %s", field.label.c_str(),
                               field.value.c_str());
        }
    }
}
