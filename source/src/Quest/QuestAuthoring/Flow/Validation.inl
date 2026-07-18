bool ordered_flow(const AuthoredQuest& quest,
                  std::vector<const AuthoredNode*>& ordered,
                  std::string& error) {
    ordered.clear();
    std::vector<const AuthoredNode*> flow;
    std::unordered_map<int, const AuthoredNode*> by_id;
    for (const AuthoredNode& node : quest.nodes) {
        by_id[node.id] = &node;
        if (!IsPrerequisiteNode(node.kind)) flow.push_back(&node);
    }
    if (flow.empty()) {
        error = "Add quest nodes with the graph right-click menu.";
        return false;
    }

    std::unordered_map<int, int> incoming;
    std::unordered_map<int, std::vector<int>> outgoing;
    for (const AuthoredLink& link : quest.links) {
        const auto from = by_id.find(link.from_node);
        const auto to = by_id.find(link.to_node);
        if (from == by_id.end() || to == by_id.end()) {
            error = "The graph contains a link to a missing node.";
            return false;
        }
        if (IsPrerequisiteNode(from->second->kind)) continue;
        if (IsPrerequisiteNode(to->second->kind)) {
            error = "A quest step cannot flow into a prerequisite.";
            return false;
        }
        outgoing[link.from_node].push_back(link.to_node);
        ++incoming[link.to_node];
    }

    const AuthoredNode* root = nullptr;
    for (const AuthoredNode* node : flow) {
        if (incoming[node->id] != 0) continue;
        if (root) {
            error = "Connect the quest nodes into one flow.";
            return false;
        }
        root = node;
    }
    if (!root) {
        error = "The quest flow has no first node.";
        return false;
    }

    std::unordered_set<int> visited;
    const AuthoredNode* current = root;
    while (current) {
        if (!visited.insert(current->id).second) {
            error = "The simple quest flow contains a loop.";
            return false;
        }
        ordered.push_back(current);
        const std::vector<int>& next = outgoing[current->id];
        if (next.size() > 1) {
            error = "The simple quest flow cannot branch yet.";
            return false;
        }
        current = next.empty() ? nullptr : by_id[next.front()];
    }
    if (ordered.size() != flow.size()) {
        error = "Connect every quest node into the flow.";
        return false;
    }
    return true;
}

bool validate_prerequisite(const AuthoredNode& node, std::string& error) {
    switch (node.kind) {
        case AuthoredNodeKind::PrerequisiteStoryProgress:
            if (node.story_start.empty() || node.story_end.empty()) {
                error = "Complete the selected story progression prerequisite.";
                return false;
            }
            break;
        case AuthoredNodeKind::PrerequisiteQuestState:
            if (!IsValidQuestId(node.other_quest)) {
                error = "Enter a valid quest ID in the quest-state prerequisite.";
                return false;
            }
            break;
        case AuthoredNodeKind::PrerequisiteGameflowFlag:
            if (!IsValidQuestId(node.gameflow_flag)) {
                error = "Enter a valid gameflow flag in its prerequisite.";
                return false;
            }
            break;
        case AuthoredNodeKind::PrerequisiteLuaCondition:
            if (node.lua_condition.empty()) {
                error = "Enter the Lua condition for its prerequisite.";
                return false;
            }
            break;
        case AuthoredNodeKind::PrerequisiteHeroRequirement: {
            int value = 0;
            const bool numeric =
                node.hero_requirement != HeroRequirementKind::Gender &&
                node.hero_requirement != HeroRequirementKind::Married &&
                node.hero_requirement != HeroRequirementKind::HasChildren;
            if (numeric && !ParseAuthoredInteger(node.hero_value, value)) {
                error = "Enter a valid whole number for the hero requirement.";
                return false;
            }
            if (numeric &&
                node.hero_requirement != HeroRequirementKind::Alignment &&
                node.hero_requirement != HeroRequirementKind::Purity &&
                value < 0) {
                error = "This hero requirement cannot use a negative value.";
                return false;
            }
            if (node.hero_requirement == HeroRequirementKind::AbilityLevel &&
                std::find(HeroAbilityTypes().begin(),
                          HeroAbilityTypes().end(), node.hero_option) ==
                    HeroAbilityTypes().end()) {
                error = "Select an ability for the hero requirement.";
                return false;
            }
            break;
        }
        default:
            break;
    }
    return true;
}

bool contains_ci_ascii(std::string value, std::string needle) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    std::transform(needle.begin(), needle.end(), needle.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return value.find(needle) != std::string::npos;
}
