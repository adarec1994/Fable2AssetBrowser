#include "BlueprintPrerequisites.h"

#include "Level/Database/TextBank.h"
#include "Quest/QuestAuthoring.h"

#include "IconsFontAwesome6.h"
#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace BlueprintUIDetail {

using Quest::Bp::BlueprintQuest;

namespace {

const char* prerequisite_title(Quest::AuthoredNodeKind kind) {
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

std::string milestone_display_name(
    const Quest::StoryProgressMilestone& milestone,
    std::size_t sequence_index, std::size_t sequence_size) {
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

std::string milestone_display_name(const std::string& value) {
    const auto& milestones = Quest::StoryProgressMilestones();
    for (std::size_t index = 0; index < milestones.size(); ++index) {
        if (value == milestones[index].value) {
            return milestone_display_name(milestones[index], index,
                                          milestones.size());
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
    const std::string preview =
        no_expiry ? "None" : milestone_display_name(value);
    if (ImGui::BeginCombo("##milestone", preview.c_str())) {
        const auto& milestones = Quest::StoryProgressMilestones();
        for (std::size_t index = 0; index < milestones.size(); ++index) {
            const Quest::StoryProgressMilestone& milestone =
                milestones[index];
            const bool selected = value == milestone.value;
            const bool is_no_expiry = no_expiry_at_story_end &&
                std::string(milestone.value) == "ScriptEnum.GAMEFLOW_END";
            const std::string display = is_no_expiry
                ? "None"
                : milestone_display_name(milestone, index,
                                         milestones.size());
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
    if (!label.empty()) {
        label.front() = static_cast<char>(
            std::toupper(static_cast<unsigned char>(label.front())));
    }
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
            const bool selected =
                candidate == prerequisite.hero_requirement;
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
                const bool selected =
                    prerequisite.hero_option == candidate;
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

}

bool DrawPrerequisiteConfig(Quest::AuthoredNode& prerequisite) {
    bool changed = false;
    switch (prerequisite.kind) {
        case Quest::AuthoredNodeKind::PrerequisiteStoryProgress:
            changed |= draw_story_milestone_combo(
                "Available after", prerequisite.story_start);
            changed |= draw_story_milestone_combo(
                "Unavailable after", prerequisite.story_end, true);
            break;
        case Quest::AuthoredNodeKind::PrerequisiteQuestState:
            ImGui::TextUnformatted("Quest");
            ImGui::SetNextItemWidth(-1.0f);
            changed |= ImGui::InputText("##quest",
                                        &prerequisite.other_quest);
            ImGui::TextUnformatted("State");
            {
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::BeginCombo(
                        "##state",
                        Quest::RequiredQuestStateName(
                            prerequisite.quest_state))) {
                    for (Quest::RequiredQuestState candidate : {
                             Quest::RequiredQuestState::Registered,
                             Quest::RequiredQuestState::Activated,
                             Quest::RequiredQuestState::Active,
                             Quest::RequiredQuestState::Completed,
                             Quest::RequiredQuestState::Unlocked}) {
                        const bool selected =
                            candidate == prerequisite.quest_state;
                        if (ImGui::Selectable(
                                Quest::RequiredQuestStateName(candidate),
                                selected)) {
                            prerequisite.quest_state = candidate;
                            changed = true;
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }
            changed |= ImGui::Checkbox("Must be in this state",
                                       &prerequisite.expected);
            break;
        case Quest::AuthoredNodeKind::PrerequisiteGameflowFlag:
            changed |= draw_named_gameflow_condition(prerequisite);
            break;
        case Quest::AuthoredNodeKind::PrerequisiteLuaCondition:
            ImGui::TextUnformatted("Lua condition");
            ImGui::SetNextItemWidth(-1.0f);
            changed |= ImGui::InputTextMultiline(
                "##condition", &prerequisite.lua_condition,
                ImVec2(-1.0f, ImGui::GetTextLineHeight() * 3.0f));
            changed |= ImGui::Checkbox("Must be true",
                                       &prerequisite.expected);
            break;
        case Quest::AuthoredNodeKind::PrerequisiteHeroRequirement:
            changed |= draw_hero_requirement(prerequisite);
            break;
        default:
            break;
    }
    return changed;
}

}
