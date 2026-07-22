void request_open_create_npc() {
    g_open_create_npc_requested = true;
}

static const char* const kLeftPanelTabLabels[] = {
    "BNK List", "File Tree", "Levels", "Lua Scripts",
    "Models", "Textures", "Audio", "Animations", "Items", "Entities",
    "Quests", "Hero"
};

static float compute_tab_button_width() {
    float w = 0.0f;
    for (const char* L : kLeftPanelTabLabels) {
        w = (std::max)(w, ImGui::CalcTextSize(L).x);
    }
    return w + ImGui::GetStyle().FramePadding.x * 2.0f;
}

float left_panel_min_width() {

    const ImGuiStyle& st = ImGui::GetStyle();
    float tab_w = compute_tab_button_width();
    constexpr float kTabGap = 2.0f;
    constexpr int kRow2Count = 4;
    float row_w = (float)kRow2Count * tab_w +
                  (float)(kRow2Count - 1) * kTabGap;
    row_w += st.WindowPadding.x * 2.0f;
    return row_w;
}
