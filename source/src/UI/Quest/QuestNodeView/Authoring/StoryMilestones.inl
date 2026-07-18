std::string story_milestone_display_name(
    const Quest::StoryProgressMilestone& milestone,
    std::size_t sequence_index,
    std::size_t sequence_size) {
    std::string title;
    if (milestone.title_tag) {
        TextBank::LookupTag(milestone.title_tag, title);
    }
    if (title.empty()) title = milestone.fallback_title;
    if (milestone.stage_detail && milestone.stage_detail[0]) {
        title += " - ";
        title += milestone.stage_detail;
    }
    if (sequence_index > 0 && sequence_index + 1 < sequence_size) {
        title = std::to_string(sequence_index) + ". " + title;
    }
    return title;
}

std::string story_milestone_display_name(const std::string& value) {
    const auto& milestones = Quest::StoryProgressMilestones();
    for (std::size_t index = 0; index < milestones.size(); ++index) {
        if (value == milestones[index].value) {
            return story_milestone_display_name(
                milestones[index], index, milestones.size());
        }
    }
    return "Unknown story stage";
}

bool draw_story_milestone_combo(const char* label, std::string& value,
                                bool no_expiry_at_story_end = false) {
    bool changed = false;
    ImGui::TextUnformatted(label);
    ImGui::PushID(label);
    ImGui::SetNextItemWidth(-1.0f);
    const bool no_expiry = no_expiry_at_story_end &&
        value == "ScriptEnum.GAMEFLOW_END";
    const std::string preview = no_expiry
        ? "None"
        : story_milestone_display_name(value);
    if (ImGui::BeginCombo("##milestone", preview.c_str())) {
        const auto& milestones = Quest::StoryProgressMilestones();
        for (std::size_t index = 0; index < milestones.size(); ++index) {
            const Quest::StoryProgressMilestone& milestone = milestones[index];
            const bool selected = value == milestone.value;
            const bool is_no_expiry = no_expiry_at_story_end &&
                std::string(milestone.value) == "ScriptEnum.GAMEFLOW_END";
            const std::string display = is_no_expiry
                ? "None"
                : story_milestone_display_name(
                      milestone, index, milestones.size());
            if (ImGui::Selectable(display.c_str(), selected)) {
                value = milestone.value;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
    return changed;
}


bool contains_case_insensitive(std::string_view text,
                               std::string_view query) {
    if (query.empty()) return true;
    std::string haystack(text);
    std::string needle(query);
    auto lower = [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    };
    std::transform(haystack.begin(), haystack.end(), haystack.begin(), lower);
    std::transform(needle.begin(), needle.end(), needle.begin(), lower);
    return haystack.find(needle) != std::string::npos;
}
