std::string GenerateAuthoringPreview(const AuthoredQuest& quest) {
    std::ostringstream preview;
    preview << "-- Quest script\n" << GenerateQuestLua(quest)
            << "\n-- GameflowQuestUnlocker:Update() eligibility entry\n"
            << GenerateEligibilityLua(quest) << '\n';
    return preview.str();
}
