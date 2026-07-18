Graph BuildGraph(const std::string& title, const std::string& decompiled_lua) {
    return BuildGraph(title, decompiled_lua, ReferenceCatalog{});
}

Graph BuildGraph(const std::string& title, const std::string& decompiled_lua,
                 const ReferenceCatalog& references) {
    Graph graph;
    graph.title = title;
    const std::vector<std::string> lines = split_lines(decompiled_lua);
    if (lines.empty()) return graph;

    const std::regex thread_re(
        R"quest(^\s*QuestManager\.New(Quest|Entity)Thread\("([^"]+)"\))quest");
    const std::regex function_re(
        R"(^\s*function\s+([A-Za-z0-9_\.]+):([A-Za-z0-9_]+)\s*\()");

    std::vector<ThreadDecl> threads;
    std::unordered_set<std::string> declared_threads;
    for (const std::string& line : lines) {
        std::smatch match;
        if (!std::regex_search(line, match, thread_re)) continue;
        const std::string class_name = match[2].str();
        if (!declared_threads.insert(class_name).second) continue;
        const bool entity = match[1].str() == "Entity";
        threads.push_back({class_name, entity});
        if (entity) ++graph.entity_threads;
        else ++graph.quest_threads;
    }

    std::vector<FunctionBlock> functions;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::smatch match;
        if (!std::regex_search(lines[i], match, function_re)) continue;
        if (!functions.empty()) functions.back().end = i;
        FunctionBlock function;
        function.class_name = match[1].str();
        function.method = match[2].str();
        function.begin = i;
        function.end = lines.size();
        functions.push_back(std::move(function));
    }
    graph.functions = functions.size();

    const ScriptFacts facts = build_facts(lines);
    std::string root_class;
    for (const ThreadDecl& thread : threads) {
        if (!thread.entity) {
            root_class = thread.class_name;
            break;
        }
    }
    if (root_class.empty() && !functions.empty()) root_class = functions.front().class_name;

    GraphNode& root = add_node(
        graph, NodeKind::Quest,
        humanize(root_class.empty() ? title : root_class),
        "Quest flow begins here", 0.0f, 0.0f);
    const int root_id = root.id;
    root.details.push_back("Script: " + title);
    if (!root_class.empty()) root.details.push_back("Main controller: " + root_class);
    root.details.push_back("Follow the connected quest steps in arrow order.");

    if (const FunctionBlock* init = find_function(functions, root_class, "Init")) {
        Narrative setup = collect_narrative(lines, init->begin + 1, init->end,
                                            "Quest", facts, references, 8,
                                            root_class, "Init");
        if (!setup.actions.empty() || !setup.supplemental_events.empty() ||
            !setup.entities.empty() ||
            !setup.locations.empty() || !setup.markers.empty() ||
            !setup.coordinates.empty()) {
            root.details.push_back("Initial setup:");
            append_narrative_details(root, setup, 6, "  ");
            count_references(graph, setup);
        }
    }

    std::set<std::tuple<int, int, std::string>> unique_links;
    std::unordered_map<std::string, std::vector<std::pair<int, std::string>>>
        thread_starts;
    std::unordered_map<std::string, std::vector<int>> helper_sources;
    std::unordered_map<int, Narrative> flow_narratives;

    const FunctionBlock* main_update = find_function(functions, root_class, "Update");
    std::vector<StateBranch> main_states;
    if (main_update) main_states = find_states(*main_update, lines);

    if (!main_states.empty()) {
        std::unordered_map<int, int> state_nodes;
        for (std::size_t i = 0; i < main_states.size(); ++i) {
            StateBranch& state = main_states[i];
            Narrative narrative = collect_narrative(
                lines, state.begin + 1, state.end, "Quest", facts, references,
                256, root_class, "Update", state.value);
            for (NarrativeAction& action : narrative.actions) {
                if (action.transition && *action.transition == -2147483647) {
                    action.transition = state.value + 1;
                    narrative.transitions.push_back(state.value + 1);
                }
            }
            auto [x, y] = main_flow_position(i);
            GraphNode& node = add_node(
                graph, NodeKind::State,
                "Step " + std::to_string(i + 1) + " - " +
                    narrative_headline(narrative, "Continue quest logic"),
                "Main quest | script state " + std::to_string(state.value), x, y);
            state.node_id = node.id;
            state_nodes[state.value] = node.id;
            append_narrative_details(node, narrative, 256);
            node.details.push_back("Source: " + root_class + ":Update, state " +
                                   std::to_string(state.value));
            flow_narratives[node.id] = narrative;
            for (const NarrativeAction& action : narrative.actions) {
                if (!action.starts_thread.empty()) {
                    thread_starts[action.starts_thread].push_back(
                        {node.id, action.entity_instance});
                }
            }
            for (const std::string& method : narrative.self_calls) {
                helper_sources[function_key(root_class, method)].push_back(node.id);
            }
            count_references(graph, narrative);
            ++graph.states;
            ++graph.flow_steps;
        }

        add_link(graph, unique_links, root_id, main_states.front().node_id,
                 "begins");
        for (std::size_t i = 0; i < main_states.size(); ++i) {
            StateBranch& state = main_states[i];
            Narrative& narrative = flow_narratives[state.node_id];
            std::vector<int> valid_targets;
            for (int target : narrative.transitions) {
                if (std::find(valid_targets.begin(), valid_targets.end(), target) ==
                    valid_targets.end() && state_nodes.count(target)) {
                    valid_targets.push_back(target);
                }
            }
            bool inferred = false;
            if (valid_targets.empty() && !narrative.terminal &&
                i + 1 < main_states.size() &&
                main_states[i + 1].value == state.value + 1) {
                valid_targets.push_back(main_states[i + 1].value);
                inferred = true;
            }
            GraphNode* node = node_by_id(graph, state.node_id);
            for (int target : valid_targets) {
                add_link(graph, unique_links, state.node_id, state_nodes[target],
                         inferred ? "next state (inferred)" : "then", inferred);
                if (node) {
                    node->details.push_back(
                        std::string(inferred ? "Likely next: " : "Next: ") +
                        "script state " + std::to_string(target) +
                        (inferred ? " (transition is indirect)" : ""));
                }
            }
        }
    } else if (main_update) {
        Narrative narrative = collect_narrative(
            lines, main_update->begin + 1, main_update->end,
            "Quest", facts, references, 256, root_class, "Update");
        int previous = root_id;
        std::vector<std::pair<std::size_t, int>> action_nodes;
        for (std::size_t i = 0; i < narrative.actions.size(); ++i) {
            const NarrativeAction& action = narrative.actions[i];
            auto [x, y] = main_flow_position(i);
            GraphNode& node = add_node(
                graph, NodeKind::Action,
                "Step " + std::to_string(i + 1) + " - " +
                    shorten(action.summary, 76),
                "Main quest action", x, y);
            node.details.push_back(action.summary);
            for (const std::string& extra : action.extra) node.details.push_back(extra);
            node.events.push_back(action.event);
            action_nodes.push_back({action.event.source_line, node.id});
            add_link(graph, unique_links, previous, node.id, "then");
            previous = node.id;
            if (!action.starts_thread.empty()) {
                thread_starts[action.starts_thread].push_back(
                    {node.id, action.entity_instance});
            }
            ++graph.flow_steps;
        }
        for (const QuestEvent& event : narrative.supplemental_events) {
            int owner = root_id;
            for (const auto& candidate : action_nodes) {
                if (candidate.first > event.source_line) break;
                owner = candidate.second;
            }
            if (GraphNode* node = node_by_id(graph, owner)) {
                node->events.push_back(event);
            }
        }
        count_references(graph, narrative);
    }

    const std::size_t main_rows =
        std::max<std::size_t>(1, (graph.flow_steps + 4) / 5);
    float support_y = float(main_rows) * 390.0f + 140.0f;
    std::unordered_map<std::string, int> thread_nodes;
    std::unordered_map<std::string, std::string> class_actor;

    for (const ThreadDecl& thread : threads) {
        if (!thread.entity && thread.class_name == root_class) continue;
        std::string actor;
        const auto starts = thread_starts.find(thread.class_name);
        if (starts != thread_starts.end()) {
            for (const auto& source : starts->second) {
                if (!source.second.empty()) {
                    actor = source.second;
                    break;
                }
            }
        }
        if (actor.empty()) actor = humanize(thread.class_name);
        class_actor[thread.class_name] = actor;

        Narrative behaviour;
        Narrative setup_narrative;
        std::vector<std::pair<int, Narrative>> phases;
        if (const FunctionBlock* init = find_function(functions, thread.class_name, "Init")) {
            setup_narrative = collect_narrative(
                lines, init->begin + 1, init->end, actor, facts, references, 8,
                thread.class_name, "Init");
            merge_narrative(behaviour, setup_narrative);
        }
        std::string behaviour_method = "CustomUpdate";
        const FunctionBlock* update =
            find_function(functions, thread.class_name, behaviour_method);
        if (!update) {
            behaviour_method = "Update";
            update = find_function(functions, thread.class_name, behaviour_method);
        }
        if (update) {
            const std::vector<StateBranch> states = find_states(*update, lines);
            if (states.empty()) {
                Narrative body = collect_narrative(
                    lines, update->begin + 1, update->end, actor,
                    facts, references, 256, thread.class_name,
                    behaviour_method);
                merge_narrative(behaviour, body);
            } else {
                graph.states += states.size();
                for (const StateBranch& state : states) {
                    Narrative phase = collect_narrative(
                        lines, state.begin + 1, state.end, actor,
                        facts, references, 512, thread.class_name,
                        behaviour_method, state.value);
                    phases.push_back({state.value, phase});
                    merge_narrative(behaviour, phase);
                }
            }
        }

        GraphNode& node = add_node(
            graph, thread.entity ? NodeKind::Thread : NodeKind::Function,
            humanize(actor) + (thread.entity ? " - behaviour" : " - supporting flow"),
            std::string(thread.entity ? "NPC/entity" : "Quest sub-thread") +
                " | " + thread.class_name,
            360.0f, support_y);
        const int thread_node_id = node.id;
        thread_nodes[thread.class_name] = thread_node_id;
        node.details.push_back(std::string(thread.entity ? "Attached entity: "
                                                         : "Thread: ") + actor);
        if (!phases.empty()) {
            node.details.push_back(
                std::to_string(phases.size()) +
                " connected phases; follow the arrows from left to right.");
            if (!setup_narrative.actions.empty() ||
                !setup_narrative.supplemental_events.empty() ||
                !setup_narrative.entities.empty() ||
                !setup_narrative.locations.empty() ||
                !setup_narrative.markers.empty() ||
                !setup_narrative.coordinates.empty()) {
                node.details.push_back("Initial setup:");
                append_narrative_details(node, setup_narrative, 6, "  ");
                count_references(graph, setup_narrative);
            }
            for (const std::string& method : setup_narrative.self_calls) {
                helper_sources[function_key(thread.class_name, method)]
                    .push_back(thread_node_id);
            }

            int previous_phase = thread_node_id;
            for (std::size_t p = 0; p < phases.size(); ++p) {
                constexpr std::size_t columns = 5;
                const std::size_t row = p / columns;
                std::size_t column = p % columns;
                if ((row & 1u) != 0) column = columns - 1 - column;
                GraphNode& phase = add_node(
                    graph, NodeKind::State,
                    humanize(actor) + " - Phase " + std::to_string(p + 1) +
                        ": " + narrative_headline(
                            phases[p].second, "Continue behaviour"),
                    "NPC/entity sequence | script state " +
                        std::to_string(phases[p].first),
                    790.0f + float(column) * 430.0f,
                    support_y + float(row) * 900.0f);
                append_narrative_details(phase, phases[p].second, 512);
                phase.details.push_back("Source: " + thread.class_name + ":" +
                                        behaviour_method + ", state " +
                                        std::to_string(phases[p].first));
                add_link(graph, unique_links, previous_phase, phase.id,
                         p == 0 ? "begins behaviour" : "then");
                previous_phase = phase.id;
                count_references(graph, phases[p].second);
                for (const std::string& method : phases[p].second.self_calls) {
                    helper_sources[function_key(thread.class_name, method)]
                        .push_back(phase.id);
                }
            }
        } else {
            append_narrative_details(node, behaviour, 256);
            count_references(graph, behaviour);
            for (const std::string& method : behaviour.self_calls) {
                helper_sources[function_key(thread.class_name, method)]
                    .push_back(thread_node_id);
            }
        }
        GraphNode* thread_node = node_by_id(graph, thread_node_id);
        if (thread_node && behaviour.actions.empty() && behaviour.entities.empty() &&
            behaviour.locations.empty() && behaviour.markers.empty()) {
            thread_node->details.push_back(
                "No dialogue, movement, combat, wait, or location action was "
                "recognized in this thread.");
        }
        if (thread_node) {
            thread_node->details.push_back("Source class: " + thread.class_name);
        }

        if (starts != thread_starts.end()) {
            for (const auto& source : starts->second) {
                add_link(graph, unique_links, source.first, thread_node_id,
                         "starts " + humanize(actor));
            }
        } else {
            add_link(graph, unique_links, root_id, thread_node_id,
                     "supporting behaviour");
        }
        const std::size_t phase_rows = phases.empty()
            ? 1 : std::max<std::size_t>(1, (phases.size() + 4) / 5);
        support_y += float(phase_rows) * 900.0f + 140.0f;
    }

    support_y += 100.0f;
    std::size_t helper_index = 0;
    for (const FunctionBlock& function : functions) {
        if (function.method == "Init" || function.method == "Update" ||
            function.method == "CustomUpdate" ||
            function.method == "OnTerminated") continue;
        const std::string key = function_key(function.class_name, function.method);
        std::string actor = "Quest";
        auto actor_it = class_actor.find(function.class_name);
        if (actor_it != class_actor.end()) actor = actor_it->second;
        Narrative narrative = collect_narrative(
            lines, function.begin + 1, function.end, actor,
            facts, references, 256, function.class_name, function.method);
        if (narrative.actions.empty() && narrative.supplemental_events.empty() &&
            narrative.entities.empty() &&
            narrative.locations.empty() && narrative.markers.empty() &&
            narrative.coordinates.empty()) continue;

        const std::size_t column = helper_index % 4;
        const std::size_t row = helper_index / 4;
        GraphNode& node = add_node(
            graph, NodeKind::Function, humanize(function.method),
            "Supporting action | " + humanize(actor),
            360.0f + float(column) * 430.0f,
            support_y + float(row) * 720.0f);
        append_narrative_details(node, narrative, 256);
        node.details.push_back("Source: " + function.class_name + ":" +
                               function.method);
        count_references(graph, narrative);

        auto sources = helper_sources.find(key);
        if (sources != helper_sources.end() && !sources->second.empty()) {
            for (int source : sources->second) {
                add_link(graph, unique_links, source, node.id, "runs");
            }
        } else {
            const auto owner = thread_nodes.find(function.class_name);
            add_link(graph, unique_links,
                     owner == thread_nodes.end() ? root_id : owner->second,
                     node.id, "supporting logic");
        }
        ++helper_index;
    }

    if (graph.flow_steps == 0) {
        if (GraphNode* root_node = node_by_id(graph, root_id)) {
            root_node->details.push_back(
                "No ordered main Update flow was recognized; supporting "
                "behaviours are shown below.");
        }
    } else if (GraphNode* root_node = node_by_id(graph, root_id)) {
        root_node->details.push_back(
            std::to_string(graph.flow_steps) + " main quest steps | " +
            std::to_string(graph.entity_threads) + " entity behaviours");
    }

    return graph;
}
