void merge_narrative(Narrative& target, const Narrative& source) {
    target.actions.insert(target.actions.end(), source.actions.begin(),
                          source.actions.end());
    target.supplemental_events.insert(target.supplemental_events.end(),
                                      source.supplemental_events.begin(),
                                      source.supplemental_events.end());
    for (int value : source.transitions) {
        if (std::find(target.transitions.begin(), target.transitions.end(), value) ==
            target.transitions.end()) target.transitions.push_back(value);
    }
    for (const auto& value : source.entities) append_unique(target.entities, value);
    for (const auto& value : source.locations) append_unique(target.locations, value);
    for (const auto& value : source.markers) append_unique(target.markers, value);
    for (const auto& value : source.coordinates) append_unique(target.coordinates, value);
    for (const auto& value : source.self_calls) append_unique(target.self_calls, value);
    target.terminal = target.terminal || source.terminal;
}

Narrative collect_narrative(const std::vector<std::string>& lines,
                            std::size_t begin, std::size_t end,
                            const std::string& attached_entity,
                            const ScriptFacts& facts,
                            const ReferenceCatalog& catalog,
                            std::size_t action_limit = 24,
                            const std::string& source_class = {},
                            const std::string& source_method = {},
                            int source_state = -1) {
    Narrative result;
    static const std::regex self_call_re(R"(self:([A-Za-z_][A-Za-z0-9_]*)\s*\()");
    for (std::size_t i = begin; i < end && i < lines.size();) {
        const std::size_t source_line = i + 1;
        auto [statement, last] = gather_statement(lines, i, end);
        i = last + 1;
        if (statement.empty() || statement.rfind("--", 0) == 0) continue;

        for (std::sregex_iterator it(statement.begin(), statement.end(), self_call_re),
             finish; it != finish; ++it) {
            append_unique(result.self_calls, (*it)[1].str());
        }
        for (const auto& pair : facts.entities) {
            if (statement.find(pair.first) != std::string::npos) {
                append_unique(result.entities, humanize(pair.second));
            }
        }
        if (statement.find("QuestManager.HeroEntity") != std::string::npos ||
            statement.find("GetPlayerHero()") != std::string::npos) {
            append_unique(result.entities, "Hero");
        }

        const std::vector<std::string> strings = quoted_strings(statement);
        if (statement.find("GetEntityWithName") != std::string::npos &&
            !strings.empty()) append_unique(result.entities, humanize(strings.front()));
        if (statement.find("StartNewEntityThread") != std::string::npos &&
            !strings.empty()) append_unique(result.entities, humanize(strings.front()));
        if (contains_ci(statement, "Marker") && !strings.empty()) {
            append_unique(result.markers, strings.back());
        }
        if (statement.find("LoadAndWaitForLevel") != std::string::npos ||
            statement.find("Debug.LoadLevel") != std::string::npos ||
            statement.find("TeleportToLevel") != std::string::npos) {
            for (const std::string& value : strings) {
                if (!value.empty()) append_unique(result.locations, humanize(value));
            }
        }
        for (const std::string& value : strings) {
            if (value.size() < 4) continue;
            for (const std::string& level : catalog.level_assets) {
                if (contains_ci(level, value)) {
                    append_unique(result.locations, humanize(value));
                    break;
                }
            }
        }
        if (const auto coordinate = coordinate_for(statement, facts)) {
            append_unique(result.coordinates, format_coordinate(*coordinate));
        }

        NarrativeAction action = describe_statement(
            statement, attached_entity, facts, catalog);
        action.event.title = action.summary;
        action.event.source_class = source_class;
        action.event.source_method = source_method;
        action.event.source_state = source_state;
        action.event.source_line = source_line;
        if (!action.summary.empty() &&
            action.event.kind == QuestEventKind::Unknown) {
            action.event.kind = QuestEventKind::ScriptCall;
        }
        if (action.transition) {
            int target = *action.transition;
            if (target != -2147483647 &&
                std::find(result.transitions.begin(), result.transitions.end(), target) ==
                    result.transitions.end()) result.transitions.push_back(target);
        }
        if (!action.summary.empty() && result.actions.size() < action_limit) {
            result.terminal = result.terminal || action.terminal;
            result.actions.push_back(std::move(action));
        } else if (!action.summary.empty()) {
            result.terminal = result.terminal || action.terminal;
            result.supplemental_events.push_back(std::move(action.event));
        } else if (auto fallback = describe_fallback_event(statement)) {
            fallback->source_class = source_class;
            fallback->source_method = source_method;
            fallback->source_state = source_state;
            fallback->source_line = source_line;
            result.supplemental_events.push_back(std::move(*fallback));
        }
    }
    return result;
}
