void draw_create_entity_modal() {
    ImGui::SetNextWindowSize(ImVec2(720.0f, 620.0f),
                             ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Create Entity##modal", nullptr,
                                ImGuiWindowFlags_NoResize)) return;

    int entity_kind = static_cast<int>(g_new_entity_kind);
    const char* entity_kinds[] = {"NPC", "Static prop"};
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::Combo("Entity type", &entity_kind, entity_kinds,
                     static_cast<int>(std::size(entity_kinds)))) {
        g_new_entity_kind = static_cast<NewEntityKind>(entity_kind);
        g_new_npc_error.clear();
        g_new_static_prop_error.clear();
    }

    ImGui::BeginChild("##create_entity_form", ImVec2(0.0f, -44.0f),
                      false);
    if (g_new_entity_kind == NewEntityKind::StaticProp) {
        ImGui::SeparatorText("Identity");
        ImGui::InputTextWithHint("Entity ID", "QPROP_ChildhoodSkip",
                                 &g_new_static_prop.internal_name);

        ImGui::Spacing();
        ImGui::SeparatorText("Appearance");
        draw_static_prop_model_picker();
        ImGui::Spacing();
        ImGui::TextWrapped(
            "A static prop is a lightweight named world object. It receives "
            "only a model and transform: no AI, targeting, action-use, "
            "sale-sign, readable, inventory, or physics behaviour.");
        ImGui::TextDisabled(
            "Quest Blueprint nodes reference the placed instance by name; "
            "the node does not copy the entity's component details.");
        if (!g_new_static_prop_error.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.95f, 0.42f, 0.38f, 1.0f), "%s",
                               g_new_static_prop_error.c_str());
        }
    } else {
        ImGui::SeparatorText("Identity");
        ImGui::InputTextWithHint("NPC ID", "QNPC_MyCharacter",
                                 &g_new_npc.internal_name);
        ImGui::InputText("Display name", &g_new_npc.display_name);

        ImGui::Spacing();
        ImGui::SeparatorText("Appearance and behaviour");
        draw_npc_template_picker();
        if (g_new_npc.template_entity != 0) {
        ImGui::TextDisabled(
            "The complete model, eyes, hair, rig, animations, and unlisted "
            "GDB values are inherited from this template.");
        const std::string model_parts_label = "Model parts (" +
            std::to_string(g_new_npc.model_hashes.size()) + ')';
        if (ImGui::TreeNode(model_parts_label.c_str())) {
            for (uint32_t hash : g_new_npc.model_hashes) {
                const FlatAssetEntry* model =
                    FindGlobalModelAssetByPathHash(hash);
                if (model) ImGui::BulletText("%s", model->full_path.c_str());
                else ImGui::BulletText("Unresolved model 0x%08X", hash);
            }
            ImGui::TreePop();
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Gameplay");
        draw_npc_named_record_combo("Faction / allegiance",
                                    g_new_npc.faction_record,
                                    g_new_npc.faction_name, true);
        draw_npc_named_record_combo("Combat profile",
                                    g_new_npc.combat_profile_record,
                                    g_new_npc.combat_profile_name, false);

        if (!g_new_npc.core_fields.empty()) {
            ImGui::Spacing();
            ImGui::SeparatorText("Core stats");
            for (auto& field : g_new_npc.core_fields) {
                draw_npc_value_field(field);
            }
        }
        if (!g_new_npc.combat_fields.empty()) {
            ImGui::Spacing();
            ImGui::SeparatorText("Combat");
            for (auto& field : g_new_npc.combat_fields) {
                draw_npc_value_field(field);
            }
        }
        }
        if (!g_new_npc_error.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.95f, 0.42f, 0.38f, 1.0f), "%s",
                               g_new_npc_error.c_str());
        }
    }
    ImGui::EndChild();

    const bool saving_static =
        g_new_entity_kind == NewEntityKind::StaticProp;
    const bool can_save = saving_static
        ? StaticPropAuthoring::IsValidInternalName(
              g_new_static_prop.internal_name) &&
              !g_new_static_prop.model_path.empty()
        : g_new_npc.template_entity != 0 &&
              g_new_npc.creature_component != 0;
    ImGui::BeginDisabled(!can_save);
    if (ImGui::Button("Save Entity", ImVec2(140.0f, 0.0f))) {
        uint32_t saved_entity_hash = 0;
        std::string result;
        std::string error;
        bool saved_ok = false;
        if (saving_static) {
            StaticPropAuthoring::CatalogEntry saved;
            saved_ok = StaticPropAuthoring::Save(
                S.root_dir, g_new_static_prop, saved, result, error);
            saved_entity_hash = saved.entity_hash;
        } else {
            saved_ok = NpcAuthoring::Save(
                S.root_dir, g_new_npc, saved_entity_hash, result, error);
        }
        if (saved_ok) {
            if (!saving_static) {
                TextBank::Invalidate();
                TextBank::LoadForRoot(S.root_dir);
            }
            Level::BuildGlobalEntityCatalog();
            int saved_index = -1;
            for (int i = 0;
                 i < static_cast<int>(g_global_entity_catalog.size()); ++i) {
                if (g_global_entity_catalog[static_cast<size_t>(i)]
                        .entity_hash == saved_entity_hash) {
                    saved_index = i;
                    break;
                }
            }
            if (saved_index >= 0) {
                const auto& saved =
                    g_global_entity_catalog[static_cast<size_t>(saved_index)];
                const std::string label = saved.display_name.empty()
                    ? saved.name : saved.display_name;
                ContentTabs::OpenEntity(saved_index, label);
                load_entity_preview(saved_index);
            }
            OutputLog::success("entity save: " + result);
            show_completion_box(result);
            g_new_npc = NpcAuthoring::Definition{};
            g_new_npc_template_index = -1;
            g_new_npc_error.clear();
            g_new_static_prop = StaticPropAuthoring::Definition{};
            g_new_static_prop_model_index = -1;
            g_new_static_prop_error.clear();
            ImGui::CloseCurrentPopup();
        } else {
            (saving_static ? g_new_static_prop_error : g_new_npc_error) =
                std::move(error);
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(110.0f, 0.0f))) {
        g_new_npc = NpcAuthoring::Definition{};
        g_new_npc_template_index = -1;
        g_new_npc_error.clear();
        g_new_static_prop = StaticPropAuthoring::Definition{};
        g_new_static_prop_model_index = -1;
        g_new_static_prop_error.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}
