void DrawNodeView() {
    if (BlueprintUI::IsActive()) {
        BlueprintUI::Draw();
        return;
    }
    std::shared_ptr<const Quest::Graph> graph;
    std::uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(g_graph_mutex);
        graph = g_graph;
        generation = g_graph_generation;
    }

    if (!graph) {
        ImGui::TextDisabled("Building quest graph...");
        return;
    }
    if (graph->nodes.empty()) {
        ImGui::TextDisabled("No quest threads or functions were found in this script.");
        return;
    }

    if (generation != g_visible_generation) {
        g_visible_generation = generation;
        reset_editor();
    } else {
        ensure_editor();
    }

    const Quest::GraphNode* selected_node =
        find_graph_node(*graph, g_selected_graph_node);
    const bool show_inspector = selected_node != nullptr;
    if (show_inspector) {
        constexpr float inspector_width = 360.0f;
        const float inspector_height =
            ImGui::GetContentRegionAvail().y * 0.5f;
        ImGui::BeginChild("##read_only_node_inspector",
                          ImVec2(inspector_width, inspector_height), true);
        DrawSelectedReadOnlyNodeInspector(*graph, *selected_node);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##read_only_node_canvas", ImVec2(0.0f, 0.0f),
                          false);
    }

    ed::SetCurrentEditor(g_editor);
    ed::Begin("quest_node_canvas", ImGui::GetContentRegionAvail());

    for (const Quest::GraphNode& node : graph->nodes) {
        draw_node(node);
    }

    struct LinkLabel {
        const Quest::GraphNode* source = nullptr;
        const Quest::GraphNode* target = nullptr;
        std::string text;
        ImVec4 color;
    };
    std::vector<LinkLabel> link_labels;
    for (const Quest::GraphLink& link : graph->links) {
        const Quest::GraphNode* source = nullptr;
        const Quest::GraphNode* target = nullptr;
        for (const Quest::GraphNode& node : graph->nodes) {
            if (node.id == link.from_node) source = &node;
            if (node.id == link.to_node) {
                target = &node;
            }
        }
        ImVec4 color = link_color(target);
        if (link.label == "Yes") color = ImVec4(0.38f, 0.86f, 0.48f, 0.95f);
        if (link.label == "No") color = ImVec4(0.95f, 0.38f, 0.36f, 0.95f);
        if (link.label == "any order") {
            color = ImVec4(0.93f, 0.72f, 0.30f, 0.95f);
        }
        if (link.inferred) color = ImVec4(0.55f, 0.58f, 0.64f, 0.65f);
        ed::Link(link_id(link.id), output_pin_id(link.from_node),
                 input_pin_id(link.to_node), color,
                 link.inferred ? 1.0f : 2.5f);
        if (source && target && !link.label.empty()) {
            link_labels.push_back({source, target, link.label, color});
        }
    }

    ed::End();
    ed::NodeId selected_ids[1];
    const int selected_count = ed::GetSelectedNodes(selected_ids, 1);
    if (selected_count == 1) {
        const int id = static_cast<int>(selected_ids[0].Get());
        if (find_graph_node(*graph, id)) g_selected_graph_node = id;
    } else if (selected_count == 0 &&
               ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
               ImGui::IsItemHovered()) {
        g_selected_graph_node = 0;
    }



    if (!link_labels.empty()) {
        const ImVec2 editor_min = ImGui::GetItemRectMin();
        const ImVec2 editor_max = ImGui::GetItemRectMax();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(editor_min, editor_max, true);
        for (const LinkLabel& label : link_labels) {
            const Quest::GraphNode* source = label.source;
            const Quest::GraphNode* target = label.target;
            const ImVec2 from = ed::GetNodePosition(node_id(source->id));
            const ImVec2 to = ed::GetNodePosition(node_id(target->id));
            const ImVec2 from_size = ed::GetNodeSize(node_id(source->id));
            const ImVec2 to_size = ed::GetNodeSize(node_id(target->id));
            const ImVec2 canvas(
                (from.x + from_size.x + to.x) * 0.5f,
                (from.y + from_size.y * 0.5f +
                 to.y + to_size.y * 0.5f) * 0.5f);
            const ImVec2 screen = ed::CanvasToScreen(canvas);
            const ImVec2 size = ImGui::CalcTextSize(label.text.c_str());
            const ImU32 fill = ImGui::ColorConvertFloat4ToU32(
                ImVec4(0.08f, 0.09f, 0.11f, 0.94f));
            draw->AddRectFilled(ImVec2(screen.x - 5.0f, screen.y - 3.0f),
                                ImVec2(screen.x + size.x + 5.0f,
                                       screen.y + size.y + 3.0f),
                                fill, 4.0f);
            draw->AddText(screen, ImGui::ColorConvertFloat4ToU32(label.color),
                          label.text.c_str());
        }
        draw->PopClipRect();
    }
    if (g_select_completion_requested) {
        std::vector<const Quest::GraphNode*> completions;
        for (const Quest::GraphNode& node : graph->nodes) {
            if (read_only_completion_node(node)) completions.push_back(&node);
        }
        if (g_completion_selection_index < completions.size()) {
            const Quest::GraphNode& completion =
                *completions[g_completion_selection_index];
            ed::ClearSelection();
            ed::SelectNode(node_id(completion.id), false);
            ed::NavigateToSelection(true, 0.0f);
            g_selected_graph_node = completion.id;
            g_completion_focus_frames = 30;
        }
        g_select_completion_requested = false;
        g_focus_quest_requested = false;
        g_fit_requested = false;
    } else if (g_focus_quest_requested) {
        std::vector<const Quest::GraphNode*> opening;
        opening.reserve(graph->nodes.size());
        for (const Quest::GraphNode& node : graph->nodes) {
            opening.push_back(&node);
        }
        if (!g_initial_focus_explicit_range) {
            std::sort(opening.begin(), opening.end(),
                      [](const Quest::GraphNode* a,
                         const Quest::GraphNode* b) {
                          if (a->x != b->x) return a->x < b->x;
                          return a->y < b->y;
                      });
        }
        ed::ClearSelection();
        const std::size_t start = std::min(
            g_initial_focus_explicit_range ? g_initial_focus_node_start : 0,
            opening.size());
        const std::size_t visible =
            std::min(g_initial_focus_node_count, opening.size() - start);
        for (std::size_t i = 0; i < visible; ++i) {
            ed::SelectNode(node_id(opening[start + i]->id), i != 0);
        }
        ed::NavigateToSelection(true, 0.0f);
        g_focus_quest_requested = false;
    } else if (g_fit_requested) {
        ed::NavigateToContent(0.0f);
        g_fit_requested = false;
    }
    if (g_completion_focus_frames > 0 && g_selected_graph_node != 0) {
        ed::ClearSelection();
        ed::SelectNode(node_id(g_selected_graph_node), false);
        ed::NavigateToSelection(true, 0.0f);
        --g_completion_focus_frames;
    }
    ed::SetCurrentEditor(nullptr);
    if (show_inspector) ImGui::EndChild();
    g_apply_layout = false;
}
