bool draw_item_picker(const char* id, Quest::ItemReference& selected) {
    bool changed = false;
    const char* preview = selected.display_name.empty()
        ? "Select item" : selected.display_name.c_str();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo(id, preview)) {
        static std::unordered_map<std::string, std::string> filters;
        std::string& filter = filters[id];
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##item_filter", "Search items...",
                                 &filter);
        ImGui::Separator();
        std::string needle = filter;
        std::transform(needle.begin(), needle.end(), needle.begin(),
                       [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
        ImGui::BeginChild("##item_rows", ImVec2(460.0f, 240.0f), false);
        for (const Gdb::ItemDetail& item : g_item_details) {
            if (item.is_money || item.internal_name.empty()) continue;
            const std::string display = item.display_name.empty()
                ? item.label : item.display_name;
            std::string haystack = display + ' ' + item.internal_name;
            std::transform(haystack.begin(), haystack.end(),
                           haystack.begin(), [](unsigned char c) {
                               return static_cast<char>(std::tolower(c));
                           });
            if (!needle.empty() &&
                haystack.find(needle) == std::string::npos) continue;
            const bool is_selected =
                selected.record_hash == item.record_hash;
            ImGui::PushID(static_cast<int>(item.record_hash));
            if (ImGui::Selectable(display.c_str(), is_selected)) {
                selected.record_hash = item.record_hash;
                selected.internal_name = item.internal_name;
                selected.display_name = display;
                selected.model_path = item.model_path;
                selected.source = Quest::WorldReference{};
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::EndCombo();
    }
    if (!selected.internal_name.empty()) {
        ImGui::TextDisabled("GDB item: %s", selected.internal_name.c_str());
    }
    return changed;
}
