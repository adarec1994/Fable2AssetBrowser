std::string condition_description(std::string expression,
                                  const ScriptFacts& facts,
                                  const std::string& attached_entity) {
    expression = trim(std::move(expression));
    if (expression.empty()) return "the scripted condition is met";
    while (!expression.empty() && expression.back() == ')') {
        const std::size_t opens = std::count(expression.begin(), expression.end(), '(');
        const std::size_t closes = std::count(expression.begin(), expression.end(), ')');
        if (closes <= opens) break;
        expression.pop_back();
        expression = trim(std::move(expression));
    }
    if (expression.rfind("not ", 0) == 0) {
        return humanize(expression.substr(4)) + " becomes false";
    }
    if (expression == "true") return "the next scripted update begins";
    if (expression.find("IsAvailableToSayLine") != std::string::npos) {
        const auto args = call_arguments(expression, "IsAvailableToSayLine");
        const std::string speaker = args.empty()
            ? (attached_entity.empty() ? "the NPC"
                                       : humanize(attached_entity))
            : entity_name(args.front(), facts, attached_entity);
        return speaker + " is ready to speak";
    }
    if (expression.find("IsMessageSentTo") != std::string::npos) {
        const auto args = call_arguments(expression, "IsMessageSentTo");
        const std::string event = args.empty() ? "the required event"
                                               : event_description(args[0]);
        const std::string target = args.size() >= 2
            ? entity_name(args[1], facts, attached_entity)
            : humanize(attached_entity);
        return event + (target.empty() ? std::string(" occurs")
                                       : " involving " + target);
    }
    if (expression.find("IsTriggerEntityInsideTrigger") != std::string::npos) {
        const std::string call = expression.find("IsTriggerEntityInsideTriggerVolume") !=
                                         std::string::npos
            ? "IsTriggerEntityInsideTriggerVolume"
            : "IsTriggerEntityInsideTrigger";
        const auto args = call_arguments(expression, call);
        if (args.size() >= 2) {
            return entity_name(args[1], facts, attached_entity) + " enters " +
                   entity_name(args[0], facts, attached_entity);
        }
    }
    if (expression.find("==") != std::string::npos) {
        const std::size_t op = expression.find("==");
        return humanize(expression.substr(0, op)) + " equals " +
               humanize(expression.substr(op + 2));
    }
    if (expression.find("~=") != std::string::npos) {
        const std::size_t op = expression.find("~=");
        return humanize(expression.substr(0, op)) + " no longer equals " +
               humanize(expression.substr(op + 2));
    }
    return humanize(expression) + " becomes true";
}
