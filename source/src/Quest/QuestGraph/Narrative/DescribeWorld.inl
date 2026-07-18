    if (statement.find("GUI.RemoveTimer") != std::string::npos ||
        statement.find("GUI.StopTimer") != std::string::npos) {
        const std::string call = statement.find("GUI.RemoveTimer") != std::string::npos
            ? "GUI.RemoveTimer" : "GUI.StopTimer";
        const auto args = call_arguments(statement, call);
        const std::string timer_name = args.empty()
            ? std::string() : resolve_string(args.front(), facts);
        action.summary = "Stop " +
            (timer_name.empty() ? std::string("quest timer")
                                : humanize(timer_name) + " timer");
        action.event.kind = QuestEventKind::TimerStop;
        action.event.target = timer_name;
        return action;
    }
    if ((statement.find("GetTime()") != std::string::npos ||
         statement.find(":GetTime()") != std::string::npos) &&
        (statement.find("== 0") != std::string::npos ||
         statement.find("<= 0") != std::string::npos)) {
        action.summary = "Wait until the quest timer expires";
        action.event.kind = QuestEventKind::TimerWait;
        action.event.condition = "timer reaches zero";
        return action;
    }
    if (statement.find("DiggingSpot.SetAsDiggableWithoutDog") !=
            std::string::npos ||
        statement.find("SetAsDiggableWithoutDog") != std::string::npos) {
        const auto args = call_arguments(statement, "SetAsDiggableWithoutDog");
        const std::string target = args.empty()
            ? humanize(attached_entity)
            : entity_name(args.front(), facts, attached_entity);
        std::string marker = args.empty() ? std::string()
                                          : resolve_string(args.front(), facts);
        if (marker.empty() && !args.empty()) {
            for (const auto& entity : facts.entities) {
                if (trim(args.front()) == entity.first) {
                    marker = entity.second;
                    break;
                }
            }
        }
        action.summary = "Enable digging at " + target;
        action.event.kind = QuestEventKind::DigSpotEnable;
        action.event.target = target;
        if (!marker.empty()) resolve_world_marker(action, catalog, marker);
        return action;
    }
    if (statement.find("DiggingSpot.IsActive") != std::string::npos) {
        const auto args = call_arguments(statement, "DiggingSpot.IsActive");
        const std::string target = args.empty()
            ? humanize(attached_entity)
            : entity_name(args.front(), facts, attached_entity);
        std::string marker = args.empty() ? std::string()
                                          : resolve_string(args.front(), facts);
        if (marker.empty() && !args.empty()) {
            for (const auto& entity : facts.entities) {
                if (trim(args.front()) == entity.first) {
                    marker = entity.second;
                    break;
                }
            }
        }
        const bool complete = contains_ci(statement, "not ") ||
                              statement.find("== false") != std::string::npos;
        const bool branch_condition = trim(statement).rfind("if ", 0) == 0 ||
                                      trim(statement).rfind("elseif ", 0) == 0;
        action.summary = complete
            ? "Wait until the player finishes digging at " + target
            : branch_condition
                ? "Check whether the player is digging at " + target
                : "Wait until digging is active at " + target;
        action.event.kind = complete ? QuestEventKind::DigSpotComplete
                                     : QuestEventKind::Condition;
        action.event.target = target;
        action.event.condition = complete ? "digging completed"
                                          : "digging spot active";
        if (!marker.empty()) resolve_world_marker(action, catalog, marker);
        return action;
    }
    if (statement.find("WaitForTimeInSeconds") != std::string::npos) {
        const auto args = call_arguments(statement, "WaitForTimeInSeconds");
        action.summary = "Wait " +
                         (args.empty() ? std::string("for the scripted delay")
                                       : trim(args.front()) + " seconds");
        action.event.kind = QuestEventKind::TimerWait;
        if (!args.empty()) {
            action.event.duration_seconds = resolve_number(args.front(), facts);
        }
        return action;
    }
    if (statement.find("WaitForTriggerToFire") != std::string::npos) {
        const auto args = call_arguments(statement, "WaitForTriggerToFire");
        const std::string trigger = args.empty()
            ? "the trigger"
            : entity_name(args.front(), facts, attached_entity);
        action.summary = "Wait for trigger " + trigger + " to fire";
        action.event.kind = QuestEventKind::Condition;
        action.event.target = trigger;
        action.event.condition = "trigger fires";
        return action;
    }
    if (statement.find("IsDistanceBetweenThingsUnder") != std::string::npos) {
        const auto args = call_arguments(statement, "IsDistanceBetweenThingsUnder");
        if (args.size() >= 3) {
            action.summary = "Wait until " + entity_name(args[0], facts, attached_entity) +
                             " is within " + trim(args[2]) + " of " +
                             entity_name(args[1], facts, attached_entity);
        } else {
            action.summary = "Wait until the required entities are close enough";
        }
        action.event.kind = QuestEventKind::Condition;
        action.event.condition = action.summary;
        return action;
    }
    if (statement.find("IsMessageSentTo") != std::string::npos) {
        const auto args = call_arguments(statement, "IsMessageSentTo");
        const std::string event = args.empty() ? "the required event"
                                               : event_description(args[0]);
        const std::string target = args.size() >= 2
            ? entity_name(args[1], facts, attached_entity)
            : humanize(attached_entity);
        action.summary = "Wait for " + event +
                         (target.empty() ? std::string() : " involving " + target);
        action.event.kind = contains_ci(event, "interaction")
            ? QuestEventKind::Interaction : QuestEventKind::Condition;
        action.event.target = target;
        action.event.condition = event;
        return action;
    }
    if (statement.find("IsLevelLoaded") != std::string::npos) {
        const auto args = call_arguments(statement, "IsLevelLoaded");
        const std::string level = args.empty() ? "the destination"
                                               : resolve_string(args[0], facts);
        action.summary = "Wait for " + humanize(level) + " to finish loading";
        action.event.kind = QuestEventKind::Condition;
        action.event.world.level = level;
        action.event.condition = "level loaded";
        return action;
    }
    if (statement.find("IsTriggerEntityInsideTriggerVolume") != std::string::npos ||
        statement.find("IsTriggerEntityInsideTrigger") != std::string::npos) {
        const std::string call = statement.find("IsTriggerEntityInsideTriggerVolume") !=
                                         std::string::npos
            ? "IsTriggerEntityInsideTriggerVolume"
            : "IsTriggerEntityInsideTrigger";
        const auto args = call_arguments(statement, call);
        if (args.size() >= 2) {
            action.summary = "Wait until " + entity_name(args[1], facts, attached_entity) +
                             " enters " + entity_name(args[0], facts, attached_entity);
        } else {
            action.summary = "Wait until the trigger is entered";
        }
        action.event.kind = QuestEventKind::Condition;
        action.event.condition = action.summary;
        return action;
    }
    if (statement.find("self.Interacted") != std::string::npos ||
        statement.find(".Interacted") != std::string::npos) {
        std::string target = humanize(attached_entity);
        static const std::regex interacted_re(
            R"interact(([A-Za-z_][A-Za-z0-9_\.]*)\.Interacted)interact");
        std::smatch match;
        if (std::regex_search(statement, match, interacted_re)) {
            target = entity_name(match[1].str(), facts, attached_entity);
        }
        action.summary = "Wait for the player to interact with " + target;
        action.event.kind = QuestEventKind::Interaction;
        action.event.actor = "Hero";
        action.event.target = target;
        action.event.condition = "player interacts";
        return action;
    }
    if (statement.find(":WaitFor(") != std::string::npos) {
        std::string condition;
        static const std::regex return_re(
            R"wait(\breturn\s+(.+?)(?:\s+end\s*\)?\s*$|$))wait");
        std::smatch match;
        if (std::regex_search(statement, match, return_re)) {
            condition = match[1].str();
        }
        action.summary = "Wait until " +
            condition_description(condition, facts, attached_entity);
        action.event.kind = contains_ci(condition, "interact")
            ? QuestEventKind::Interaction : QuestEventKind::Condition;
        action.event.condition = condition_description(
            condition, facts, attached_entity);
        if (action.event.kind == QuestEventKind::Interaction) {
            action.event.actor = "Hero";
            action.event.target = humanize(attached_entity);
        }
        return action;
    }

    const std::vector<std::string> marker_calls = {
        "MoveAndRotateEntityToMarkerNamed", "MoveAndRotateToMarkerNamed",
        "MoveToMarker",
        "TeleportToMarker", "TeleportPlayerTo", "SetToMarker"};
    for (const std::string& call : marker_calls) {
        if (statement.find(call) == std::string::npos) continue;
        const auto args = call_arguments(statement, call);
        std::string marker;
        for (const std::string& arg : args) {
            marker = resolve_string(arg, facts);
            if (!marker.empty()) break;
        }
        const bool attached_actor_call = call == "MoveAndRotateToMarkerNamed";
        std::string actor = call == "TeleportPlayerTo"
            ? "Hero" : humanize(attached_entity);
        if (!args.empty() && call != "TeleportPlayerTo" &&
            !attached_actor_call) {
            actor = entity_name(args.front(), facts, attached_entity);
        }
        const bool teleport = contains_ci(call, "Teleport") || call == "SetToMarker";
        action.summary = std::string(teleport ? "Move " : "Guide ") + actor +
                         " to marker " + humanize(marker.empty() ? "target marker" : marker);
        action.event.kind = QuestEventKind::ActorMove;
        action.event.actor = actor;
        action.event.target = marker;
        if (!marker.empty()) resolve_world_marker(action, catalog, marker);
        return action;
    }

    if (statement.find("StartScriptControlledMovement") !=
        std::string::npos) {
        const auto args = call_arguments(
            statement, "StartScriptControlledMovement");
        const std::string actor = args.empty()
            ? humanize(attached_entity)
            : entity_name(args.front(), facts, attached_entity);
        action.event.kind = QuestEventKind::ActorMove;
        action.event.actor = actor;

        const std::size_t marker_call = statement.find("GetPositionOfEntity");
        if (marker_call != std::string::npos) {
            const auto marker_args = call_arguments(
                statement.substr(marker_call), "GetPositionOfEntity");
            const std::string marker = marker_args.empty()
                ? std::string() : resolve_string(marker_args.front(), facts);
            action.summary = "Guide " + actor + " to marker " + humanize(
                marker.empty() ? "target marker" : marker);
            action.event.target = marker;
            if (!marker.empty()) resolve_world_marker(action, catalog, marker);
        } else {
            action.summary = "Move " + actor + " to a world position";
            if (const auto coordinate = coordinate_for(statement, facts)) {
                action.extra.push_back(
                    "Position: " + format_coordinate(*coordinate));
                set_world_position(action.event, *coordinate);
            }
        }
        return action;
    }

    if (statement.find("MoveToPosition") != std::string::npos ||
        statement.find("SetScriptMoveToMode") != std::string::npos ||
        statement.find("TeleportToPosition") != std::string::npos) {
        const std::string call = statement.find("MoveToPosition") != std::string::npos
            ? "MoveToPosition"
            : statement.find("SetScriptMoveToMode") != std::string::npos
                ? "SetScriptMoveToMode"
                : "TeleportToPosition";
        const auto args = call_arguments(statement, call);
        const std::string actor = args.empty()
            ? humanize(attached_entity)
            : entity_name(args.front(), facts, attached_entity);
        action.summary = (call == "TeleportToPosition" ? "Teleport " : "Move ") +
                         actor + " to a world position";
        action.event.kind = QuestEventKind::ActorMove;
        action.event.actor = actor;
        if (const auto coordinate = coordinate_for(statement, facts)) {
            action.extra.push_back("Position: " + format_coordinate(*coordinate));
            set_world_position(action.event, *coordinate);
        }
        return action;
    }

    if (statement.find("Follow") != std::string::npos &&
        statement.find('(') != std::string::npos) {
        const auto args = call_arguments(statement, "Follow");
        const std::string actor = args.empty() ? humanize(attached_entity)
                                               : entity_name(args[0], facts, attached_entity);
        const std::string target = args.size() >= 2
            ? entity_name(args[1], facts, attached_entity)
            : "the target";
        action.summary = actor + " follows " + target;
        action.event.kind = QuestEventKind::ActorMove;
        action.event.actor = actor;
        action.event.target = target;
        return action;
    }

    if (statement.find("PlayAnimation") != std::string::npos ||
        statement.find("ActionPlayAnim") != std::string::npos) {
        const auto strings = quoted_strings(statement);
        const std::string animation = strings.empty() ? "scripted animation"
                                                       : strings.back();
        action.summary = humanize(attached_entity) + " plays animation " +
                         humanize(animation);
        action.extra.push_back("Animation ID: " + animation);
        action.event.kind = QuestEventKind::ActorAnimation;
        action.event.actor = humanize(attached_entity);
        action.event.animation_id = animation;
        return action;
    }
