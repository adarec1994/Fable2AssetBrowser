std::string GenerateEligibilityLua(const AuthoredQuest& quest) {
    std::string start = "ScriptEnum.GAMEFLOW_START";
    std::string end =
        is_childhood_skip_graph(quest)
            ? "ScriptEnum.DebugQC060"
            : "ScriptEnum.GAMEFLOW_END";
    std::vector<std::string> requirements;
    auto append_prerequisite = [&](const AuthoredNode& node) {
        if (node.kind == AuthoredNodeKind::PrerequisiteStoryProgress) {
            if (!node.story_start.empty()) start = node.story_start;
            if (!node.story_end.empty()) end = node.story_end;
            return;
        }
        const std::string expression = prerequisite_expression(node);
        if (!expression.empty()) requirements.push_back(expression);
    };
    for (const AuthoredNode& prerequisite : quest.prerequisites) {
        append_prerequisite(prerequisite);
    }


    for (const AuthoredNode& node : quest.nodes) {
        if (IsPrerequisiteNode(node.kind)) append_prerequisite(node);
    }

    std::string unavailable = "false";
    if (!requirements.empty()) {
        std::ostringstream joined;
        for (std::size_t i = 0; i < requirements.size(); ++i) {
            if (i) joined << " and ";
            joined << '(' << requirements[i] << ')';
        }
        unavailable = "not (" + joined.str() + ")";
    }
    std::ostringstream lua;
    lua << "gameflow:CheckQuestEligibility(\"" << quest.quest_id
        << "\", " << start << ", " << end << ", 0, 0, "
        << unavailable << ")";
    return lua.str();
}
