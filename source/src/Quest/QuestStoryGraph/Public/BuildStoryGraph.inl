Graph BuildStoryGraph(const std::string& title,
                      const std::string& decompiled_lua,
                      const ReferenceCatalog& references) {
    const Graph technical = BuildGraph(title, decompiled_lua, references);
    const bool frankenbride = contains_ci(title, "frankenbride") &&
        decompiled_lua.find("QO570_FrankenBride") != std::string::npos;
    auto relayout = [](Graph& graph) {
        int root = 0;
        for (const GraphNode& node : graph.nodes) {
            if (node.kind == NodeKind::Quest) {
                root = node.id;
                break;
            }
        }
        if (!root && !graph.nodes.empty()) root = graph.nodes.front().id;
        std::vector<int> order;
        layout_branching_story(graph, root, order);
    };
    if (frankenbride) {
        Graph graph = build_frankenbride_timeline(technical, references);
        attach_completion_rewards(graph, technical, references);
        relayout(graph);
        return graph;
    }
    const bool childhood = contains_ci(title, "childhood") &&
        decompiled_lua.find("QC010_Childhood") != std::string::npos;
    if (childhood) {
        const std::vector<const GraphNode*> primary =
            primary_entity_sequence(technical);
        if (primary.size() >= 30) {
            Graph graph = build_childhood_timeline(technical, primary);
            attach_completion_rewards(graph, technical, references);
            relayout(graph);
            return graph;
        }
    }
    Graph graph;
    graph.title = technical.title;
    if (technical.nodes.empty()) return graph;

    std::unordered_map<int, std::vector<int>> story_by_technical;
    std::unordered_map<int, std::vector<int>> story_exits_by_technical;
    std::unordered_set<std::string> seen_dialogue;
    std::unordered_set<std::string> seen_events;
    std::size_t dialogue_count = 0;
    int root_story = 0;
    std::set<std::tuple<int, int, std::string>> unique_links;

    for (const GraphNode& source : technical.nodes) {
        if (source.kind == NodeKind::Quest) {
            GraphNode node;
            node.id = int(graph.nodes.size()) + 1;
            node.kind = NodeKind::Quest;
            node.badge = "Quest start";
            node.title = clean_names(source.title);
            node.x = source.x;
            node.y = source.y;
            graph.nodes.push_back(std::move(node));
            root_story = graph.nodes.back().id;
            story_by_technical[source.id].push_back(root_story);
            story_exits_by_technical[source.id] = {root_story};
            const std::vector<Beat> setup_beats = story_beats(
                source, seen_dialogue, seen_events);
            if (!setup_beats.empty()) {
                const std::size_t first_setup = graph.nodes.size();
                float setup_y = source.y + estimated_node_height(
                    graph.nodes.back()) + 105.0f;
                std::vector<int> setup_exits;
                append_story_beat_nodes(
                    graph, setup_beats, root_story, source.x, setup_y,
                    dialogue_count, unique_links, "setup", &setup_exits);
                if (!setup_exits.empty()) {
                    story_exits_by_technical[source.id] =
                        std::move(setup_exits);
                }
                for (std::size_t i = first_setup; i < graph.nodes.size(); ++i) {
                    story_by_technical[source.id].push_back(graph.nodes[i].id);
                }
            }
            continue;
        }

        const std::vector<Beat> beats = story_beats(
            source, seen_dialogue, seen_events);
        if (beats.empty()) continue;
        const std::size_t first_new_node = graph.nodes.size();
        float source_y = source.y;
        std::vector<int> source_exits;
        append_story_beat_nodes(
            graph, beats, 0, source.x, source_y, dialogue_count,
            unique_links, {}, &source_exits);
        for (std::size_t i = first_new_node; i < graph.nodes.size(); ++i) {
            story_by_technical[source.id].push_back(graph.nodes[i].id);
        }
        story_exits_by_technical[source.id] = std::move(source_exits);
    }

    std::unordered_map<int, std::vector<int>> outgoing;
    std::unordered_map<int, std::vector<int>> incoming;
    for (const GraphLink& link : technical.links) {
        outgoing[link.from_node].push_back(link.to_node);
        incoming[link.to_node].push_back(link.from_node);
    }

    for (const auto& entry : story_by_technical) {
        if (entry.second.empty()) continue;
        const int source_technical = entry.first;
        const auto exits = story_exits_by_technical.find(source_technical);
        const std::vector<int>& source_story =
            exits != story_exits_by_technical.end() && !exits->second.empty()
                ? exits->second : entry.second;
        std::deque<int> queue;
        std::unordered_set<int> visited;
        for (int next : outgoing[source_technical]) queue.push_back(next);
        while (!queue.empty()) {
            const int current = queue.front();
            queue.pop_front();
            if (!visited.insert(current).second) continue;
            const auto found = story_by_technical.find(current);
            if (found != story_by_technical.end() && !found->second.empty()) {
                for (int source_exit : source_story) {
                    add_link(graph, unique_links, source_exit,
                             found->second.front(),
                             is_decision(graph, source_exit) ? "Yes" : "");
                }
                continue;
            }
            for (int next : outgoing[current]) queue.push_back(next);
        }
    }

    for (const auto& entry : story_by_technical) {
        const std::vector<int>& beats = entry.second;
        for (std::size_t i = 0; i < beats.size(); ++i) {
            const int decision = beats[i];
            if (!is_decision(graph, decision)) continue;
            int previous = 0;
            if (i > 0) {
                previous = beats[i - 1];
            } else {
                std::deque<int> queue;
                std::unordered_set<int> visited;
                for (int prior : incoming[entry.first]) queue.push_back(prior);
                while (!queue.empty() && previous == 0) {
                    const int current = queue.front();
                    queue.pop_front();
                    if (!visited.insert(current).second) continue;
                    const auto found = story_by_technical.find(current);
                    if (found != story_by_technical.end() &&
                        !found->second.empty()) {
                        const auto exits =
                            story_exits_by_technical.find(current);
                        previous = exits != story_exits_by_technical.end() &&
                                   !exits->second.empty()
                            ? exits->second.front() : found->second.back();
                        break;
                    }
                    for (int prior : incoming[current]) queue.push_back(prior);
                }
            }
            if (previous > 0 && previous != root_story) {
                add_link(graph, unique_links, decision, previous, "No");
            }
        }
    }

    std::unordered_map<int, std::vector<int>> forward_story;
    for (const GraphLink& link : graph.links) {
        if (link.label != "No") {
            forward_story[link.from_node].push_back(link.to_node);
        }
    }
    auto condition_reaches_story = [&](int start) {
        std::deque<int> queue;
        std::unordered_set<int> visited;
        for (int next : forward_story[start]) queue.push_back(next);
        while (!queue.empty()) {
            const int current = queue.front();
            queue.pop_front();
            if (!visited.insert(current).second) continue;
            if (!is_decision(graph, current)) return true;
            for (int next : forward_story[current]) queue.push_back(next);
        }
        return false;
    };
    std::unordered_set<int> remove_conditions;
    for (const GraphNode& node : graph.nodes) {
        if (node.badge == "Condition" &&
            !condition_reaches_story(node.id)) {
            remove_conditions.insert(node.id);
        }
    }
    if (!remove_conditions.empty()) {
        graph.links.erase(
            std::remove_if(graph.links.begin(), graph.links.end(),
                           [&](const GraphLink& link) {
                               return remove_conditions.count(link.from_node) ||
                                      remove_conditions.count(link.to_node);
                           }),
            graph.links.end());
        graph.nodes.erase(
            std::remove_if(graph.nodes.begin(), graph.nodes.end(),
                           [&](const GraphNode& node) {
                               return remove_conditions.count(node.id) != 0;
                           }),
            graph.nodes.end());
        for (std::size_t i = 0; i < graph.links.size(); ++i) {
            graph.links[i].id = int(i) + 1;
        }
    }

    if (root_story > 0) {
        GraphNode* root = nullptr;
        for (GraphNode& node : graph.nodes) {
            if (node.id == root_story) {
                root = &node;
                break;
            }
        }
        if (root) {
            root->x = 0.0f;
            root->y = 0.0f;
        }
    }

    std::vector<int> layout_order;
    layout_branching_story(graph, root_story, layout_order);

    std::size_t step_number = 0;
    for (int id : layout_order) {
        for (GraphNode& node : graph.nodes) {
            if (node.id != id) continue;
            const std::string semantic = node.badge;
            if (node.subtitle.empty()) {
                if (semantic == "Condition") node.subtitle = "Decision (Yes / No)";
                else if (semantic == "Objective") node.subtitle = "Objective";
                else if (semantic == "Reward") node.subtitle = "Reward";
                else if (semantic == "Quest end") node.subtitle = "Quest ending";
                else if (semantic == "Dialogue") node.subtitle = "Dialogue scene";
            }
            if (semantic == "Quest step") {
                node.badge = "Quest step " +
                             std::to_string(++step_number);
            }
            break;
        }
    }

    graph.flow_steps = step_number;
    graph.dialogue_lines = dialogue_count;
    for (const GraphNode& node : graph.nodes) {
        for (const std::string& metadata : node.metadata) {
            if (starts_ci(metadata, "Related audio: ")) ++graph.audio_matches;
        }
    }
    attach_completion_rewards(graph, technical, references);
    return graph;
}
