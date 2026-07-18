void DrawSelectedReadOnlyNodeInspector(const Quest::Graph& graph,
                                       const Quest::GraphNode& node) {
    ImGui::TextColored(ImVec4(0.55f, 0.78f, 1.0f, 1.0f), "NODE");
    ImGui::TextWrapped("%s", node.badge.empty()
                                ? Quest::NodeKindName(node.kind)
                                : node.badge.c_str());
    ImGui::Separator();
    ImGui::TextDisabled("Read-only original quest");

    const bool quest_completion = read_only_completion_node(node);
    const bool quest_start = !quest_completion &&
        (node.kind == Quest::NodeKind::Quest ||
         contains_case_insensitive(node.badge, "quest start"));
    if (quest_start) {
        const std::string& name = node.title.empty() ? graph.title : node.title;
        draw_read_only_field("Quest name", name);
        ImGui::Spacing();
        ImGui::SeparatorText("Prerequisites");
        const std::vector<std::string> prerequisites =
            read_only_prerequisites(graph, node);
        if (prerequisites.empty()) {
            ImGui::TextDisabled("None found in this quest script");
        } else {
            for (const std::string& prerequisite : prerequisites) {
                ImGui::BulletText("%s", prerequisite.c_str());
            }
        }
        return;
    }

    if (quest_completion) {
        draw_read_only_field("Title", node.title);
        if (!node.subtitle.empty()) {
            draw_read_only_field("Context", node.subtitle);
        }
        ImGui::Spacing();
        ImGui::SeparatorText("Rewards");
        const std::vector<std::string> rewards =
            read_only_completion_rewards(graph, node);
        if (rewards.empty()) {
            ImGui::TextDisabled("None found");
        } else {
            for (const std::string& reward : rewards) {
                ImGui::BulletText("%s", reward.c_str());
            }
        }
        if (!node.details.empty()) {
            ImGui::Spacing();
            ImGui::SeparatorText("Details");
            for (const std::string& detail : node.details) {
                ImGui::TextWrapped("%s", detail.c_str());
            }
        }
        return;
    }

    draw_read_only_field("Title", node.title);
    if (!node.subtitle.empty()) {
        draw_read_only_field("Context", node.subtitle);
    }
    if (!node.details.empty()) {
        ImGui::Spacing();
        ImGui::SeparatorText("Details");
        for (const std::string& detail : node.details) {
            ImGui::TextWrapped("%s", detail.c_str());
        }
    }
}
