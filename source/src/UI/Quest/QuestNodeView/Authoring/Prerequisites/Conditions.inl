const char* authored_prerequisite_title(Quest::AuthoredNodeKind kind) {
    switch (kind) {
        case Quest::AuthoredNodeKind::PrerequisiteStoryProgress:
            return "Story progression";
        case Quest::AuthoredNodeKind::PrerequisiteQuestState:
            return "Quest state";
        case Quest::AuthoredNodeKind::PrerequisiteGameflowFlag:
            return "Named gameflow condition";
        case Quest::AuthoredNodeKind::PrerequisiteLuaCondition:
            return "Lua condition";
        case Quest::AuthoredNodeKind::PrerequisiteHeroRequirement:
            return "Hero requirement";
        default:
            return "Prerequisite";
    }
}

bool draw_required_quest_state(const char* id,
                               Quest::RequiredQuestState& state) {
    bool changed = false;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo(id, Quest::RequiredQuestStateName(state))) {
        for (Quest::RequiredQuestState candidate : {
                 Quest::RequiredQuestState::Registered,
                 Quest::RequiredQuestState::Activated,
                 Quest::RequiredQuestState::Active,
                 Quest::RequiredQuestState::Completed,
                 Quest::RequiredQuestState::Unlocked}) {
            const bool selected = state == candidate;
            if (ImGui::Selectable(
                    Quest::RequiredQuestStateName(candidate), selected)) {
                state = candidate;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool draw_yes_no_combo(const char* id, bool& value) {
    bool changed = false;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo(id, value ? "Yes" : "No")) {
        if (ImGui::Selectable("Yes", value)) {
            value = true;
            changed = true;
        }
        if (ImGui::Selectable("No", !value)) {
            value = false;
            changed = true;
        }
        ImGui::EndCombo();
    }
    return changed;
}

struct NamedGameflowCondition {
    const char* label;
    const char* variable;
};

constexpr NamedGameflowCondition kNamedGameflowConditions[] = {
    {"Westcliff investment opportunity missed", "OpportunityMissed"},
    {"Thag camp gypsy-prisoner sequence required", "GypsiesNeeded"},
    {"Banshee active in Bloodstone", "BansheeInBloodstone"},
    {"Temple of Evil destroyed", "TempleOfEvilDestroyed"},
    {"Brightwood farmer quest completed", "BrightwoodFarmerComplete"},
};

const char* named_gameflow_label(const std::string& variable) {
    for (const NamedGameflowCondition& condition :
         kNamedGameflowConditions) {
        if (variable == condition.variable) return condition.label;
    }
    return variable.empty() ? "Select condition..." : variable.c_str();
}

bool draw_named_gameflow_condition(Quest::AuthoredNode& prerequisite) {
    bool changed = false;
    ImGui::TextUnformatted("Condition");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##condition",
                          named_gameflow_label(
                              prerequisite.gameflow_flag))) {
        for (const NamedGameflowCondition& condition :
             kNamedGameflowConditions) {
            const bool selected =
                prerequisite.gameflow_flag == condition.variable;
            if (ImGui::Selectable(condition.label, selected)) {
                prerequisite.gameflow_flag = condition.variable;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::TextUnformatted("Required value");
    changed |= draw_yes_no_combo("##required_value", prerequisite.expected);
    return changed;
}

std::string hero_ability_label(const std::string& value) {
    constexpr std::string_view prefix = "EHeroAbilityType.HERO_ABILITY_";
    std::string label = value;
    if (label.rfind(prefix, 0) == 0) label.erase(0, prefix.size());
    std::replace(label.begin(), label.end(), '_', ' ');
    std::transform(label.begin(), label.end(), label.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    if (!label.empty()) label.front() =
        static_cast<char>(std::toupper(
            static_cast<unsigned char>(label.front())));
    return label.empty() ? "Select ability..." : label;
}

bool draw_numeric_comparison(Quest::NumericComparison& comparison) {
    bool changed = false;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##comparison",
                          Quest::NumericComparisonName(comparison))) {
        for (Quest::NumericComparison candidate : {
                 Quest::NumericComparison::AtLeast,
                 Quest::NumericComparison::AtMost,
                 Quest::NumericComparison::Exactly}) {
            const bool selected = candidate == comparison;
            if (ImGui::Selectable(
                    Quest::NumericComparisonName(candidate), selected)) {
                comparison = candidate;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool draw_hero_requirement(Quest::AuthoredNode& prerequisite) {
    bool changed = false;
    ImGui::TextUnformatted("Requirement");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo(
            "##hero_requirement",
            Quest::HeroRequirementKindName(prerequisite.hero_requirement))) {
        for (Quest::HeroRequirementKind candidate : {
                 Quest::HeroRequirementKind::Renown,
                 Quest::HeroRequirementKind::Alignment,
                 Quest::HeroRequirementKind::Purity,
                 Quest::HeroRequirementKind::Gold,
                 Quest::HeroRequirementKind::AbilityLevel,
                 Quest::HeroRequirementKind::Age,
                 Quest::HeroRequirementKind::Gender,
                 Quest::HeroRequirementKind::Married,
                 Quest::HeroRequirementKind::SpouseCount,
                 Quest::HeroRequirementKind::HasChildren}) {
            const bool selected = candidate == prerequisite.hero_requirement;
            if (ImGui::Selectable(
                    Quest::HeroRequirementKindName(candidate), selected)) {
                prerequisite.hero_requirement = candidate;
                prerequisite.expected = true;
                if (candidate == Quest::HeroRequirementKind::Gender) {
                    prerequisite.hero_option = "Male";
                } else if (candidate ==
                           Quest::HeroRequirementKind::AbilityLevel) {
                    const auto& abilities = Quest::HeroAbilityTypes();
                    if (std::find(abilities.begin(), abilities.end(),
                                  prerequisite.hero_option) ==
                        abilities.end()) {
                        prerequisite.hero_option = abilities.front();
                    }
                }
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    if (prerequisite.hero_requirement ==
        Quest::HeroRequirementKind::Gender) {
        ImGui::TextUnformatted("Gender");
        ImGui::SetNextItemWidth(-1.0f);
        const char* current = prerequisite.hero_option == "Female"
            ? "Female" : "Male";
        if (ImGui::BeginCombo("##gender", current)) {
            for (const char* candidate : {"Male", "Female"}) {
                const bool selected = prerequisite.hero_option == candidate;
                if (ImGui::Selectable(candidate, selected)) {
                    prerequisite.hero_option = candidate;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }
    if (prerequisite.hero_requirement ==
            Quest::HeroRequirementKind::Married ||
        prerequisite.hero_requirement ==
            Quest::HeroRequirementKind::HasChildren) {
        ImGui::TextUnformatted("Required value");
        changed |= draw_yes_no_combo("##hero_bool", prerequisite.expected);
        return changed;
    }

    if (prerequisite.hero_requirement ==
        Quest::HeroRequirementKind::AbilityLevel) {
        ImGui::TextUnformatted("Ability");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo(
                "##ability",
                hero_ability_label(prerequisite.hero_option).c_str())) {
            for (const std::string& ability : Quest::HeroAbilityTypes()) {
                const bool selected = prerequisite.hero_option == ability;
                const std::string label = hero_ability_label(ability);
                if (ImGui::Selectable(label.c_str(), selected)) {
                    prerequisite.hero_option = ability;
                    changed = true;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    ImGui::TextUnformatted("Comparison");
    changed |= draw_numeric_comparison(prerequisite.numeric_comparison);
    ImGui::TextUnformatted("Value");
    ImGui::SetNextItemWidth(-1.0f);
    changed |= ImGui::InputText("##value", &prerequisite.hero_value);
    int parsed = 0;
    if (!Quest::ParseAuthoredInteger(prerequisite.hero_value, parsed)) {
        ImGui::TextColored(ImVec4(1.0f, 0.36f, 0.32f, 1.0f),
                           "Enter a whole number.");
    }
    return changed;
}
