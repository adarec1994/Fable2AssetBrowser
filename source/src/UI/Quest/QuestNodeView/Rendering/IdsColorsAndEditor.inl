ed::NodeId node_id(int id) {
    return ed::NodeId(static_cast<std::uintptr_t>(id));
}

ed::PinId input_pin_id(int id) {
    return ed::PinId(static_cast<std::uintptr_t>(100000 + id * 2));
}

ed::PinId output_pin_id(int id) {
    return ed::PinId(static_cast<std::uintptr_t>(100001 + id * 2));
}

ed::LinkId link_id(int id) {
    return ed::LinkId(static_cast<std::uintptr_t>(200000 + id));
}

ImVec4 node_color(Quest::NodeKind kind) {
    switch (kind) {
        case Quest::NodeKind::Quest:
            return ImVec4(0.12f, 0.27f, 0.43f, 0.98f);
        case Quest::NodeKind::Thread:
            return ImVec4(0.10f, 0.35f, 0.34f, 0.98f);
        case Quest::NodeKind::Function:
            return ImVec4(0.31f, 0.20f, 0.43f, 0.98f);
        case Quest::NodeKind::State:
            return ImVec4(0.48f, 0.27f, 0.10f, 0.98f);
        case Quest::NodeKind::Action:
            return ImVec4(0.18f, 0.38f, 0.22f, 0.98f);
    }
    return ImVec4(0.20f, 0.20f, 0.23f, 0.98f);
}

ImVec4 link_color(const Quest::GraphNode* target) {
    if (!target) return ImVec4(0.65f, 0.68f, 0.75f, 0.85f);
    ImVec4 color = node_color(target->kind);
    color.x = (color.x + 0.8f) * 0.5f;
    color.y = (color.y + 0.8f) * 0.5f;
    color.z = (color.z + 0.8f) * 0.5f;
    color.w = 0.85f;
    return color;
}

void ensure_editor() {
    if (g_editor) return;
    ed::Config config;
    config.SettingsFile = nullptr;
    config.CanvasSizeMode = ed::CanvasSizeMode::CenterOnly;
    g_editor = ed::CreateEditor(&config);
    ed::SetCurrentEditor(g_editor);
    ed::GetStyle().SourceDirection = ImVec2(1.0f, 0.0f);
    ed::GetStyle().TargetDirection = ImVec2(-1.0f, 0.0f);
    ed::SetCurrentEditor(nullptr);
}

void reset_editor() {
    if (g_editor) {
        ed::DestroyEditor(g_editor);
        g_editor = nullptr;
    }
    ensure_editor();
    g_selected_graph_node = 0;
    g_apply_layout = true;
    g_fit_requested = g_initial_view_all;
    g_focus_quest_requested = !g_initial_view_all;
}
