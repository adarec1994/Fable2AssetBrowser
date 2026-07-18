Graph build_frankenbride_timeline(const Graph& technical,
                                  const ReferenceCatalog& references) {
    Graph graph;
    graph.title = technical.title;
    std::set<std::tuple<int, int, std::string>> unique_links;

    auto add_node = [&](NodeKind kind, std::string badge,
                        std::string title, std::string subtitle,
                        std::vector<std::string> details,
                        float x, float& y) {
        GraphNode node;
        node.id = int(graph.nodes.size()) + 1;
        node.kind = kind;
        node.badge = std::move(badge);
        node.title = std::move(title);
        node.subtitle = std::move(subtitle);
        node.details = std::move(details);
        node.x = x;
        node.y = y;
        y += estimated_node_height(node) + 105.0f;
        graph.nodes.push_back(std::move(node));
        return graph.nodes.back().id;
    };

    auto add_dialogue = [&](const std::string& title,
                            const std::string& scene,
                            const std::vector<std::pair<std::string,
                                                       std::string>>& lines,
                            float x, float& y) {
        GraphNode node;
        node.id = int(graph.nodes.size()) + 1;
        node.kind = NodeKind::Thread;
        node.badge = "Dialogue";
        node.title = title;
        node.subtitle = scene.empty() ? std::string() : "Scene: " + scene;
        node.x = x;
        node.y = y;
        for (const auto& line : lines) {
            std::string text = reference_text(references, line.second);
            if (text.empty()) text = "[Missing text: " + line.second + "]";
            node.details.push_back(line.first + ": \"" + text + "\"");
            append_dialogue_metadata(node, references, line.second);
            ++graph.dialogue_lines;
        }
        y += estimated_node_height(node) + 105.0f;
        graph.nodes.push_back(std::move(node));
        return graph.nodes.back().id;
    };

    auto connect = [&](int from, int to, const std::string& label = {}) {
        add_link(graph, unique_links, from, to, label);
    };

    float main_y = 0.0f;
    std::string quest_name = reference_text(
        references, "TEXT_QUEST_QO570_NAME");
    if (quest_name.empty()) quest_name = "Love Hurts";
    const int root = add_node(
        NodeKind::Quest, "Quest start",
        quest_name + " - Resurrect Lady Grey",
        {},
        {"Quest: QO570 Franken Bride",
         "Primary actors: Hero, Victor (the Grave Keeper), Lady Grey",
         "The final choice is made by leaving or remaining in the laboratory."},
        0.0f, main_y);
    int previous = root;

    auto add_step = [&](const std::string& badge, const std::string& title,
                        std::vector<std::string> details) {
        const int id = add_node(NodeKind::State, badge, title, {},
                                std::move(details), 0.0f, main_y);
        connect(previous, id);
        previous = id;
        return id;
    };
    auto add_main_dialogue = [&](const std::string& title,
                                 const std::string& scene,
                                 const std::vector<std::pair<std::string,
                                                            std::string>>& lines) {
        const int id = add_dialogue(title, scene, lines, 0.0f, main_y);
        connect(previous, id);
        previous = id;
        return id;
    };
    auto add_main_action = [&](const std::string& badge,
                               const std::string& title,
                               std::vector<std::string> details) {
        const int id = add_node(NodeKind::Action, badge, title, {},
                                std::move(details), 0.0f, main_y);
        connect(previous, id);
        previous = id;
        return id;
    };

    add_step("Quest step 1", "Meet the Grave Keeper",
             {"Location: Victor's mansion, Bowerstone Cemetery",
              "Interact with the front door and speak through the peephole."});
    add_main_dialogue(
        "Victor answers from behind the door", "GK Intro Pt 1",
        {{"Victor", "TEXT_QUEST_QO570_V2_INTRO_10"},
         {"Victor", "TEXT_QUEST_QO570_V2_INTRO_20"}});
    add_main_dialogue(
        "Victor offers the quest", "GK Intro Pt 2",
        {{"Victor", "TEXT_QUEST_QO570_V2_INTRO_30"},
         {"Victor", "TEXT_QUEST_QO570_V2_INTRO_40"},
         {"Victor", "TEXT_QUEST_QO570_V2_INTRO_50"},
         {"Victor", "TEXT_QUEST_QO570_V2_INTRO_60"}});
    add_main_dialogue(
        "Accept Victor's scientific expedition", "GK Accept Quest",
        {{"Victor", "TEXT_QUEST_QO570_V2_ACCEPT_QUEST_10"},
         {"Victor", "TEXT_QUEST_QO570_V2_ACCEPT_QUEST_20"},
         {"Victor", "TEXT_QUEST_QO570_V2_ACCEPT_QUEST_30"}});

    std::vector<std::string> lower_body_details = {
        "Location: Hobbe Cave in Rookridge",
        "Level asset: Caves\\Dunecrest\\HobbeCave",
    };
    append_world_placement_details(lower_body_details, references,
                                   "QO570_DigSpot");
    lower_body_details.push_back(
        "Dig there and collect ZombieBrideLegs.");
    lower_body_details.push_back(
        "Return to Victor in Bowerstone Cemetery.");
    add_step("Quest step 2", "Recover the lower body",
             std::move(lower_body_details));
    add_main_action(
        "Objective", "Dig up the first body part and return it to Victor",
        {"Victor leaves the mansion door open after the lower body is found.",
         "The Hero hands over ZombieBrideLegs."});
    add_main_dialogue(
        "Victor reveals that the body is Lady Grey", "Give GK Lower Body",
        {{"Victor", "TEXT_QUEST_QO570_V2_BODY_ONE_10"},
         {"Victor", "TEXT_QUEST_QO570_V2_BODY_ONE_20"},
         {"Victor", "TEXT_QUEST_QO570_V2_BODY_ONE_30"},
         {"Victor", "TEXT_QUEST_QO570_V2_BODY_ONE_40"},
         {"Victor", "TEXT_QUEST_QO570_V2_BODY_ONE_50"},
         {"Victor", "TEXT_QUEST_QO570_V2_BODY_ONE_60"}});

    const int gender_branch_from = previous;
    float female_y = main_y;
    const int female_line = add_dialogue(
        "If female - Victor comments on the Hero", "GK Extra Line If Female",
        {{"Victor", "TEXT_QUEST_QO570_V2_WAIT_THREE_FEMALE"}},
        380.0f, female_y);
    connect(gender_branch_from, female_line, "Female Hero");
    main_y = female_y + 45.0f;
    previous = female_line;

    const int upper_body_step = add_step(
        "Quest step 3", "Recover the upper body",
        {"Location: Twinblade's Tomb, between Bloodstone and Wraithmarsh",
         "Level asset: Tombs\\Wraithmarsh\\WraithmarshToBloodstoneTomb",
         "Open QO570_Coffin_V_3 and collect ZombieBrideTorso and QO570_Note2.",
         "Return to Victor in Bowerstone Cemetery."});
    connect(gender_branch_from, upper_body_step, "Male Hero");
    add_main_action(
        "Objective", "Open the coffin and bring Lady Grey's torso to Victor",
        {"A Hollow Men creature generator is triggered inside the tomb.",
         "The Hero hands over ZombieBrideTorso."});
    add_main_dialogue(
        "Victor receives Lady Grey's upper body", "Give GK Upper Body",
        {{"Victor", "TEXT_QUEST_QO570_V2_BODY_TWO_10"},
         {"Victor", "TEXT_QUEST_QO570_V2_BODY_TWO_20"},
         {"Victor", "TEXT_QUEST_QO570_V2_BODY_TWO_30"},
         {"Victor", "TEXT_QUEST_QO570_V2_BODY_TWO_40"},
         {"Victor", "TEXT_QUEST_QO570_V2_BODY_TWO_50"}});

    add_step("Quest step 4", "Recover Lady Grey's head",
             {"Location: Lady Grey's Tomb, reached through Fairfax Gardens",
              "Level asset: Tombs\\BWSCemetery\\LadyGreysTomb",
              "Follow QO570_LastSarcMarker and open the marked sarcophagus.",
              "Return to Victor's laboratory in Bowerstone Cemetery."});
    add_main_action(
        "Objective", "Open the sarcophagus and return Lady Grey's head",
        {"The tomb portcullis unlocks after the sarcophagus is opened.",
         "Victor waits in the basement laboratory with the body prepared."});
    add_main_dialogue(
        "Victor receives the final body part", "Give GK The Head",
        {{"Victor", "TEXT_QUEST_QO570_V2_BODY_THREE_10"}});

    add_step("Quest step 5", "Resurrect Lady Grey",
             {"Location: Victor's basement laboratory, Bowerstone Cemetery",
              "The laboratory door locks and fast travel is blocked during the scene."});
    add_main_dialogue(
        "Victor explains the resurrection and love spell",
        "The Resurrection Scene",
        {{"Victor", "TEXT_QUEST_QO570_V2_LAB_10"},
         {"Victor", "TEXT_QUEST_QO570_V2_LAB_20"},
         {"Victor", "TEXT_QUEST_QO570_V2_LAB_30"},
         {"Victor", "TEXT_QUEST_QO570_V2_LAB_40"},
         {"Victor", "TEXT_QUEST_QO570_V2_LAB_50"},
         {"Victor", "TEXT_QUEST_QO570_V2_LAB_70"},
         {"Victor", "TEXT_QUEST_QO570_V2_LAB_80"}});
    add_main_action(
        "Story event", "Victor activates the Table of Life; Lady Grey rises",
        {"Victor moves around the table and plays the 'She Is Alive' animation.",
         "Lady Grey plays 'Table Of Life Out Of', rises, and moves into position.",
         "Music: MUSIC_QO570_RESURRECTION_FRANKENWIFE_01"});
    add_main_dialogue(
        "The love spell makes Lady Grey fall for the Hero",
        "GK End Scene Pt 4",
        {{"Victor", "TEXT_QUEST_QO570_V2_RESURRECTED_10"},
         {"Victor", "TEXT_QUEST_QO570_V2_RESURRECTED_20"},
         {"Lady Grey", "TEXT_QUEST_QO570_V2_RESURRECTED_30"},
         {"Victor", "TEXT_QUEST_QO570_V2_RESURRECTED_40"},
         {"Lady Grey", "TEXT_QUEST_QO570_V2_RESURRECTED_50"},
         {"Victor", "TEXT_QUEST_QO570_V2_RESURRECTED_60"}});

    add_step("Quest step 6", "Decide who Lady Grey will love",
             {"A 45-second timer begins when the laboratory door unlocks.",
              "The decision is made through movement; there is no menu prompt."});
    add_main_action(
        "Timed choice", "45 seconds: leave the laboratory or remain with Lady Grey",
        {"Leave through either outside-laboratory trigger before time expires: Lady Grey marries Victor.",
         "Remain until the timer expires: Lady Grey stays in love with the Hero."});
    add_main_dialogue(
        "Lady Grey flirts while Victor begs the Hero to leave",
        "GK End Scene Pt 5",
        {{"Lady Grey", "TEXT_QUEST_QO570_V2_TIMER_10"},
         {"Victor", "TEXT_QUEST_QO570_V2_TIMER_20"},
         {"Lady Grey", "TEXT_QUEST_QO570_V2_TIMER_30"}});

    const int timer_branch_from = previous;
    const std::string variant_text = reference_text(
        references, "TEXT_QUEST_QO570_V2_TIMER_40");
    Beat variant_beat;
    variant_beat.title = "Victor: " + variant_text;
    const std::optional<GenderDialogue> variants = gender_dialogue(variant_beat);
    const std::string male_text = variants
        ? variants->male : variant_text;
    const std::string female_text = variants
        ? variants->female : variant_text;
    const float timer_gender_y = main_y;
    float timer_male_y = timer_gender_y;
    float timer_female_y = timer_gender_y;
    const int timer_male = add_node(
        NodeKind::Thread, "If male", "Victor pleads with a male Hero",
        "Scene: GK End Scene Pt 5", {"Victor: \"" + male_text + "\""},
        -380.0f, timer_male_y);
    append_dialogue_metadata(graph.nodes.back(), references,
                             "TEXT_QUEST_QO570_V2_TIMER_40_HM");
    ++graph.dialogue_lines;
    const int timer_female = add_node(
        NodeKind::Thread, "If female", "Victor pleads with a female Hero",
        "Scene: GK End Scene Pt 5", {"Victor: \"" + female_text + "\""},
        380.0f, timer_female_y);
    append_dialogue_metadata(graph.nodes.back(), references,
                             "TEXT_QUEST_QO570_V2_TIMER_40_HF");
    ++graph.dialogue_lines;
    connect(timer_branch_from, timer_male, "Male Hero");
    connect(timer_branch_from, timer_female, "Female Hero");
    main_y = std::max(timer_male_y, timer_female_y) + 45.0f;
    previous = timer_male;
    const int timer_continues = add_main_dialogue(
        "The timer approaches zero", "GK End Scene Pt 5",
        {{"Lady Grey", "TEXT_QUEST_QO570_V2_TIMER_50"},
         {"Victor", "TEXT_QUEST_QO570_V2_TIMER_60"}});
    connect(timer_female, timer_continues);

    const int ending_choice = add_node(
        NodeKind::State, "Ending choice",
        "Does the Hero leave before 45 seconds expire?", {},
        {"YES: leave the laboratory; Lady Grey and Victor marry.",
         "NO: remain for 45 seconds; Lady Grey stays in love with the Hero."},
        0.0f, main_y);
    connect(previous, ending_choice);

    constexpr float kEndingLane = 650.0f;
    float leave_y = main_y;
    const int leave_action = add_node(
        NodeKind::Action, "Leave within 45 seconds",
        "Lady Grey notices Victor after the Hero exits", {},
        {"The laboratory door closes and Lady Grey turns toward Victor.",
         "The love spell transfers to the next person she sees."},
        -kEndingLane, leave_y);
    connect(ending_choice, leave_action, "Yes - leave");
    const int leave_dialogue = add_dialogue(
        "Lady Grey and Victor fall in love", "GK End Scene Pt 7",
        {{"Lady Grey", "TEXT_QUEST_QO570_V2_GOOD_10"},
         {"Victor", "TEXT_QUEST_QO570_V2_GOOD_20"}},
        -kEndingLane, leave_y);
    connect(leave_action, leave_dialogue);
    const int leave_ending = add_node(
        NodeKind::Quest, "Quest complete: Good ending",
        "Lady Grey marries Victor", {},
        {"Victor and Lady Grey marry.",
         "Morality: +10 good",
         "Lady Grey's quest layer is removed after the quest.",
         "Good epilogue; quest complete."},
        -kEndingLane, leave_y);
    connect(leave_dialogue, leave_ending);

    float stay_y = main_y;
    const int stay_action = add_node(
        NodeKind::Action, "Stay for 45 seconds",
        "The timer expires; Victor loses Lady Grey", {},
        {"Lady Grey remains focused on the Hero.",
         "Victor opens the door, sprints out of the laboratory, and later disappears."},
        kEndingLane, stay_y);
    connect(ending_choice, stay_action, "No - stay");
    const int stay_dialogue = add_dialogue(
        "Lady Grey chooses the Hero; Victor despairs", "GK End Scene Pt 6",
        {{"Lady Grey", "TEXT_QUEST_QO570_V2_EVIL_10"},
         {"Victor", "TEXT_QUEST_QO570_V2_EVIL_20"},
         {"Victor", "TEXT_QUEST_QO570_V2_EVIL_30"}},
        kEndingLane, stay_y);
    connect(stay_action, stay_dialogue);
    const int stay_ending = add_node(
        NodeKind::Quest, "Quest complete: Evil ending",
        "Lady Grey remains available to the Hero", {},
        {"Morality: -10 evil",
         "Lady Grey remains alive in the world with normal social behaviours.",
         "The Hero may court and marry her later through the normal relationship system.",
         "Evil epilogue; quest complete."},
        kEndingLane, stay_y);
    connect(stay_dialogue, stay_ending);

    graph.flow_steps = 6;
    for (const GraphNode& node : graph.nodes) {
        for (const std::string& metadata : node.metadata) {
            if (starts_ci(metadata, "Related audio: ")) ++graph.audio_matches;
        }
    }
    return graph;
}
