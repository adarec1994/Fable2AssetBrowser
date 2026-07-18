void draw_node(const Quest::GraphNode& node) {
    const ed::NodeId id = node_id(node.id);
    ed::PushStyleColor(ed::StyleColor_NodeBg, node_color(node.kind));
    ed::PushStyleColor(ed::StyleColor_NodeBorder,
                       ImVec4(0.66f, 0.72f, 0.82f, 0.75f));
    ed::BeginNode(id);
    ImGui::PushID(node.id);

    const float content_x = ImGui::GetCursorPosX();
    draw_input_pin(node.id, content_x);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.76f, 0.82f, 0.92f, 1.0f), "%s",
                       node.badge.empty() ? Quest::NodeKindName(node.kind)
                                          : node.badge.c_str());
    draw_output_pin(node.id, content_x);

    ImGui::SetCursorPosX(content_x);
    ImGui::PushTextWrapPos(content_x + kNodeContentWidth);
    ImGui::TextUnformatted(node.title.c_str());
    if (!node.subtitle.empty()) {
        ImGui::TextDisabled("%s", node.subtitle.c_str());
    }
    ImGui::Separator();
    std::size_t shown = 0;
    for (const std::string& detail : node.details) {
        if (shown == kMaxVisibleNodeDetails) {
            ImGui::TextDisabled(
                "+ %zu more - select the node to view everything",
                node.details.size() - shown);
            break;
        }
        const bool indented = detail.rfind("   ", 0) == 0 ||
                              detail.rfind("  ", 0) == 0;
        const bool dialogue = indented &&
                              detail.find(": \"") != std::string::npos;
        if (dialogue) {
            ImGui::PushStyleColor(
                ImGuiCol_Text, ImVec4(0.94f, 0.95f, 0.98f, 1.0f));
            ImGui::TextWrapped("%s", detail.c_str());
            ImGui::PopStyleColor();
        } else if (indented) {
            ImGui::TextDisabled("%s", detail.c_str());
        } else {
            ImGui::TextWrapped("%s", detail.c_str());
        }
        ++shown;
    }
    ImGui::PopTextWrapPos();

    ImGui::SetCursorPosX(content_x);
    ImGui::Dummy(ImVec2(kNodeContentWidth, 1.0f));

    ImGui::PopID();
    ed::EndNode();
    ed::PopStyleColor(2);

    if (g_apply_layout) {
        ed::SetNodePosition(id, ImVec2(node.x, node.y));
    }
}
