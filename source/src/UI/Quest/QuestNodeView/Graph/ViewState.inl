void SetInitialViewAll(bool enabled) {
    g_initial_view_all = enabled;
}

void SetInitialFocusNodeCount(std::size_t count) {
    g_initial_focus_node_count = std::max<std::size_t>(1, count);
    g_initial_focus_node_start = 0;
    g_initial_focus_explicit_range = false;
}

void SetInitialFocusNodeRange(std::size_t start, std::size_t count) {
    g_initial_focus_node_start = start;
    g_initial_focus_node_count = std::max<std::size_t>(1, count);
    g_initial_focus_explicit_range = true;
}

void Clear() {
    g_active_authored_quest = -1;
    BlueprintUI::CloseActive();
    CancelPendingNpcCreation();
    std::lock_guard<std::mutex> lock(g_graph_mutex);
    g_graph.reset();
    ++g_graph_generation;
}
