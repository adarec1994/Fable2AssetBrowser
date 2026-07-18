std::optional<QuestEvent> describe_fallback_event(
    const std::string& statement) {
    const std::string line = trim(statement);
    if (line.empty() || line.rfind("--", 0) == 0 || line == "end" ||
        line == "else") {
        return std::nullopt;
    }

    QuestEvent event;
    event.source_statement = statement;
    static const std::regex condition_re(
        R"condition(^(?:if|elseif|while)\s+(.+?)(?:\s+then|\s+do)?$)condition");
    std::smatch match;
    if (std::regex_match(line, match, condition_re)) {
        event.kind = QuestEventKind::Condition;
        event.condition = trim(match[1].str());
        event.title = "Evaluate condition: " + humanize(event.condition);
        return event;
    }

    static const std::regex state_assignment_re(
        R"assign(^(?:local\s+)?(self\.[A-Za-z_][A-Za-z0-9_\.]*)\s*=\s*(.+)$)assign");
    if (std::regex_match(line, match, state_assignment_re)) {
        event.kind = QuestEventKind::ActorState;
        event.target = match[1].str();
        event.title = "Set " + humanize(event.target);
        event.properties.push_back("Value: " + trim(match[2].str()));
        return event;
    }

    static const std::regex call_re(
        R"call(([A-Za-z_][A-Za-z0-9_\.:]*)\s*\()call");
    static const std::unordered_set<std::string> ignored_calls = {
        "print", "assert", "error", "type", "tonumber", "tostring",
        "pairs", "ipairs", "next", "select", "unpack",
    };
    std::vector<std::string> calls;
    for (std::sregex_iterator it(line.begin(), line.end(), call_re), end;
         it != end; ++it) {
        const std::string call = (*it)[1].str();
        if (ignored_calls.count(lower_ascii(call))) continue;
        if (std::find(calls.begin(), calls.end(), call) == calls.end()) {
            calls.push_back(call);
        }
    }
    if (calls.empty()) return std::nullopt;
    event.kind = QuestEventKind::ScriptCall;
    event.target = calls.front();
    event.title = "Run " + humanize(calls.front());
    for (const std::string& call : calls) {
        event.properties.push_back("Call: " + call);
    }
    return event;
}
