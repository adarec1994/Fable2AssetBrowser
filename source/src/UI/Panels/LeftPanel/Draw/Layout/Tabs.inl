    apply_entity_preview_completions();
    ImGui::BeginChild("left_panel", ImVec2(0, 0), true);

    static int s_active_tab = 1;
    if (g_open_create_npc_requested) s_active_tab = 9;

    const ImVec2 tab_size(compute_tab_button_width(), 0.0f);

    auto tab_button = [&tab_size](const char* label, bool active,
                                  ImU32 text_col = 0) -> bool {
        const ImGuiStyle& st = ImGui::GetStyle();
        const ImVec4 bg     = st.Colors[active ? ImGuiCol_TabActive : ImGuiCol_Tab];
        const ImVec4 hov    = st.Colors[ImGuiCol_TabHovered];
        const ImVec4 act    = st.Colors[ImGuiCol_TabActive];
        ImGui::PushStyleColor(ImGuiCol_Button,        bg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hov);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  act);
        if (text_col) ImGui::PushStyleColor(ImGuiCol_Text, text_col);
        bool clicked = ImGui::Button(label, tab_size);
        if (text_col) ImGui::PopStyleColor();
        ImGui::PopStyleColor(3);
        return clicked;
    };

    if (tab_button("BNK List", s_active_tab == 0))   s_active_tab = 0;
    ImGui::SameLine(0, 2);
    if (tab_button("File Tree", s_active_tab == 1))  s_active_tab = 1;
    ImGui::SameLine(0, 2);
    const ImU32 kGoldLabel = IM_COL32(255, 215, 0, 255);
    if (tab_button("Levels", s_active_tab == 6, kGoldLabel)) s_active_tab = 6;
    ImGui::SameLine(0, 2);
    const ImU32 kLuaLabel = IM_COL32(80, 220, 120, 255);
    if (tab_button("Lua Scripts", s_active_tab == 7, kLuaLabel)) s_active_tab = 7;

    const ImU32 kPurpleLabel = IM_COL32(200, 130, 255, 255);
    if (tab_button("Models",   s_active_tab == 2, kPurpleLabel)) s_active_tab = 2;
    ImGui::SameLine(0, 2);
    if (tab_button("Textures", s_active_tab == 3, kPurpleLabel)) s_active_tab = 3;
    ImGui::SameLine(0, 2);
    if (tab_button("Audio",    s_active_tab == 4, kPurpleLabel)) s_active_tab = 4;
    ImGui::SameLine(0, 2);
    if (tab_button("Animations", s_active_tab == 5, kPurpleLabel)) s_active_tab = 5;

    const ImU32 kItemsLabel = IM_COL32(255, 175, 90, 255);
    if (tab_button("Items", s_active_tab == 8, kItemsLabel)) s_active_tab = 8;
    ImGui::SameLine(0, 2);
    const ImU32 kEntitiesLabel = IM_COL32(110, 220, 165, 255);
    if (tab_button("Entities", s_active_tab == 9, kEntitiesLabel)) s_active_tab = 9;
    ImGui::SameLine(0, 2);
    const ImU32 kQuestsLabel = IM_COL32(100, 200, 255, 255);
    if (tab_button("Quests", s_active_tab == 10, kQuestsLabel)) s_active_tab = 10;
    ImGui::SameLine(0, 2);
    const ImU32 kHeroLabel = IM_COL32(255, 120, 175, 255);
    if (tab_button("Hero", s_active_tab == 12, kHeroLabel)) {
        s_active_tab = 12;
        HeroDesigner::Open();
    }

    if (DetailsPanel::Active()) {
        ImGui::SameLine(0, 2);
        const ImU32 kDetailsLabel = IM_COL32(255, 230, 120, 255);
        if (tab_button("Details", s_active_tab == 11, kDetailsLabel)) {
            s_active_tab = 11;
        }
    } else if (s_active_tab == 11) {
        s_active_tab = 1;
    }

    ImGui::Separator();
