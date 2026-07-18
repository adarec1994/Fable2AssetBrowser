void RequestOpenPrerequisiteMenu() {
    g_open_prerequisite_menu = true;
}

void AddPrerequisiteExamplesForCapture() {
    Quest::AuthoredQuest* quest = active_authored_quest();
    if (!quest) return;
    quest->prerequisites.clear();

    Quest::AddAuthoredPrerequisite(
        *quest, Quest::AuthoredNodeKind::PrerequisiteStoryProgress);
    Quest::AuthoredNode& story = quest->prerequisites.back();
    story.story_start = "ScriptEnum.DebugQC080";
    story.story_end = "ScriptEnum.GAMEFLOW_END";

    Quest::AddAuthoredPrerequisite(
        *quest, Quest::AuthoredNodeKind::PrerequisiteQuestState);
    Quest::AuthoredNode& state = quest->prerequisites.back();
    state.other_quest = "QO100_BrightwoodFarmer";
    state.quest_state = Quest::RequiredQuestState::Completed;
    state.expected = true;

    Quest::AddAuthoredPrerequisite(
        *quest, Quest::AuthoredNodeKind::PrerequisiteGameflowFlag);
    Quest::AuthoredNode& condition = quest->prerequisites.back();
    condition.gameflow_flag = "TempleOfEvilDestroyed";
    condition.expected = false;

    Quest::AddAuthoredPrerequisite(
        *quest, Quest::AuthoredNodeKind::PrerequisiteHeroRequirement);
    Quest::AuthoredNode& hero = quest->prerequisites.back();
    hero.hero_requirement = Quest::HeroRequirementKind::Renown;
    hero.numeric_comparison = Quest::NumericComparison::AtLeast;
    hero.hero_value = "1000";
    refresh_authored_lua();
}

void SetPrerequisiteInspectorCaptureScrollBottom(bool enabled) {
    g_prerequisite_capture_scroll_bottom = enabled;
    g_prerequisite_capture_scroll_fraction = enabled ? 1.0f : -1.0f;
}

void SetPrerequisiteInspectorCaptureScrollFraction(float fraction) {
    g_prerequisite_capture_scroll_bottom = false;
    g_prerequisite_capture_scroll_fraction =
        std::clamp(fraction, 0.0f, 1.0f);
}

LevelReferenceTarget PendingLevelReferenceTarget() {
    return g_level_reference_target;
}

std::string PendingLevelReferenceLabel() {
    if (BlueprintUI::PendingPickPin() != 0) {
        return "blueprint pin reference";
    }
    const Quest::AuthoredQuest* quest = active_authored_quest_const();
    const Quest::AuthoredNode* node = quest
        ? Quest::FindAuthoredNode(*quest, g_level_reference_node) : nullptr;
    if (!node) return "quest node reference";
    if (g_level_reference_target == LevelReferenceTarget::RequiredItemSource) {
        return "item source for " +
               std::string(Quest::AuthoredNodeKindName(node->kind));
    }
    return "NPC for " +
           std::string(Quest::AuthoredNodeKindName(node->kind));
}

uint32_t PendingLevelReferenceItemHash() {
    const Quest::AuthoredQuest* quest = active_authored_quest_const();
    const Quest::AuthoredNode* node = quest
        ? Quest::FindAuthoredNode(*quest, g_level_reference_node) : nullptr;
    return node &&
           g_level_reference_target == LevelReferenceTarget::RequiredItemSource
        ? node->item.record_hash : 0;
}
