void draw_npc_template_picker() {
    const char* preview = g_new_npc.template_entity == 0
        ? "Select NPC template..." : g_new_npc.template_name.c_str();
    if (!ImGui::BeginCombo("Full model + animations", preview)) return;
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##new_npc_template_filter",
                             "Search NPC templates...",
                             g_new_npc_template_filter,
                             sizeof(g_new_npc_template_filter));
    std::string filter = g_new_npc_template_filter;
    std::transform(filter.begin(), filter.end(), filter.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    std::vector<int> visible;
    visible.reserve(g_global_entity_catalog.size());
    for (int i = 0; i < static_cast<int>(g_global_entity_catalog.size());
         ++i) {
        const auto& entity = g_global_entity_catalog[static_cast<size_t>(i)];
        if (g_global_entity_gameplay.find(entity.entity_hash) ==
            g_global_entity_gameplay.end()) continue;
        std::string text = entity.name + " " + entity.display_name;
        std::transform(text.begin(), text.end(), text.begin(),
                       [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
        if (filter.empty() || text.find(filter) != std::string::npos) {
            visible.push_back(i);
        }
    }
    ImGui::Separator();
    ImGui::BeginChild("##new_npc_templates", ImVec2(0.0f, 280.0f));
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(visible.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const int index = visible[static_cast<size_t>(row)];
            const auto& entity =
                g_global_entity_catalog[static_cast<size_t>(index)];
            const std::string& label = entity.display_name.empty()
                ? entity.name : entity.display_name;
            const bool selected = index == g_new_npc_template_index;
            if (ImGui::Selectable(label.c_str(), selected)) {
                select_new_npc_template(index);
                ImGui::CloseCurrentPopup();
            }
        }
    }
    clipper.End();
    ImGui::EndChild();
    ImGui::EndCombo();
}

void draw_static_prop_model_picker() {
    const char* preview = g_new_static_prop.model_path.empty()
        ? "Select model..." : g_new_static_prop.model_path.c_str();
    if (!ImGui::BeginCombo("Model", preview)) return;
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##new_static_prop_model_filter",
                             "Search models...",
                             g_new_static_prop_model_filter,
                             sizeof(g_new_static_prop_model_filter));
    std::string filter = g_new_static_prop_model_filter;
    std::transform(filter.begin(), filter.end(), filter.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    std::vector<int> visible;
    visible.reserve(S.all_mdl_files.size());
    for (int i = 0; i < static_cast<int>(S.all_mdl_files.size()); ++i) {
        const FlatAssetEntry& model = S.all_mdl_files[size_t(i)];
        std::string searchable = model.name + " " + model.full_path;
        std::transform(searchable.begin(), searchable.end(),
                       searchable.begin(), [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
        if (filter.empty() ||
            searchable.find(filter) != std::string::npos) {
            visible.push_back(i);
        }
    }
    ImGui::Separator();
    ImGui::BeginChild("##new_static_prop_models",
                      ImVec2(0.0f, 300.0f));
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(visible.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd;
             ++row) {
            const int index = visible[size_t(row)];
            const FlatAssetEntry& model = S.all_mdl_files[size_t(index)];
            const std::string label =
                model.name.empty() ? model.full_path : model.name;
            const bool selected = index == g_new_static_prop_model_index;
            ImGui::PushID(index);
            if (ImGui::Selectable(label.c_str(), selected)) {
                g_new_static_prop_model_index = index;
                g_new_static_prop.model_path = model.full_path;
                g_new_static_prop_error.clear();
                ImGui::CloseCurrentPopup();
            }
            if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(model.full_path.c_str());
                ImGui::EndTooltip();
            }
            ImGui::PopID();
        }
    }
    clipper.End();
    ImGui::EndChild();
    ImGui::EndCombo();
}

void draw_npc_named_record_combo(const char* label,
                                 uint32_t& selected_record,
                                 std::string& selected_name,
                                 bool faction) {
    const std::vector<Gdb::EntityGameplayOption>& options = faction
        ? g_global_entity_gameplay_options.factions
        : g_global_entity_gameplay_options.combat_profiles;
    const char* preview = selected_name.empty() ? "Inherit template"
                                                 : selected_name.c_str();
    if (ImGui::BeginCombo(label, preview)) {
        const bool inherited = selected_record == 0;
        if (ImGui::Selectable("Inherit template", inherited)) {
            selected_record = 0;
            selected_name.clear();
        }
        if (inherited) ImGui::SetItemDefaultFocus();
        ImGui::Separator();
        for (const auto& option : options) {
            const bool selected = selected_record == option.record_hash;
            if (ImGui::Selectable(option.label.c_str(), selected)) {
                selected_record = option.record_hash;
                selected_name = option.label;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

