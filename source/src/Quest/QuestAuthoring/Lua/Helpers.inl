std::string lua_quote(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('"');
    for (char c : value) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            default: out.push_back(c); break;
        }
    }
    out.push_back('"');
    return out;
}

std::string node_tag(const AuthoredQuest& quest,
                     const AuthoredNode& node) {
    return "TEXT_QUEST_" + quest.quest_id + "_NODE_" +
           std::to_string(node.id);
}

std::string patch_accept_tag(const AuthoredQuest& quest) {
    return "TEXT_QUEST_" + quest.quest_id + "_SKIP";
}

const AuthoredNode* find_kind(const AuthoredQuest& quest,
                              AuthoredNodeKind kind) {
    for (const AuthoredNode& node : quest.nodes) {
        if (node.kind == kind) return &node;
    }
    return nullptr;
}

bool is_childhood_skip_graph(const AuthoredQuest& quest) {
    return find_kind(quest, AuthoredNodeKind::SkipChildhoodEnding) !=
           nullptr;
}

std::string quest_state_expression(const AuthoredNode& node) {
    const char* function = "IsCompleted";
    switch (node.quest_state) {
        case RequiredQuestState::Registered: function = "IsRegistered"; break;
        case RequiredQuestState::Activated:
            function = "HasBeenActivated";
            break;
        case RequiredQuestState::Active: function = "IsActive"; break;
        case RequiredQuestState::Completed: function = "IsCompleted"; break;
        case RequiredQuestState::Unlocked: function = "IsUnlocked"; break;
    }
    return std::string("QuestTracker.") + function +
           "(QuestManager.HeroEntity, \"" + node.other_quest + "\")";
}

std::string numeric_comparison_operator(NumericComparison comparison) {
    switch (comparison) {
        case NumericComparison::AtLeast: return ">=";
        case NumericComparison::AtMost: return "<=";
        case NumericComparison::Exactly: return "==";
    }
    return ">=";
}

std::string hero_requirement_expression(const AuthoredNode& node) {
    int value = 0;
    ParseAuthoredInteger(node.hero_value, value);
    std::string source;
    switch (node.hero_requirement) {
        case HeroRequirementKind::Renown:
            source = "Stats.GetRenown(QuestManager.HeroEntity)";
            break;
        case HeroRequirementKind::Alignment:
            source = "Stats.GetMorality(QuestManager.HeroEntity)";
            break;
        case HeroRequirementKind::Purity:
            source = "Stats.GetPurity(QuestManager.HeroEntity)";
            break;
        case HeroRequirementKind::Gold:
            source = "Money.Get(QuestManager.HeroEntity)";
            break;
        case HeroRequirementKind::AbilityLevel:
            source = "Stats.GetHeroAbilityLevel(QuestManager.HeroEntity, " +
                     (node.hero_option.empty()
                          ? std::string("EHeroAbilityType.HERO_ABILITY_SKILL_RANGED_WEAPONS")
                          : node.hero_option) + ")";
            break;
        case HeroRequirementKind::Age:
            source = "Age.GetAge(QuestManager.HeroEntity)";
            break;
        case HeroRequirementKind::SpouseCount:
            source = "PlayerFamily.GetNumberOfSpouses(QuestManager.HeroEntity)";
            break;
        case HeroRequirementKind::Gender: {
            const std::string gender = node.hero_option == "Female"
                ? "EGender.EG_FEMALE" : "EGender.EG_MALE";
            return "Gender.Get(QuestManager.HeroEntity) == " + gender;
        }
        case HeroRequirementKind::Married:
            return "PlayerFamily.IsMarried(QuestManager.HeroEntity)";
        case HeroRequirementKind::HasChildren:
            return "PlayerFamily.HasChild(QuestManager.HeroEntity)";
    }
    return source + " " +
           numeric_comparison_operator(node.numeric_comparison) + " " +
           std::to_string(value);
}

std::string prerequisite_expression(const AuthoredNode& node) {
    std::string expression;
    switch (node.kind) {
        case AuthoredNodeKind::PrerequisiteQuestState:
            expression = quest_state_expression(node);
            break;
        case AuthoredNodeKind::PrerequisiteGameflowFlag:
            expression = "Gameflow." + node.gameflow_flag;
            break;
        case AuthoredNodeKind::PrerequisiteLuaCondition:
            expression = node.lua_condition.empty() ? "true"
                                                    : node.lua_condition;
            break;
        case AuthoredNodeKind::PrerequisiteHeroRequirement:
            expression = hero_requirement_expression(node);
            break;
        default:
            return {};
    }
    return node.expected ? expression : "not (" + expression + ")";
}
