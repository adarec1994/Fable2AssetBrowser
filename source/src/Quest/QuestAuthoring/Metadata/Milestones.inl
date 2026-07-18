const std::vector<StoryProgressMilestone>& StoryProgressMilestones() {



    static const std::vector<StoryProgressMilestone> milestones = {
        {"ScriptEnum.GAMEFLOW_START", nullptr, "Start of game", nullptr},
        {"ScriptEnum.DebugQC010", "TEXT_QUEST_QC010_NAME", "Childhood", nullptr},
        {"ScriptEnum.DebugQC060", "TEXT_QUEST_QC060_NAME", "The Birth of a Hero", nullptr},
        {"ScriptEnum.DebugQC070", "TEXT_QUEST_QC070_NAME", "The Bandit", nullptr},
        {"ScriptEnum.DebugQC075", "TEXT_QUEST_QC075_NAME", "The Journey Begins", nullptr},
        {"ScriptEnum.DebugQC080", "TEXT_QUEST_QC080_NAME", "The Ritual", nullptr},
        {"ScriptEnum.DebugQC085", "TEXT_QUEST_QC085_NAME", "The Hero of Strength", nullptr},
        {"ScriptEnum.DebugQC090", "TEXT_QUEST_QC090_NAME", "The Hero of Will", "find Garth"},
        {"ScriptEnum.DebugQC100", "TEXT_QUEST_QC100_NAME", "The Bargain", nullptr},
        {"ScriptEnum.DebugQC110", "TEXT_QUEST_QC110_NAME", "Road to Westcliff", nullptr},
        {"ScriptEnum.DebugQC120", "TEXT_QUEST_QC120_NAME", "The Crucible", nullptr},
        {"ScriptEnum.DebugQC130", "TEXT_QUEST_QC130_NAME", "The Spire", nullptr},
        {"ScriptEnum.DebugQC140", "TEXT_QUEST_QC140_NAME", "The Hero of Will", "escape the Spire"},
        {"ScriptEnum.DebugQC160", "TEXT_QUEST_QC160_NAME", "The Cullis Gate", nullptr},
        {"ScriptEnum.DebugQC170", "TEXT_QUEST_QC170_NAME", "Stranded", nullptr},
        {"ScriptEnum.DebugQC180", "TEXT_QUEST_QC180_NAME", "The Hero of Skill", nullptr},
        {"ScriptEnum.DebugQC200", "TEXT_QUEST_QC200_NAME", "Bloodstone Assault", nullptr},
        {"ScriptEnum.DebugQC220", "TEXT_QUEST_QC220_NAME", "The Weapon", nullptr},
        {"ScriptEnum.DebugQC230", "TEXT_QUEST_QC230_NAME", "A Perfect World", nullptr},
        {"ScriptEnum.DebugQC240", "TEXT_QUEST_QC240_NAME", "Retribution", nullptr},
        {"ScriptEnum.GAMEFLOW_END", nullptr, "End of story", nullptr},
    };
    return milestones;
}
