bool ValidateSimpleQuest(const AuthoredQuest& quest, std::string& error) {
    if (is_childhood_skip_graph(quest)) {
        return validate_childhood_skip_patch(quest, error);
    }
    error.clear();
    if (!IsValidQuestId(quest.quest_id)) {
        error = "The quest ID is invalid.";
        return false;
    }
    if (quest.quest_title.empty()) {
        error = "Enter a quest title.";
        return false;
    }
    for (const AuthoredNode& prerequisite : quest.prerequisites) {
        if (!validate_prerequisite(prerequisite, error)) return false;
    }



    for (const AuthoredNode& node : quest.nodes) {
        if (IsPrerequisiteNode(node.kind) &&
            !validate_prerequisite(node, error)) return false;
    }

    std::vector<const AuthoredNode*> flow;
    if (!ordered_flow(quest, flow, error)) return false;
    const AuthoredNodeKind expected[] = {
        AuthoredNodeKind::QuestStart,
        AuthoredNodeKind::ApproachNpc,
        AuthoredNodeKind::Dialogue,
        AuthoredNodeKind::AcceptQuest,
        AuthoredNodeKind::ObtainItem,
        AuthoredNodeKind::ReturnToNpc,
        AuthoredNodeKind::CompleteQuest,
    };
    if (flow.size() != std::size(expected)) {
        error = "For this basic quest, connect: Quest start -> Approach NPC -> "
                "Dialogue -> Accept quest -> Obtain item -> Return to NPC -> "
                "Complete quest.";
        return false;
    }
    for (std::size_t i = 0; i < flow.size(); ++i) {
        if (flow[i]->kind != expected[i]) {
            error = "The basic quest nodes are not connected in the required order.";
            return false;
        }
    }
    for (const AuthoredNode& prerequisite : quest.nodes) {
        if (!IsPrerequisiteNode(prerequisite.kind)) continue;
        const bool connected = std::any_of(
            quest.links.begin(), quest.links.end(),
            [&](const AuthoredLink& link) {
                return link.from_node == prerequisite.id &&
                       link.to_node == flow.front()->id;
            });
        if (!connected) {
            error = "Connect every prerequisite to the first quest node.";
            return false;
        }
    }

    const AuthoredNode& approach = *flow[1];
    const AuthoredNode& dialogue = *flow[2];
    const AuthoredNode& accept = *flow[3];
    const AuthoredNode& obtain = *flow[4];
    const AuthoredNode& returning = *flow[5];
    if (!approach.entity.valid() ||
        !IsValidQuestId(approach.entity.entity_name)) {
        error = "Select the NPC for the Approach NPC node.";
        return false;
    }
    if (approach.approach_radius <= 0.0f) {
        error = "The NPC approach radius must be greater than zero.";
        return false;
    }
    if (!dialogue.entity.valid() || dialogue.text.empty()) {
        error = "Select a speaker and enter text in the Dialogue node.";
        return false;
    }
    if (accept.text.empty()) {
        error = "Enter the question shown by the Accept quest node.";
        return false;
    }
    if (!obtain.item.valid() || !obtain.item.source.valid() ||
        obtain.item_count < 1 || obtain.text.empty()) {
        error = "Complete the item, source, amount, and objective in the Obtain item node.";
        return false;
    }
    if (!returning.entity.valid() || returning.text.empty()) {
        error = "Select an NPC and enter objective text in the Return to NPC node.";
        return false;
    }
    if (returning.remove_item && !returning.item.valid()) {
        error = "Select the item removed by the Return to NPC node.";
        return false;
    }
    for (const AuthoredReward& reward : quest.rewards) {
        if (reward.kind == QuestRewardKind::Item) {
            if (!reward.item.valid()) {
                error = "Select the item used by every item reward.";
                return false;
            }
            if (reward.item_count < 1) {
                error = "An item reward amount must be at least one.";
                return false;
            }
        } else if (reward.kind != QuestRewardKind::Morality &&
                   reward.kind != QuestRewardKind::Purity &&
                   reward.amount < 0) {
            error = std::string(QuestRewardKindName(reward.kind)) +
                    " cannot be negative.";
            return false;
        }
    }
    return true;
}
