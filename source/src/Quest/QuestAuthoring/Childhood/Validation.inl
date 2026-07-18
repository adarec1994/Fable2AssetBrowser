bool validate_childhood_skip_patch(const AuthoredQuest& quest,
                                   std::string& error) {
    error.clear();
    if (!IsValidQuestId(quest.quest_id)) {
        error = "The quest patch ID is invalid.";
        return false;
    }
    if (quest.quest_title.empty()) {
        error = "Enter the hold-A prompt title.";
        return false;
    }
    if (!quest.prerequisites.empty()) {
        error = "The childhood skip patch already has a fixed QC010 lifetime.";
        return false;
    }
    for (const AuthoredNode& node : quest.nodes) {
        if (IsPrerequisiteNode(node.kind)) {
            error = "Remove prerequisite nodes from the childhood skip patch.";
            return false;
        }
    }

    std::vector<const AuthoredNode*> flow;
    if (!ordered_flow(quest, flow, error)) return false;
    const AuthoredNodeKind expected[] = {
        AuthoredNodeKind::QuestStart,
        AuthoredNodeKind::ApproachNpc,
        AuthoredNodeKind::HoldInteraction,
        AuthoredNodeKind::SkipChildhoodEnding,
    };
    if (flow.size() != std::size(expected)) {
        error =
            "Connect: Quest start -> Approach entity -> Hold A prompt -> "
            "Skip childhood to ending.";
        return false;
    }
    for (std::size_t i = 0; i < flow.size(); ++i) {
        if (flow[i]->kind != expected[i]) {
            error = "The childhood skip patch nodes are not connected in the "
                    "required order.";
            return false;
        }
    }

    const AuthoredNode& approach = *flow[1];
    const AuthoredNode& prompt = *flow[2];
    if (!approach.entity.valid() ||
        !IsValidQuestId(approach.entity.entity_name)) {
        error =
            "Create, place, and assign a named entity in the Approach "
            "entity node.";
        return false;
    }
    if (!approach.entity.authored_instance) {
        error =
            "The childhood skip patch requires a newly placed named entity.";
        return false;
    }
    if (!contains_ci_ascii(approach.entity.level_id, "bwsslums") &&
        !contains_ci_ascii(approach.entity.level_id,
                           "bowerstoneslums")) {
        error = "Place the entity in Bowerstone Slums (BWSSlums).";
        return false;
    }
    if (approach.approach_radius <= 0.0f) {
        error = "The approach radius must be greater than zero.";
        return false;
    }
    if (prompt.text.empty()) {
        error = "Enter the text shown by the Hold A prompt node.";
        return false;
    }
    return true;
}
