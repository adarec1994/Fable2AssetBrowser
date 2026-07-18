bool IsPrerequisiteNode(AuthoredNodeKind kind) {
    return kind == AuthoredNodeKind::PrerequisiteStoryProgress ||
           kind == AuthoredNodeKind::PrerequisiteQuestState ||
           kind == AuthoredNodeKind::PrerequisiteGameflowFlag ||
           kind == AuthoredNodeKind::PrerequisiteLuaCondition ||
           kind == AuthoredNodeKind::PrerequisiteHeroRequirement;
}

const char* AuthoredNodeKindName(AuthoredNodeKind kind) {
    switch (kind) {
        case AuthoredNodeKind::PrerequisiteStoryProgress:
            return "Story progression prerequisite";
        case AuthoredNodeKind::PrerequisiteQuestState:
            return "Quest state prerequisite";
        case AuthoredNodeKind::PrerequisiteGameflowFlag:
            return "Gameflow flag prerequisite";
        case AuthoredNodeKind::PrerequisiteLuaCondition:
            return "Lua condition prerequisite";
        case AuthoredNodeKind::PrerequisiteHeroRequirement:
            return "Hero requirement prerequisite";
        case AuthoredNodeKind::QuestStart: return "Quest start";
        case AuthoredNodeKind::ApproachNpc: return "Approach entity";
        case AuthoredNodeKind::Dialogue: return "Dialogue";
        case AuthoredNodeKind::AcceptQuest: return "Accept quest";
        case AuthoredNodeKind::HoldInteraction: return "Hold A prompt";
        case AuthoredNodeKind::ObtainItem: return "Obtain item";
        case AuthoredNodeKind::ReturnToNpc: return "Return to NPC";
        case AuthoredNodeKind::CompleteQuest: return "Complete quest";
        case AuthoredNodeKind::SkipChildhoodEnding:
            return "Skip childhood to ending";
    }
    return "Quest node";
}

const char* RequiredQuestStateName(RequiredQuestState state) {
    switch (state) {
        case RequiredQuestState::Registered: return "Registered";
        case RequiredQuestState::Activated: return "Activated before";
        case RequiredQuestState::Active: return "Active";
        case RequiredQuestState::Completed: return "Completed";
        case RequiredQuestState::Unlocked: return "Unlocked";
    }
    return "Completed";
}

const char* HeroRequirementKindName(HeroRequirementKind kind) {
    switch (kind) {
        case HeroRequirementKind::Renown: return "Renown";
        case HeroRequirementKind::Alignment: return "Alignment";
        case HeroRequirementKind::Purity: return "Purity";
        case HeroRequirementKind::Gold: return "Gold";
        case HeroRequirementKind::AbilityLevel: return "Ability level";
        case HeroRequirementKind::Age: return "Age";
        case HeroRequirementKind::Gender: return "Gender";
        case HeroRequirementKind::Married: return "Married";
        case HeroRequirementKind::SpouseCount: return "Number of spouses";
        case HeroRequirementKind::HasChildren: return "Has children";
    }
    return "Hero requirement";
}

const char* NumericComparisonName(NumericComparison comparison) {
    switch (comparison) {
        case NumericComparison::AtLeast: return "At least";
        case NumericComparison::AtMost: return "At most";
        case NumericComparison::Exactly: return "Exactly";
    }
    return "At least";
}

const char* QuestRewardKindName(QuestRewardKind kind) {
    switch (kind) {
        case QuestRewardKind::Gold: return "Gold";
        case QuestRewardKind::Renown: return "Renown";
        case QuestRewardKind::GeneralExperience: return "General experience";
        case QuestRewardKind::StrengthExperience: return "Strength experience";
        case QuestRewardKind::SkillExperience: return "Skill experience";
        case QuestRewardKind::WillExperience: return "Will experience";
        case QuestRewardKind::Item: return "Item";
        case QuestRewardKind::Morality: return "Morality";
        case QuestRewardKind::Purity: return "Purity";
    }
    return "Reward";
}

const std::vector<std::string>& HeroAbilityTypes() {
    static const std::vector<std::string> abilities = {
        "EHeroAbilityType.HERO_ABILITY_STRENGTH_MELEE_WEAPONS",
        "EHeroAbilityType.HERO_ABILITY_STRENGTH_PHYSIQUE",
        "EHeroAbilityType.HERO_ABILITY_STRENGTH_TOUGHNESS",
        "EHeroAbilityType.HERO_ABILITY_SKILL_RANGED_WEAPONS",
        "EHeroAbilityType.HERO_ABILITY_SKILL_ACCURACY",
        "EHeroAbilityType.HERO_ABILITY_SKILL_SPEED",
        "EHeroAbilityType.HERO_ABILITY_WILL_LIGHTNING",
        "EHeroAbilityType.HERO_ABILITY_WILL_FIREBALL",
        "EHeroAbilityType.HERO_ABILITY_WILL_SLOW_TIME",
        "EHeroAbilityType.HERO_ABILITY_WILL_SWORDS",
        "EHeroAbilityType.HERO_ABILITY_WILL_VORTEX",
        "EHeroAbilityType.HERO_ABILITY_WILL_CHAOS",
        "EHeroAbilityType.HERO_ABILITY_WILL_FORCE_PUSH",
        "EHeroAbilityType.HERO_ABILITY_WILL_DEAD_RISING",
    };
    return abilities;
}

bool ParseAuthoredInteger(const std::string& text, int& value) {
    value = 0;
    if (text.empty()) return false;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const std::from_chars_result result = std::from_chars(begin, end, value);
    return result.ec == std::errc() && result.ptr == end;
}
