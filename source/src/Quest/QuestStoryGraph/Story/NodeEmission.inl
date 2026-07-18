int append_story_beat_nodes(
    Graph& graph, const std::vector<Beat>& beats, int previous,
    float x, float& y, std::size_t& dialogue_count,
    std::set<std::tuple<int, int, std::string>>& unique_links,
    const std::string& first_link_label = {},
    std::vector<int>* terminal_nodes = nullptr) {
    bool first_node = true;
    std::vector<int> pending_branch_ends;
    std::string current_scene;
    std::vector<std::string> pending_scene_details;
    std::vector<std::string> pending_scene_metadata;
    std::vector<QuestEvent> pending_scene_events;
    auto link_to = [&](int target) {
        if (!pending_branch_ends.empty()) {
            for (int branch_end : pending_branch_ends) {
                add_link(graph, unique_links, branch_end, target, "");
            }
            pending_branch_ends.clear();
            return;
        }
        add_link(graph, unique_links, previous, target,
                 first_node ? first_link_label
                            : is_decision(graph, previous)
                                ? "Yes" : std::string());
    };
    for (std::size_t i = 0; i < beats.size();) {
        if (beats[i].source_boundary) {
            current_scene.clear();
            pending_scene_details.clear();
            pending_scene_metadata.clear();
            pending_scene_events.clear();
            ++i;
            continue;
        }
        const bool scene_context =
            starts_ci(beats[i].title, "Cutscene: ") ||
            starts_ci(beats[i].title, "Alternative scene: ");
        const bool has_scene_content = i + 1 < beats.size() &&
            (beats[i + 1].kind == BeatKind::ActorAction ||
             beats[i + 1].kind == BeatKind::Dialogue);
        if (scene_context && has_scene_content) {
            current_scene = readable_childhood_scene(beats[i].title);
            pending_scene_details = beats[i].details;
            pending_scene_metadata = beats[i].metadata;
            pending_scene_events = beats[i].events;
            ++i;
            continue;
        }

        if (beats[i].kind == BeatKind::Dialogue) {
            std::size_t end = i + 1;
            while (end < beats.size() &&
                   beats[end].kind == BeatKind::Dialogue) ++end;
            std::size_t gender_line = end;
            for (std::size_t line = i; line < end; ++line) {
                if (gender_dialogue(beats[line])) {
                    gender_line = line;
                    break;
                }
            }
            if (gender_line > i && gender_line < end) {
                end = gender_line;
            } else if (gender_line == i) {
                const GenderDialogue variants = *gender_dialogue(beats[i]);
                if (previous <= 0 && pending_branch_ends.empty()) {
                    GraphNode fork;
                    fork.id = int(graph.nodes.size()) + 1;
                    fork.kind = NodeKind::State;
                    fork.badge = "Player gender";
                    fork.title = variants.subject +
                        "'s gender determines this dialogue";
                    fork.x = x;
                    fork.y = y;
                    y += estimated_node_height(fork) + 150.0f;
                    graph.nodes.push_back(std::move(fork));
                    previous = graph.nodes.back().id;
                    first_node = false;
                }
                std::vector<int> branch_sources = pending_branch_ends;
                pending_branch_ends.clear();
                if (branch_sources.empty() && previous > 0) {
                    branch_sources.push_back(previous);
                }
                auto make_branch = [&](const char* title,
                                       const std::string& text,
                                       const std::string& suffix,
                                       float branch_x) {
                    GraphNode branch;
                    branch.id = int(graph.nodes.size()) + 1;
                    branch.kind = NodeKind::Thread;
                    branch.badge = "Dialogue branch";
                    branch.title = title;
                    branch.subtitle = variants.speaker;
                    branch.details.push_back(
                        variants.speaker + ": \"" + text + "\"");
                    if (!current_scene.empty()) {
                        branch.details.push_back("Scene: " + current_scene);
                    }
                    branch.details.insert(branch.details.end(),
                                          pending_scene_details.begin(),
                                          pending_scene_details.end());
                    branch.metadata = gender_metadata(beats[i], suffix);
                    branch.metadata.insert(branch.metadata.begin(),
                                           pending_scene_metadata.begin(),
                                           pending_scene_metadata.end());
                    branch.events = beats[i].events;
                    branch.events.insert(branch.events.begin(),
                                         pending_scene_events.begin(),
                                         pending_scene_events.end());
                    branch.x = branch_x;
                    branch.y = y;
                    graph.nodes.push_back(std::move(branch));
                    return graph.nodes.back().id;
                };

                const int male = make_branch(
                    "If male", variants.male, "_HM", x - 380.0f);
                const float male_height = estimated_node_height(
                    graph.nodes[size_t(male - 1)]);
                const int female = make_branch(
                    "If female", variants.female, "_HF", x + 380.0f);
                const float female_height = estimated_node_height(
                    graph.nodes[size_t(female - 1)]);
                for (int branch_source : branch_sources) {
                    add_link(graph, unique_links, branch_source, male,
                             "Male " + variants.subject);
                    add_link(graph, unique_links, branch_source, female,
                             "Female " + variants.subject);
                }

                y += std::max(male_height, female_height) + 150.0f;
                pending_branch_ends = {male, female};
                previous = 0;
                first_node = false;
                dialogue_count += 2;
                current_scene.clear();
                pending_scene_details.clear();
                pending_scene_metadata.clear();
                pending_scene_events.clear();
                ++i;
                continue;
            }
            GraphNode node;
            node.id = int(graph.nodes.size()) + 1;
            node.kind = NodeKind::Thread;
            node.badge = "Dialogue";
            node.title = dialogue_node_title(beats, i, end);
            if (!current_scene.empty()) {
                node.subtitle = "Scene: " + current_scene;
            }
            std::set<std::string> metadata_seen;
            for (const std::string& metadata : pending_scene_metadata) {
                if (metadata_seen.insert(metadata).second) {
                    node.metadata.push_back(metadata);
                }
            }
            node.events = pending_scene_events;
            for (std::size_t line = i; line < end; ++line) {
                ++dialogue_count;
                node.details.push_back(quoted_dialogue(beats[line].title));
                for (const std::string& detail : beats[line].details) {
                    node.details.push_back(detail);
                }
                for (const std::string& metadata : beats[line].metadata) {
                    if (metadata_seen.insert(metadata).second) {
                        node.metadata.push_back(metadata);
                    }
                }
                node.events.insert(node.events.end(), beats[line].events.begin(),
                                   beats[line].events.end());
            }
            node.details.insert(node.details.end(),
                                pending_scene_details.begin(),
                                pending_scene_details.end());
            pending_scene_details.clear();
            pending_scene_metadata.clear();
            pending_scene_events.clear();
            node.x = x;
            node.y = y;
            y += estimated_node_height(node) + 105.0f;
            graph.nodes.push_back(std::move(node));
            link_to(graph.nodes.back().id);
            previous = graph.nodes.back().id;
            first_node = false;
            i = end;
            continue;
        }

        GraphNode node;
        node.id = int(graph.nodes.size()) + 1;
        node.kind = node_kind(beats[i].kind);
        node.badge = badge(beats[i].kind);
        node.title = beats[i].title;
        node.subtitle = beats[i].subtitle;
        node.details = beats[i].details;
        node.metadata = beats[i].metadata;
        node.events = beats[i].events;

        if (starts_ci(beats[i].title, "Cutscene: ") ||
            starts_ci(beats[i].title, "Alternative scene: ")) {
            current_scene = readable_childhood_scene(beats[i].title);
            node.title = current_scene;
            node.badge = starts_ci(beats[i].title, "Alternative scene: ")
                ? "Alternative scene" : "Story event";
            node.kind = NodeKind::Action;
        } else if (starts_ci(beats[i].title, "Cinematic: ")) {
            current_scene = beats[i].title;
            node.badge = "Cinematic";
        } else if (beats[i].kind == BeatKind::ActorAction) {
            node.badge = "Actor action";
            node.kind = NodeKind::Action;
            if (!current_scene.empty()) {
                node.subtitle = "Scene: " + current_scene;
            }
            node.details.insert(node.details.begin(),
                                pending_scene_details.begin(),
                                pending_scene_details.end());
            node.metadata.insert(node.metadata.begin(),
                                 pending_scene_metadata.begin(),
                                 pending_scene_metadata.end());
            node.events.insert(node.events.begin(), pending_scene_events.begin(),
                               pending_scene_events.end());
            pending_scene_details.clear();
            pending_scene_metadata.clear();
            pending_scene_events.clear();
        } else {
            current_scene.clear();
            pending_scene_details.clear();
            pending_scene_metadata.clear();
            pending_scene_events.clear();
        }
        if (contains_ci(node.title, "Go to Hero Wish Marker")) {
            node.title = "Move Sparrow to the wishing spot";
            node.badge = "Player action";
            node.subtitle.clear();
        }
        if (starts_ci(node.title, "Has the trigger been entered?")) {
            node.title = "Has Sparrow reached the quiet wishing spot?";
            node.badge = "Condition";
            node.subtitle.clear();
        }
        if (contains_ci(node.title,
                        "Quest Manager.Hero Entity inside shack trigger")) {
            node.title = "Has Sparrow entered the shack?";
            node.badge = "Condition";
            node.subtitle.clear();
        }
        if (starts_ci(node.title, "Objective: ")) {
            node.title = trim(node.title.substr(
                std::string("Objective: ").size()));
            node.badge = "Objective";
            node.subtitle.clear();
        }
        if (starts_ci(node.title,
                      "Travel to Albion / Fairfax Castle Gardens")) {
            node.title =
                "Travel from Bowerstone Old Town to Fairfax Castle";
            node.badge = "Travel";
            node.subtitle.clear();
        }
        if (starts_ci(node.title, "Cinematic: Intro")) {
            node.title = "Opening cinematic";
            node.badge = "Cinematic";
            node.metadata.push_back("Movie asset: Intro.bik");
        }

        if (!node.title.empty()) {
            node.x = x;
            node.y = y;
            y += estimated_node_height(node) + 105.0f;
            graph.nodes.push_back(std::move(node));
            link_to(graph.nodes.back().id);
            previous = graph.nodes.back().id;
            first_node = false;
        }
        ++i;
    }
    if (terminal_nodes) {
        *terminal_nodes = pending_branch_ends.empty()
            ? (previous > 0 ? std::vector<int>{previous} : std::vector<int>{})
            : pending_branch_ends;
    }
    return pending_branch_ends.empty()
        ? previous : pending_branch_ends.front();
}
