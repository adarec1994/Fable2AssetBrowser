NarrativeAction describe_statement(const std::string& statement,
                                   const std::string& attached_entity,
                                   const ScriptFacts& facts,
                                   const ReferenceCatalog& catalog) {
    NarrativeAction action;
    action.event.source_statement = statement;
    std::smatch transition;
    static const std::regex transition_re(
        R"(self\.CurrentState\s*=\s*(-?[0-9]+))");
    if (std::regex_search(statement, transition, transition_re)) {
        action.transition = std::stoi(transition[1].str());
    } else if (contains_ci(statement,
                           "self.CurrentState = self.CurrentState + 1")) {
        action.transition = -2147483647;
    }

    const std::vector<std::string> dialogue_calls = {
        "SaySimLine", "SayLine", "TalkToEntity", "PlaySpeech",
        "PostGuildSealMessage", "ShowToasterBoxWithDialogue",
        "DisplayInfoBox", "DisplayMessageBox"};
    for (const std::string& call : dialogue_calls) {
        if (!contains_call(statement, call)) continue;
        const std::vector<std::string> args = call_arguments(statement, call);
        const std::string id = choose_text_id(args, facts, catalog);
        const std::string id_lower = lower_ascii(id);
        const bool display_call = call.rfind("Display", 0) == 0;
        if (display_call && id_lower.rfind("text_", 0) != 0 &&
            localized_text(id, catalog).empty()) {
            continue;
        }
        std::string speaker = "Game UI";
        if (call == "PostGuildSealMessage") speaker = "Guild Seal";
        else if (call.find("Display") == std::string::npos && !args.empty()) {
            speaker = entity_name(args.front(), facts, attached_entity);
        } else if (!attached_entity.empty() && call.find("Display") == std::string::npos) {
            speaker = humanize(attached_entity);
        }
        const std::string text = localized_text(id, catalog);
        if (!text.empty()) {
            action.summary = speaker + " says: \"" + shorten(text, 180) + "\"";
        } else if (!id.empty()) {
            action.summary = speaker + " speaks dialogue " + id;
        } else {
            action.summary = speaker + " speaks";
        }
        if (!id.empty()) action.extra.push_back("Dialogue ID: " + id);
        action.event.kind = QuestEventKind::Dialogue;
        action.event.actor = speaker;
        action.event.dialogue_id = id;
        for (const std::string& audio : related_audio(id, catalog)) {
            action.extra.push_back("Related audio: " + audio);
            ++action.audio_matches;
        }
        action.dialogue_lines = 1;
        return action;
    }

    if (statement.find("StartNewEntityThread") != std::string::npos) {
        const auto args = call_arguments(statement, "StartNewEntityThread");
        if (!args.empty()) action.entity_instance = resolve_string(args[0], facts);
        if (action.entity_instance.empty() && !args.empty()) {
            action.entity_instance = entity_name(args[0], facts, attached_entity);
        }
        if (args.size() >= 2) action.starts_thread = trim(args[1]);
        const std::string actor = action.entity_instance.empty()
            ? humanize(action.starts_thread)
            : humanize(action.entity_instance);
        action.summary = "Start " + actor + "'s scripted behaviour";
        action.event.kind = QuestEventKind::ScriptCall;
        action.event.actor = actor;
        action.event.target = action.starts_thread;
        return action;
    }

    if (statement.find("QuestTracker.SetAsCompleted") != std::string::npos ||
        statement.find("MissionSucceeded = true") != std::string::npos) {
        action.summary = "Complete the quest successfully";
        action.event.kind = QuestEventKind::QuestComplete;
        action.terminal = true;
        return action;
    }
    if (statement.find("MissionFailed = true") != std::string::npos ||
        statement.find("QuestTracker.SetAsFailed") != std::string::npos) {
        action.summary = "Fail the quest";
        action.event.kind = QuestEventKind::QuestFail;
        action.terminal = true;
        return action;
    }
    if (statement.find("QuestTracker.SetAsActive") != std::string::npos) {
        action.summary = "Mark the quest as active";
        action.event.kind = QuestEventKind::QuestStart;
        return action;
    }
    if (statement.find("QuestTracker.SetAsPrimary") != std::string::npos) {
        action.summary = "Make this the primary quest";
        return action;
    }

    const std::vector<std::string> objective_calls = {
        "UpdateObjectiveTag", "SetObjectiveTag", "SetObjectiveLevelAndExit",
        "SetObjectiveLevel",
        "SetObjectiveEntity", "SetObjectiveBreadcrumbRadius",
        "RemoveObjectiveTag", "SetObjectiveAsCompleted"};
    for (const std::string& call : objective_calls) {
        if (statement.find(call) == std::string::npos) continue;
        const auto args = call_arguments(statement, call);
        const std::string id = choose_text_id(args, facts, catalog);
        const std::string text = localized_text(id, catalog);
        if (call == "SetObjectiveEntity") {
            std::size_t target_index = args.empty() ? 0 : args.size() - 1;
            if (args.size() >= 2 &&
                (contains_ci(args.back(), "true") ||
                 contains_ci(args.back(), "false"))) {
                target_index = args.size() - 2;
            }
            const std::string target = args.empty()
                ? "the target entity"
                : entity_name(args[target_index], facts, attached_entity);
            action.summary = "Point the objective marker at " + target;
            action.event.kind = QuestEventKind::ObjectiveTarget;
            action.event.target = target;
            const std::string marker = args.empty()
                ? std::string() : resolve_string(args[target_index], facts);
            if (!marker.empty()) resolve_world_marker(action, catalog, marker);
        } else if (call == "SetObjectiveLevel" ||
                   call == "SetObjectiveLevelAndExit") {
            action.summary = "Set the objective destination to " +
                             humanize(id.empty() ? "the target level" : id);
            action.event.kind = QuestEventKind::ObjectiveTarget;
            action.event.world.level = id;
        } else if (call == "RemoveObjectiveTag") {
            action.summary = "Remove objective: " +
                             (text.empty() ? humanize(id) : text);
            action.event.kind = QuestEventKind::ObjectiveRemove;
        } else if (call == "SetObjectiveAsCompleted") {
            action.summary = "Mark the current objective complete";
            action.event.kind = QuestEventKind::ObjectiveComplete;
        } else {
            action.summary = "Set objective: " +
                             (text.empty() ? humanize(id) : text);
            action.event.kind = QuestEventKind::ObjectiveSet;
        }
        action.event.objective = id;
        if (!id.empty()) action.extra.push_back("Objective ID: " + id);
        return action;
    }

    if (contains_call(statement, "PlayCutscene") ||
        contains_call(statement, "InteractiveCutscene")) {
        const auto strings = quoted_strings(statement);
        const std::string name = strings.empty() ? std::string() : strings.front();
        action.summary = name.empty() ? "Play the configured cutscene"
                                      : "Play cutscene " + humanize(name);
        action.event.kind = QuestEventKind::Cutscene;
        action.event.cutscene_id = name;
        if (!name.empty()) action.extra.push_back("Cutscene ID: " + name);
        if (const CutsceneReference* reference =
                cutscene_reference(name, catalog)) {
            if (!reference->speakers.empty()) {
                std::string speakers;
                for (const std::string& speaker : reference->speakers) {
                    if (!speakers.empty()) speakers += ", ";
                    speakers += humanize(speaker);
                }
                action.extra.push_back("Participants/NPCs: " + speakers);
            }
            std::size_t spoken_lines = reference->dialogue_tags.size();
            if (!reference->timeline.empty()) {
                spoken_lines = 0;
                for (const CutsceneTimelineEntry& entry :
                     reference->timeline) {
                    if (entry.kind == CutsceneTimelineKind::Dialogue) {
                        ++spoken_lines;
                    }
                }
            }
            action.dialogue_lines = spoken_lines;
            if (spoken_lines > 0) {
                action.summary += " (" +
                    std::to_string(spoken_lines) +
                    (spoken_lines == 1
                         ? " spoken line)" : " spoken lines)");
            }

            auto append_dialogue = [&](const std::string& tag,
                                       const std::string& known_speaker) {
                const std::string text = localized_text(tag, catalog);
                std::string speaker = humanize(known_speaker);
                if (speaker.empty()) {
                    for (const CutsceneDialogueLine& line :
                         reference->dialogue_lines) {
                        if (lower_ascii(line.text_tag) == lower_ascii(tag)) {
                            speaker = humanize(line.speaker);
                            break;
                        }
                    }
                }
                action.extra.push_back(
                    "Dialogue [" + tag + "]" +
                    (speaker.empty() ? std::string() : " " + speaker) +
                    ": " + shorten(text.empty() ? humanize(tag) : text, 220));
                for (const std::string& audio : related_audio(tag, catalog)) {
                    action.extra.push_back("Related audio: " + audio);
                    ++action.audio_matches;
                }
            };

            if (!reference->timeline.empty()) {
                for (const CutsceneTimelineEntry& entry :
                     reference->timeline) {
                    if (entry.kind == CutsceneTimelineKind::Dialogue) {
                        append_dialogue(entry.text_tag, entry.speaker);
                        continue;
                    }
                    action.extra.push_back(
                        "Actor action: " + entry.description);
                    for (const std::string& detail : entry.details) {
                        action.extra.push_back("Actor detail: " + detail);
                    }
                    for (const std::string& metadata : entry.metadata) {
                        action.extra.push_back(metadata);
                    }
                }
            } else {
                for (const std::string& tag : reference->dialogue_tags) {
                    append_dialogue(tag, {});
                }
            }
        }
        return action;
    }

    if (contains_call(statement, "SetLookAtCamera")) {
        const bool childhood_castle_view =
            contains_ci(attached_entity, "QC010 Rose") &&
            statement.find("dof_focus_position") != std::string::npos;
        action.summary = childhood_castle_view
            ? "Camera event: Sparrow is prompted to look at Castle Fairfax"
            : "Camera event: The camera focuses on a story point";
        action.event.kind = QuestEventKind::Camera;
        if (const auto source = parse_coordinate_after(
                statement, "source_position")) {
            action.extra.push_back(
                "Camera position: " + format_coordinate(*source));
        }
        if (const auto target = parse_coordinate_after(
                statement, "target_position")) {
            action.extra.push_back(
                "Camera focus: " + format_coordinate(*target));
            set_world_position(action.event, *target);
        }
        if (const auto fov = parse_number_after(statement, "fov")) {
            action.extra.push_back(
                "Field of view: " + format_number(*fov) + " degrees");
        }
        if (childhood_castle_view) {
            action.extra.push_back(
                "The look-at prompt lets the player focus on the castle "
                "before Rose continues speaking.");
        }
        return action;
    }
    if (statement.find("GUI.PlayMovie") != std::string::npos ||
        statement.find("GUI.PlayLocalisedMovie") != std::string::npos) {
        const auto strings = quoted_strings(statement);
        const std::string movie = strings.empty() ? "movie" : strings.front();
        action.summary = "Play movie " + humanize(movie);
        action.event.kind = QuestEventKind::Cutscene;
        action.event.cutscene_id = movie;
        action.extra.push_back("Movie asset: " + movie);
        return action;
    }

    const std::vector<std::string> level_calls = {
        "LoadAndWaitForLevel", "Debug.LoadLevel", "TeleportToLevel"};
    for (const std::string& call : level_calls) {
        if (statement.find(call) == std::string::npos) continue;
        const auto args = call_arguments(statement, call);
        std::vector<std::string> places;
        for (const std::string& arg : args) {
            const std::string place = resolve_string(arg, facts);
            if (!place.empty()) append_unique(places, humanize(place));
        }
        std::string destination;
        for (const std::string& place : places) {
            if (!destination.empty()) destination += " / ";
            destination += place;
        }
        action.summary = "Travel to " +
                         (destination.empty() ? std::string("the next area")
                                              : destination);
        action.event.kind = QuestEventKind::Travel;
        action.event.world.level = destination;
        return action;
    }

    if (statement.find("Layers.ActivateLayer") != std::string::npos ||
        statement.find("Layers.DeactivateLayer") != std::string::npos) {
        const bool activate = statement.find("ActivateLayer") != std::string::npos &&
                              statement.find("DeactivateLayer") == std::string::npos;
        const auto strings = quoted_strings(statement);
        const std::string layer = strings.empty() ? "quest layer" : strings.front();
        action.summary = std::string(activate ? "Enable " : "Disable ") +
                         humanize(layer) + " in the level";
        action.extra.push_back("Layer ID: " + layer);
        action.event.kind = QuestEventKind::LayerState;
        action.event.target = layer;
        action.event.properties.push_back(activate ? "enabled" : "disabled");
        return action;
    }

    if (statement.find("GUI.SetTimer") != std::string::npos ||
        statement.find("QuestManager.NewTimer") != std::string::npos) {
        const std::string call = statement.find("GUI.SetTimer") != std::string::npos
            ? "GUI.SetTimer" : "QuestManager.NewTimer";
        const auto args = call_arguments(statement, call);
        std::string timer_name;
        if (call == "GUI.SetTimer" && !args.empty()) {
            timer_name = resolve_string(args.front(), facts);
        }
        std::optional<double> duration;
        for (auto it = args.rbegin(); it != args.rend(); ++it) {
            duration = resolve_number(*it, facts);
            if (duration) break;
        }
        action.summary = "Start " +
            (timer_name.empty() ? std::string("quest timer")
                                : humanize(timer_name) + " timer") +
            (duration ? " for " + format_number(*duration) + " seconds"
                      : std::string());
        action.event.kind = QuestEventKind::TimerStart;
        action.event.target = timer_name;
        action.event.duration_seconds = duration;
        return action;
    }
