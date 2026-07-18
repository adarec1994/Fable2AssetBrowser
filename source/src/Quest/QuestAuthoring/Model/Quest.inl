bool IsValidQuestId(const std::string& quest_id) {
    if (quest_id.empty()) return false;
    const unsigned char first = static_cast<unsigned char>(quest_id.front());
    if (!std::isalpha(first) && quest_id.front() != '_') return false;
    return std::all_of(quest_id.begin() + 1, quest_id.end(),
                       [](unsigned char c) {
                           return std::isalnum(c) || c == '_';
                       });
}

AuthoredQuest CreateAuthoredQuest(const std::string& quest_id) {
    AuthoredQuest quest;
    quest.quest_id = quest_id;
    quest.quest_title = quest_id;
    AddAuthoredNode(quest, AuthoredNodeKind::QuestStart, 0.0f, 0.0f);
    return quest;
}

int AddAuthoredNode(AuthoredQuest& quest, AuthoredNodeKind kind,
                    float x, float y) {
    if (kind == AuthoredNodeKind::QuestStart ||
        kind == AuthoredNodeKind::CompleteQuest ||
        kind == AuthoredNodeKind::SkipChildhoodEnding) {
        for (const AuthoredNode& existing : quest.nodes) {
            if (existing.kind == kind) {
                return existing.id;
            }
        }
    }
    AuthoredNode node;
    node.id = quest.next_node_id++;
    node.kind = kind;
    node.x = x;
    node.y = y;
    quest.nodes.push_back(std::move(node));
    return quest.nodes.back().id;
}

bool RemoveAuthoredNode(AuthoredQuest& quest, int node_id) {
    const AuthoredNode* node = FindAuthoredNode(quest, node_id);
    if (node && node->kind == AuthoredNodeKind::QuestStart) return false;
    const bool removes_completion = node &&
        node->kind == AuthoredNodeKind::CompleteQuest;
    const std::size_t old_size = quest.nodes.size();
    quest.nodes.erase(
        std::remove_if(quest.nodes.begin(), quest.nodes.end(),
                       [node_id](const AuthoredNode& node) {
                           return node.id == node_id;
                       }),
        quest.nodes.end());
    if (quest.nodes.size() == old_size) return false;
    if (removes_completion) quest.rewards.clear();
    quest.links.erase(
        std::remove_if(quest.links.begin(), quest.links.end(),
                       [node_id](const AuthoredLink& link) {
                           return link.from_node == node_id ||
                                  link.to_node == node_id;
                       }),
        quest.links.end());
    return true;
}

int AddAuthoredLink(AuthoredQuest& quest, int from_node, int to_node) {
    if (from_node == to_node || !FindAuthoredNode(quest, from_node) ||
        !FindAuthoredNode(quest, to_node)) return 0;
    const AuthoredNodeKind from_kind =
        FindAuthoredNode(quest, from_node)->kind;
    if (from_kind == AuthoredNodeKind::CompleteQuest ||
        from_kind == AuthoredNodeKind::SkipChildhoodEnding) return 0;
    for (const AuthoredLink& link : quest.links) {
        if (link.from_node == from_node && link.to_node == to_node) {
            return link.id;
        }
    }
    AuthoredLink link;
    link.id = quest.next_link_id++;
    link.from_node = from_node;
    link.to_node = to_node;
    quest.links.push_back(link);
    return link.id;
}

bool RemoveAuthoredLink(AuthoredQuest& quest, int link_id) {
    const std::size_t old_size = quest.links.size();
    quest.links.erase(
        std::remove_if(quest.links.begin(), quest.links.end(),
                       [link_id](const AuthoredLink& link) {
                           return link.id == link_id;
                       }),
        quest.links.end());
    return quest.links.size() != old_size;
}

int AddAuthoredPrerequisite(AuthoredQuest& quest, AuthoredNodeKind kind) {
    if (!IsPrerequisiteNode(kind)) return 0;
    AuthoredNode prerequisite;
    prerequisite.id = quest.next_prerequisite_id++;
    prerequisite.kind = kind;
    quest.prerequisites.push_back(std::move(prerequisite));
    return quest.prerequisites.back().id;
}

bool RemoveAuthoredPrerequisite(AuthoredQuest& quest,
                                int prerequisite_id) {
    const std::size_t old_size = quest.prerequisites.size();
    quest.prerequisites.erase(
        std::remove_if(quest.prerequisites.begin(),
                       quest.prerequisites.end(),
                       [prerequisite_id](const AuthoredNode& prerequisite) {
                           return prerequisite.id == prerequisite_id;
                       }),
        quest.prerequisites.end());
    return quest.prerequisites.size() != old_size;
}

int AddAuthoredReward(AuthoredQuest& quest, QuestRewardKind kind) {
    AuthoredReward reward;
    reward.id = quest.next_reward_id++;
    reward.kind = kind;
    quest.rewards.push_back(std::move(reward));
    return quest.rewards.back().id;
}

bool RemoveAuthoredReward(AuthoredQuest& quest, int reward_id) {
    const std::size_t old_size = quest.rewards.size();
    quest.rewards.erase(
        std::remove_if(quest.rewards.begin(), quest.rewards.end(),
                       [reward_id](const AuthoredReward& reward) {
                           return reward.id == reward_id;
                       }),
        quest.rewards.end());
    return quest.rewards.size() != old_size;
}

AuthoredNode* FindAuthoredNode(AuthoredQuest& quest, int node_id) {
    for (AuthoredNode& node : quest.nodes) {
        if (node.id == node_id) return &node;
    }
    return nullptr;
}

const AuthoredNode* FindAuthoredNode(const AuthoredQuest& quest,
                                     int node_id) {
    for (const AuthoredNode& node : quest.nodes) {
        if (node.id == node_id) return &node;
    }
    return nullptr;
}
