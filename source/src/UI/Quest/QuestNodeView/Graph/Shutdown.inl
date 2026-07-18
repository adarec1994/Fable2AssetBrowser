void RequestSelectCompletionNode(std::size_t index) {
    g_completion_selection_index = index;
    g_select_completion_requested = true;
}

void Shutdown() {
    BlueprintUI::Shutdown();
    if (g_editor) {
        ed::DestroyEditor(g_editor);
        g_editor = nullptr;
    }
    std::lock_guard<std::mutex> lock(g_graph_mutex);
    g_graph.reset();
    ++g_graph_generation;
    g_authored_quests.clear();
    g_active_authored_quest = -1;
    g_selected_graph_node = 0;
}
