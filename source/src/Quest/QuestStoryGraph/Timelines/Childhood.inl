Graph build_childhood_timeline(
    const Graph& technical,
    const std::vector<const GraphNode*>& rose_sequence) {
    Graph graph;
    graph.title = technical.title;
    if (rose_sequence.size() < 30) return graph;

    const GraphNode* technical_root = nullptr;
    for (const GraphNode& node : technical.nodes) {
        if (node.kind == NodeKind::Quest) {
            technical_root = &node;
            break;
        }
    }
    GraphNode root;
    root.id = 1;
    root.kind = NodeKind::Quest;
    root.badge = "Quest start";
    root.title = technical_root
        ? clean_names(technical_root->title)
        : clean_names(technical.title);
    graph.nodes.push_back(std::move(root));

    const GraphNode* intro = source_with_detail(technical, "Play movie Intro.bik");
    const GraphNode* murgo_pitch = source_with_detail(
        technical, "Cutscene ID: QC010_MurgoPitch1");
    const GraphNode* travel = source_with_detail(
        technical, "Travel to Albion / Fairfax Castle Gardens");

    struct Section {
        std::size_t begin;
        std::size_t end;
        const char* title;
    };
    static const Section sections[] = {
        {0, 2, "Opening in Bowerstone Old Town"},
        {2, 9, "Follow Rose to Murgo's crowd"},
        {9, 10, "Earn five gold for the music box"},
        {10, 13, "Return to Murgo and buy the music box"},
        {13, 16, "Go somewhere quiet and make the wish"},
        {16, 18, "Return to the shack and go to bed"},
        {18, 22, "Meet Lucien's guard the next morning"},
        {22, 25, "Travel to Fairfax Castle and follow Jeeves"},
        {25, 27, "Meet Lord Lucien in his study"},
        {27, 30, "Stand in the magic circle"},
    };

    std::unordered_set<std::string> seen_dialogue;
    std::unordered_set<std::string> seen_events;
    std::size_t dialogue_count = 0;
    int previous = graph.nodes.front().id;
    std::vector<int> previous_ends{previous};
    std::set<std::tuple<int, int, std::string>> unique_links;

    auto append_source = [&](std::vector<Beat>& beats,
                             const GraphNode* source) {
        if (!source) return;
        std::vector<Beat> source_beats = story_beats(
            *source, seen_dialogue, seen_events);
        if (!beats.empty() && !source_beats.empty()) {
            Beat boundary;
            boundary.source_boundary = true;
            beats.push_back(std::move(boundary));
        }
        beats.insert(beats.end(),
                     std::make_move_iterator(source_beats.begin()),
                     std::make_move_iterator(source_beats.end()));
    };

    float main_y = estimated_node_height(graph.nodes.front()) + 105.0f;
    auto add_step_node = [&](const std::string& title,
                             const std::string& step_badge,
                             float x, float& y) {
        GraphNode node;
        node.id = int(graph.nodes.size()) + 1;
        node.kind = NodeKind::State;
        node.badge = step_badge;
        node.title = title;
        node.x = x;
        node.y = y;
        y += estimated_node_height(node) + 105.0f;
        graph.nodes.push_back(std::move(node));
        return graph.nodes.back().id;
    };

    for (std::size_t section_index = 0;
         section_index < std::size(sections); ++section_index) {
        const Section& section = sections[section_index];
        std::vector<Beat> beats;
        if (section_index == 1) {
            std::vector<Beat> prefix;
            for (std::size_t i = section.begin; i < 5; ++i) {
                append_source(prefix, rose_sequence[i]);
            }

            std::vector<Beat> conditional = story_beats(
                *rose_sequence[5], seen_dialogue, seen_events);
            std::vector<Beat> creep_scene;
            std::vector<Beat> beckons_scene;
            std::optional<Beat> crowd_movement;
            enum class ConditionalScene { None, Creep, Beckons, Ignore };
            ConditionalScene active_scene = ConditionalScene::None;
            for (const Beat& beat : conditional) {
                if (starts_ci(beat.title, "Cutscene: ")) {
                    if (contains_ci(beat.title, "Rose Creep")) {
                        active_scene = creep_scene.empty()
                            ? ConditionalScene::Creep
                            : ConditionalScene::Ignore;
                        if (active_scene == ConditionalScene::Creep) {
                            creep_scene.push_back(beat);
                        }
                    } else if (contains_ci(
                                   beat.title, "Rose Beckons To Crowd")) {
                        active_scene = ConditionalScene::Beckons;
                        beckons_scene.push_back(beat);
                    } else {
                        active_scene = ConditionalScene::Ignore;
                    }
                    continue;
                }
                if (beat.kind == BeatKind::ActorAction &&
                    contains_ci(beat.title, "Rose In Crowd Marker")) {
                    crowd_movement = beat;
                    active_scene = ConditionalScene::None;
                    continue;
                }
                if (beat.kind != BeatKind::Dialogue) continue;
                if (active_scene == ConditionalScene::Creep) {
                    creep_scene.push_back(beat);
                } else if (active_scene == ConditionalScene::Beckons) {
                    beckons_scene.push_back(beat);
                }
            }

            std::vector<Beat> suffix;
            for (std::size_t i = 6; i < section.end; ++i) {
                append_source(suffix, rose_sequence[i]);
                if (i == 6) append_source(suffix, murgo_pitch);
            }

            const int step_id = add_step_node(
                section.title, "Quest step 2", 0.0f, main_y);
            for (int branch_end : previous_ends) {
                add_link(graph, unique_links, branch_end, step_id, "");
            }
            int prefix_end = append_story_beat_nodes(
                graph, prefix, step_id, 0.0f, main_y,
                dialogue_count, unique_links);

            const int in_crowd_condition = add_step_node(
                "Is Sparrow already in Murgo's crowd?", "Condition",
                0.0f, main_y);
            graph.nodes.back().metadata.push_back(
                "Lua condition: self.ParentQuest.HeroInCrowd");
            add_link(graph, unique_links, prefix_end, in_crowd_condition, "");

            const int through_arch_condition = add_step_node(
                "Has Sparrow passed through the arch?", "Condition",
                0.0f, main_y);
            graph.nodes.back().metadata.push_back(
                "Lua condition: self.ParentQuest.HeroThroughArch");
            add_link(graph, unique_links, in_crowd_condition,
                     through_arch_condition, "No");

            const float branch_start_y = main_y;
            float through_arch_y = branch_start_y;
            std::vector<int> through_arch_ends;
            append_story_beat_nodes(
                graph, creep_scene, through_arch_condition, -430.0f,
                through_arch_y, dialogue_count, unique_links,
                "Yes", &through_arch_ends);

            std::vector<Beat> beckoned_path = beckons_scene;
            if (crowd_movement) {
                Beat boundary;
                boundary.source_boundary = true;
                beckoned_path.push_back(std::move(boundary));
                beckoned_path.push_back(*crowd_movement);
            }
            if (!creep_scene.empty()) {
                Beat boundary;
                boundary.source_boundary = true;
                beckoned_path.push_back(std::move(boundary));
                beckoned_path.insert(beckoned_path.end(),
                                     creep_scene.begin(), creep_scene.end());
            }
            float beckoned_y = branch_start_y;
            std::vector<int> beckoned_ends;
            append_story_beat_nodes(
                graph, beckoned_path, through_arch_condition, 430.0f,
                beckoned_y, dialogue_count, unique_links,
                "No", &beckoned_ends);

            main_y = std::max(through_arch_y, beckoned_y) + 105.0f;
            const std::size_t suffix_begin = graph.nodes.size();
            std::vector<int> suffix_ends;
            append_story_beat_nodes(
                graph, suffix, 0, 0.0f, main_y, dialogue_count,
                unique_links, {}, &suffix_ends);
            if (suffix_begin < graph.nodes.size()) {
                const int suffix_first = graph.nodes[suffix_begin].id;
                add_link(graph, unique_links, in_crowd_condition,
                         suffix_first, "Yes");
                for (int branch_end : through_arch_ends) {
                    add_link(graph, unique_links, branch_end, suffix_first, "");
                }
                for (int branch_end : beckoned_ends) {
                    add_link(graph, unique_links, branch_end, suffix_first, "");
                }
                previous = suffix_ends.empty()
                    ? suffix_first : suffix_ends.front();
                previous_ends = suffix_ends.empty()
                    ? std::vector<int>{suffix_first} : suffix_ends;
            } else {
                previous = in_crowd_condition;
                previous_ends = {in_crowd_condition};
            }
            continue;
        }
        if (section_index == 0) append_source(beats, intro);
        if (section_index == 7) append_source(beats, travel);
        for (std::size_t i = section.begin; i < section.end; ++i) {
            append_source(beats, rose_sequence[i]);



            if (section_index == 1 && i == 6) {
                append_source(beats, murgo_pitch);
            }
        }
        if (beats.empty()) continue;

        if (section_index == 2) {
            std::vector<Beat> job_beats[7];
            int current_job = 6;
            for (Beat& beat : beats) {
                if (starts_ci(beat.title, "Cutscene: ")) {
                    current_job = childhood_job_for_event(
                        beat.title, current_job);
                    mark_childhood_alternative(beat);
                }
                job_beats[current_job].push_back(std::move(beat));
            }

            const int hub_id = add_step_node(
                section.title, "Quest step 3", 0.0f, main_y);
            GraphNode& hub = graph.nodes.back();
            const float hub_empty_height =
                main_y - hub.y - 105.0f;
            hub.details = {
                "Complete all five jobs. They may be done in any order:",
                "Pose for Barnum's picture",
                "Clear the beetles from Balthazar's warehouse",
                "Deliver Monty's love letter",
                "Find Derek's arrest warrants",
                "Retrieve Pete's stolen bottle",
            };
            main_y += estimated_node_height(hub) - hub_empty_height;
            for (int branch_end : previous_ends) {
                add_link(graph, unique_links, branch_end, hub_id, "");
            }

            static const char* job_titles[] = {
                "Pose for Barnum's picture",
                "Clear the beetles from Balthazar's warehouse",
                "Deliver Monty's love letter",
                "Find Derek's arrest warrants",
                "Retrieve Pete's stolen bottle",
                "Help the dog after Rex's attack",
                "Rose reports the current gold total",
            };
            std::vector<int> required_jobs;
            std::vector<int> phase_events;
            float branch_end_y = main_y;
            for (int job = 0; job < 7; ++job) {
                if (job_beats[job].empty()) continue;
                const float lane_x = float(job - 3) * 620.0f;
                float lane_y = main_y;
                const std::string lane_badge = job < 5
                    ? "Step 3 job - any order"
                    : "Step 3 event";
                const int lane_start = add_step_node(
                    job_titles[job], lane_badge, lane_x, lane_y);
                add_link(graph, unique_links, hub_id, lane_start,
                         job < 5 ? "any order"
                                 : job == 5 ? "after the third job"
                                            : "during this step");
                const int lane_end = append_story_beat_nodes(
                    graph, job_beats[job], lane_start, lane_x, lane_y,
                    dialogue_count, unique_links);
                if (job < 5) required_jobs.push_back(lane_end);
                else phase_events.push_back(lane_end);
                branch_end_y = std::max(branch_end_y, lane_y);
            }

            main_y = branch_end_y + 105.0f;
            const int collected_id = add_step_node(
                "Five gold collected", "Step 3 complete", 0.0f, main_y);
            GraphNode& collected = graph.nodes.back();
            const float collected_empty_height =
                main_y - collected.y - 105.0f;
            collected.details.push_back(
                "The story continues after all five jobs are complete.");
            main_y += estimated_node_height(collected) -
                      collected_empty_height;
            for (int job_id : required_jobs) {
                add_link(graph, unique_links, job_id, collected_id,
                         "job complete");
            }
            for (int event_id : phase_events) {
                add_link(graph, unique_links, event_id, collected_id,
                         "during the five-gold phase");
            }
            previous = collected_id;
            previous_ends = {collected_id};
            continue;
        }

        const int step_id = add_step_node(
            section.title,
            "Quest step " + std::to_string(section_index + 1),
            0.0f, main_y);
        for (int branch_end : previous_ends) {
            add_link(graph, unique_links, branch_end, step_id, "");
        }
        std::vector<int> section_ends;
        previous = append_story_beat_nodes(
            graph, beats, step_id, 0.0f, main_y,
            dialogue_count, unique_links, {}, &section_ends);
        previous_ends = section_ends.empty()
            ? std::vector<int>{previous} : std::move(section_ends);
    }

    std::vector<Beat> ending;
    Beat fall;
    fall.kind = BeatKind::Task;
    fall.title =
        "Cinematic: Lucien shoots Sparrow, who falls from the castle";
    fall.subtitle = "Story event";
    fall.metadata.push_back("Movie asset: Fall_m.bik / Fall_f.bik");
    ending.push_back(std::move(fall));
    Beat new_beginnings;
    new_beginnings.kind = BeatKind::Task;
    new_beginnings.title =
        "Cinematic: Theresa rescues Sparrow; time advances to adulthood";
    new_beginnings.subtitle = "Story event";
    new_beginnings.metadata.push_back("Movie asset: New Beginnings.bik");
    ending.push_back(std::move(new_beginnings));
    Beat complete;
    complete.kind = BeatKind::Ending;
    complete.title = "Quest complete";
    ending.push_back(std::move(complete));
    const int ending_step = add_step_node(
        "Childhood ends", "Quest step 11", 0.0f, main_y);
    for (int branch_end : previous_ends) {
        add_link(graph, unique_links, branch_end, ending_step, "");
    }
    append_story_beat_nodes(
        graph, ending, ending_step, 0.0f, main_y,
        dialogue_count, unique_links);

    graph.flow_steps = 11;
    graph.dialogue_lines = dialogue_count;
    for (const GraphNode& node : graph.nodes) {
        for (const std::string& metadata : node.metadata) {
            if (starts_ci(metadata, "Related audio: ")) ++graph.audio_matches;
        }
    }
    return graph;
}
