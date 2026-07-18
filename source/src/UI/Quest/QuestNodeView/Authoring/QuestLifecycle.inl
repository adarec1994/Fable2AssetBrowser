bool CreateNewBlueprintQuest(const std::string& quest_id,
                             std::string& error) {
    error.clear();
    for (const Quest::AuthoredQuest& quest : g_authored_quests) {
        if (quest.quest_id == quest_id) {
            error = "A custom quest with this ID already exists.";
            return false;
        }
    }
    if (!BlueprintUI::CreateQuest(quest_id, error)) return false;
    g_active_authored_quest = -1;
    refresh_authored_lua();
    return true;
}

bool OpenAuthoredQuest(const std::string& quest_id) {
    if (!BlueprintUI::OpenQuest(quest_id)) return false;
    refresh_authored_lua();
    return true;
}

std::vector<std::string> AuthoredQuestIds() {
    return BlueprintUI::QuestIds();
}

bool DeleteAuthoredQuest(const std::string& quest_id, std::string& error) {
    return BlueprintUI::DeleteQuest(quest_id, error);
}

bool IsAuthoredQuestActive() {
    return BlueprintUI::IsActive();
}

std::string ActiveAuthoredQuestId() {
    return BlueprintUI::ActiveQuestId();
}

std::string ActiveAuthoredLua() {
    return BlueprintUI::ActiveLua();
}

std::string ActiveAuthoredQuestLua() {
    return BlueprintUI::ActiveQuestLua();
}

std::string ActiveAuthoredEligibilityLua() {
    return BlueprintUI::ActiveEligibilityLua();
}

std::vector<std::pair<std::string, std::string>>
ActiveAuthoredTextEntries() {
    return BlueprintUI::ActiveTextEntries();
}

bool ValidateActiveAuthoredQuest(std::string& error) {
    return BlueprintUI::ValidateActive(error);
}
