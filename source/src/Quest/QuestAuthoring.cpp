#include "QuestAuthoring.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <iterator>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace Quest {
namespace {

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

const AuthoredNode* find_kind(const AuthoredQuest& quest,
                              AuthoredNodeKind kind) {
    for (const AuthoredNode& node : quest.nodes) {
        if (node.kind == kind) return &node;
    }
    return nullptr;
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

}

bool IsValidQuestId(const std::string& quest_id) {
    if (quest_id.empty()) return false;
    const unsigned char first = static_cast<unsigned char>(quest_id.front());
    if (!std::isalpha(first) && quest_id.front() != '_') return false;
    return std::all_of(quest_id.begin() + 1, quest_id.end(),
                       [](unsigned char c) {
                           return std::isalnum(c) || c == '_';
                       });
}

AuthoredQuest CreateAuthoredQuest(const std::string& quest_id) {
    AuthoredQuest quest;
    quest.quest_id = quest_id;
    quest.quest_title = quest_id;
    AddAuthoredNode(quest, AuthoredNodeKind::QuestStart, 0.0f, 0.0f);
    return quest;
}

int AddAuthoredNode(AuthoredQuest& quest, AuthoredNodeKind kind,
                    float x, float y) {
    if (kind == AuthoredNodeKind::QuestStart ||
        kind == AuthoredNodeKind::CompleteQuest) {
        for (const AuthoredNode& existing : quest.nodes) {
            if (existing.kind == kind) {
                return existing.id;
            }
        }
    }
    AuthoredNode node;
    node.id = quest.next_node_id++;
    node.kind = kind;
    node.x = x;
    node.y = y;
    quest.nodes.push_back(std::move(node));
    return quest.nodes.back().id;
}

bool RemoveAuthoredNode(AuthoredQuest& quest, int node_id) {
    const AuthoredNode* node = FindAuthoredNode(quest, node_id);
    if (node && node->kind == AuthoredNodeKind::QuestStart) return false;
    const bool removes_completion = node &&
        node->kind == AuthoredNodeKind::CompleteQuest;
    const std::size_t old_size = quest.nodes.size();
    quest.nodes.erase(
        std::remove_if(quest.nodes.begin(), quest.nodes.end(),
                       [node_id](const AuthoredNode& node) {
                           return node.id == node_id;
                       }),
        quest.nodes.end());
    if (quest.nodes.size() == old_size) return false;
    if (removes_completion) quest.rewards.clear();
    quest.links.erase(
        std::remove_if(quest.links.begin(), quest.links.end(),
                       [node_id](const AuthoredLink& link) {
                           return link.from_node == node_id ||
                                  link.to_node == node_id;
                       }),
        quest.links.end());
    return true;
}

int AddAuthoredLink(AuthoredQuest& quest, int from_node, int to_node) {
    if (from_node == to_node || !FindAuthoredNode(quest, from_node) ||
        !FindAuthoredNode(quest, to_node)) return 0;
    if (FindAuthoredNode(quest, from_node)->kind ==
        AuthoredNodeKind::CompleteQuest) return 0;
    for (const AuthoredLink& link : quest.links) {
        if (link.from_node == from_node && link.to_node == to_node) {
            return link.id;
        }
    }
    AuthoredLink link;
    link.id = quest.next_link_id++;
    link.from_node = from_node;
    link.to_node = to_node;
    quest.links.push_back(link);
    return link.id;
}

bool RemoveAuthoredLink(AuthoredQuest& quest, int link_id) {
    const std::size_t old_size = quest.links.size();
    quest.links.erase(
        std::remove_if(quest.links.begin(), quest.links.end(),
                       [link_id](const AuthoredLink& link) {
                           return link.id == link_id;
                       }),
        quest.links.end());
    return quest.links.size() != old_size;
}

int AddAuthoredPrerequisite(AuthoredQuest& quest, AuthoredNodeKind kind) {
    if (!IsPrerequisiteNode(kind)) return 0;
    AuthoredNode prerequisite;
    prerequisite.id = quest.next_prerequisite_id++;
    prerequisite.kind = kind;
    quest.prerequisites.push_back(std::move(prerequisite));
    return quest.prerequisites.back().id;
}

bool RemoveAuthoredPrerequisite(AuthoredQuest& quest,
                                int prerequisite_id) {
    const std::size_t old_size = quest.prerequisites.size();
    quest.prerequisites.erase(
        std::remove_if(quest.prerequisites.begin(),
                       quest.prerequisites.end(),
                       [prerequisite_id](const AuthoredNode& prerequisite) {
                           return prerequisite.id == prerequisite_id;
                       }),
        quest.prerequisites.end());
    return quest.prerequisites.size() != old_size;
}

int AddAuthoredReward(AuthoredQuest& quest, QuestRewardKind kind) {
    AuthoredReward reward;
    reward.id = quest.next_reward_id++;
    reward.kind = kind;
    quest.rewards.push_back(std::move(reward));
    return quest.rewards.back().id;
}

bool RemoveAuthoredReward(AuthoredQuest& quest, int reward_id) {
    const std::size_t old_size = quest.rewards.size();
    quest.rewards.erase(
        std::remove_if(quest.rewards.begin(), quest.rewards.end(),
                       [reward_id](const AuthoredReward& reward) {
                           return reward.id == reward_id;
                       }),
        quest.rewards.end());
    return quest.rewards.size() != old_size;
}

AuthoredNode* FindAuthoredNode(AuthoredQuest& quest, int node_id) {
    for (AuthoredNode& node : quest.nodes) {
        if (node.id == node_id) return &node;
    }
    return nullptr;
}

const AuthoredNode* FindAuthoredNode(const AuthoredQuest& quest,
                                     int node_id) {
    for (const AuthoredNode& node : quest.nodes) {
        if (node.id == node_id) return &node;
    }
    return nullptr;
}

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
        case AuthoredNodeKind::ApproachNpc: return "Approach NPC";
        case AuthoredNodeKind::Dialogue: return "Dialogue";
        case AuthoredNodeKind::AcceptQuest: return "Accept quest";
        case AuthoredNodeKind::ObtainItem: return "Obtain item";
        case AuthoredNodeKind::ReturnToNpc: return "Return to NPC";
        case AuthoredNodeKind::CompleteQuest: return "Complete quest";
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

bool ValidateSimpleQuest(const AuthoredQuest& quest, std::string& error) {
    error.clear();
    if (!IsValidQuestId(quest.quest_id)) {
        error = "The quest ID is invalid.";
        return false;
    }
    if (quest.quest_title.empty()) {
        error = "Enter a quest title.";
        return false;
    }
    for (const AuthoredNode& prerequisite : quest.prerequisites) {
        if (!validate_prerequisite(prerequisite, error)) return false;
    }



    for (const AuthoredNode& node : quest.nodes) {
        if (IsPrerequisiteNode(node.kind) &&
            !validate_prerequisite(node, error)) return false;
    }

    std::vector<const AuthoredNode*> flow;
    if (!ordered_flow(quest, flow, error)) return false;
    const AuthoredNodeKind expected[] = {
        AuthoredNodeKind::QuestStart,
        AuthoredNodeKind::ApproachNpc,
        AuthoredNodeKind::Dialogue,
        AuthoredNodeKind::AcceptQuest,
        AuthoredNodeKind::ObtainItem,
        AuthoredNodeKind::ReturnToNpc,
        AuthoredNodeKind::CompleteQuest,
    };
    if (flow.size() != std::size(expected)) {
        error = "For this basic quest, connect: Quest start -> Approach NPC -> "
                "Dialogue -> Accept quest -> Obtain item -> Return to NPC -> "
                "Complete quest.";
        return false;
    }
    for (std::size_t i = 0; i < flow.size(); ++i) {
        if (flow[i]->kind != expected[i]) {
            error = "The basic quest nodes are not connected in the required order.";
            return false;
        }
    }
    for (const AuthoredNode& prerequisite : quest.nodes) {
        if (!IsPrerequisiteNode(prerequisite.kind)) continue;
        const bool connected = std::any_of(
            quest.links.begin(), quest.links.end(),
            [&](const AuthoredLink& link) {
                return link.from_node == prerequisite.id &&
                       link.to_node == flow.front()->id;
            });
        if (!connected) {
            error = "Connect every prerequisite to the first quest node.";
            return false;
        }
    }

    const AuthoredNode& approach = *flow[1];
    const AuthoredNode& dialogue = *flow[2];
    const AuthoredNode& accept = *flow[3];
    const AuthoredNode& obtain = *flow[4];
    const AuthoredNode& returning = *flow[5];
    if (!approach.entity.valid() ||
        !IsValidQuestId(approach.entity.entity_name)) {
        error = "Select the NPC for the Approach NPC node.";
        return false;
    }
    if (approach.approach_radius <= 0.0f) {
        error = "The NPC approach radius must be greater than zero.";
        return false;
    }
    if (!dialogue.entity.valid() || dialogue.text.empty()) {
        error = "Select a speaker and enter text in the Dialogue node.";
        return false;
    }
    if (accept.text.empty()) {
        error = "Enter the question shown by the Accept quest node.";
        return false;
    }
    if (!obtain.item.valid() || !obtain.item.source.valid() ||
        obtain.item_count < 1 || obtain.text.empty()) {
        error = "Complete the item, source, amount, and objective in the Obtain item node.";
        return false;
    }
    if (!returning.entity.valid() || returning.text.empty()) {
        error = "Select an NPC and enter objective text in the Return to NPC node.";
        return false;
    }
    if (returning.remove_item && !returning.item.valid()) {
        error = "Select the item removed by the Return to NPC node.";
        return false;
    }
    for (const AuthoredReward& reward : quest.rewards) {
        if (reward.kind == QuestRewardKind::Item) {
            if (!reward.item.valid()) {
                error = "Select the item used by every item reward.";
                return false;
            }
            if (reward.item_count < 1) {
                error = "An item reward amount must be at least one.";
                return false;
            }
        } else if (reward.kind != QuestRewardKind::Morality &&
                   reward.kind != QuestRewardKind::Purity &&
                   reward.amount < 0) {
            error = std::string(QuestRewardKindName(reward.kind)) +
                    " cannot be negative.";
            return false;
        }
    }
    return true;
}

std::vector<std::pair<std::string, std::string>>
AuthoredTextEntries(const AuthoredQuest& quest) {
    std::vector<std::pair<std::string, std::string>> entries;
    entries.emplace_back("Quest_" + quest.quest_id, quest.quest_title);
    for (const AuthoredNode& node : quest.nodes) {
        switch (node.kind) {
            case AuthoredNodeKind::Dialogue:
            case AuthoredNodeKind::AcceptQuest:
            case AuthoredNodeKind::ObtainItem:
            case AuthoredNodeKind::ReturnToNpc:
                if (!node.text.empty()) {
                    entries.emplace_back(node_tag(quest, node), node.text);
                }
                break;
            default:
                break;
        }
    }
    return entries;
}

std::string GenerateQuestLua(const AuthoredQuest& quest) {
    std::string validation_error;
    if (!ValidateSimpleQuest(quest, validation_error)) {
        return "-- Quest graph incomplete: " + validation_error + "\n";
    }
    const AuthoredNode& approach = *find_kind(quest, AuthoredNodeKind::ApproachNpc);
    const AuthoredNode& dialogue = *find_kind(quest, AuthoredNodeKind::Dialogue);
    const AuthoredNode& accept = *find_kind(quest, AuthoredNodeKind::AcceptQuest);
    const AuthoredNode& obtain = *find_kind(quest, AuthoredNodeKind::ObtainItem);
    const AuthoredNode& returning = *find_kind(quest, AuthoredNodeKind::ReturnToNpc);

    const std::string dialogue_tag = node_tag(quest, dialogue);
    const std::string accept_tag = node_tag(quest, accept);
    const std::string obtain_tag = node_tag(quest, obtain);
    const std::string return_tag = node_tag(quest, returning);
    const bool same_return_actor =
        approach.entity.entity_name == returning.entity.entity_name;

    std::ostringstream lua;
    lua << "module(..., package.seeall)\n"
        << "QuestManager.NewQuestThread(\"" << quest.quest_id << "\")\n\n"
        << "function " << quest.quest_id << ":Init()\n"
        << "  self.MissionSucceeded = false\n"
        << "  self.MissionFailed = false\n"
        << "  self.MissionStarted = false\n"
        << "  self.HasRequiredItem = false\n"
        << "  self.ItemBreadcrumbSet = false\n"
        << "  self.QuestName = \"" << quest.quest_id << "\"\n"
        << "  QuestTracker.Register(QuestManager.HeroEntity, self.QuestName, "
        << lua_quote("Quest_" + quest.quest_id) << ")\n"
        << "  QuestTracker.Unlock(QuestManager.HeroEntity, self.QuestName)\n"
        << "end\n\n"
        << "function " << quest.quest_id << ":BeginObjective()\n"
        << "  if self.MissionStarted then return end\n"
        << "  self.MissionStarted = true\n"
        << "  QuestTracker.SetAsActive(QuestManager.HeroEntity, self.QuestName, true)\n"
        << "  QuestTracker.SetAsPrimary(QuestManager.HeroEntity, self.QuestName)\n"
        << "  QuestTracker.SetObjectiveTag(QuestManager.HeroEntity, self.QuestName, "
        << lua_quote(obtain_tag) << ")\n"
        << "  QuestTracker.SetObjectiveLevel(QuestManager.HeroEntity, self.QuestName, "
        << lua_quote(obtain.item.source.level_id) << ")\n"
        << "end\n\n"
        << "function " << quest.quest_id << ":Update()\n"
        << "  while not self.MissionSucceeded and not self.MissionFailed do\n"
        << "    if self.MissionStarted and not self.HasRequiredItem then\n"
        << "      if not self.ItemBreadcrumbSet and IsLevelLoaded("
        << lua_quote(obtain.item.source.level_id) << ") then\n"
        << "        local source = self:GetEntityWithName("
        << lua_quote(obtain.item.source.entity_name) << ", \"object\")\n"
        << "        if source and source:IsAlive() then\n"
        << "          self.RequiredItemSource = source\n"
        << "          QuestTracker.SetObjectiveEntity(QuestManager.HeroEntity, self.QuestName, source, true)\n"
        << "          self.ItemBreadcrumbSet = true\n"
        << "        end\n"
        << "      end\n"
        << "      if Inventory.GetNumberOfItemsOfType(QuestManager.HeroEntity, "
        << lua_quote(obtain.item.internal_name) << ") >= " << obtain.item_count
        << " then\n"
        << "        self.HasRequiredItem = true\n"
        << "        if self.RequiredItemSource and self.RequiredItemSource:IsAlive() then\n"
        << "          QuestTracker.SetObjectiveEntity(QuestManager.HeroEntity, self.QuestName, self.RequiredItemSource, false)\n"
        << "        end\n"
        << "        QuestTracker.SetObjectiveTag(QuestManager.HeroEntity, self.QuestName, "
        << lua_quote(return_tag) << ", " << lua_quote(obtain_tag) << ")\n"
        << "        QuestTracker.SetObjectiveLevel(QuestManager.HeroEntity, self.QuestName, "
        << lua_quote(returning.entity.level_id) << ")\n"
        << "      end\n"
        << "    end\n"
        << "    coroutine.yield()\n"
        << "  end\n"
        << "  if self.MissionSucceeded then\n"
        << "    QuestTracker.ClearAllObjectiveEntities(QuestManager.HeroEntity, self.QuestName)\n"
        << "    QuestTracker.SetObjectiveAsCompleted(QuestManager.HeroEntity, self.QuestName, "
        << lua_quote(return_tag) << ")\n";
    for (const AuthoredReward& reward : quest.rewards) {
        switch (reward.kind) {
            case QuestRewardKind::Gold:
                if (reward.amount != 0) {
                    lua << "    Money.Modify(QuestManager.HeroEntity, "
                        << reward.amount << ")\n";
                }
                break;
            case QuestRewardKind::Renown:
                if (reward.amount != 0) {
                    lua << "    Stats.ModifyRenown(QuestManager.HeroEntity, "
                        << reward.amount << ")\n";
                }
                break;
            case QuestRewardKind::GeneralExperience:
            case QuestRewardKind::StrengthExperience:
            case QuestRewardKind::SkillExperience:
            case QuestRewardKind::WillExperience: {
                if (reward.amount == 0) break;
                const char* type =
                    reward.kind == QuestRewardKind::GeneralExperience
                        ? "EExperienceType.EXPERIENCE_GENERAL"
                    : reward.kind == QuestRewardKind::StrengthExperience
                        ? "EExperienceType.EXPERIENCE_STRENGTH"
                    : reward.kind == QuestRewardKind::SkillExperience
                        ? "EExperienceType.EXPERIENCE_SKILL"
                        : "EExperienceType.EXPERIENCE_WILL";
                lua << "    Experience.Modify(QuestManager.HeroEntity, "
                    << type << ", " << reward.amount << ", false)\n";
                break;
            }
            case QuestRewardKind::Item:
                lua << "    for reward_index = 1, " << reward.item_count
                    << " do\n"
                    << "      local reward_item = Inventory.AddItemOfType(QuestManager.HeroEntity, "
                    << lua_quote(reward.item.internal_name) << ")\n"
                    << "      GUI.DisplayReceivedItem(reward_item)\n"
                    << "    end\n";
                break;
            case QuestRewardKind::Morality:
                if (reward.amount != 0) {
                    lua << "    Stats.ModifyMorality(QuestManager.HeroEntity, "
                        << reward.amount << ")\n";
                }
                break;
            case QuestRewardKind::Purity:
                if (reward.amount != 0) {
                    lua << "    Stats.ModifyPurity(QuestManager.HeroEntity, "
                        << reward.amount << ")\n";
                }
                break;
        }
    }
    lua << "    QuestTracker.SetAsCompleted(QuestManager.HeroEntity, self.QuestName, true, true)\n"
        << "  else\n"
        << "    QuestTracker.SetAsFailed(QuestManager.HeroEntity, self.QuestName)\n"
        << "  end\n"
        << "end\n\n";

    auto emit_return_logic = [&](std::ostringstream& out) {
        out << "    if self.ParentQuest.HasRequiredItem then\n"
            << "      if self.CurrentState == 0 then\n"
            << "        QuestTracker.SetObjectiveEntity(QuestManager.HeroEntity, self.ParentQuest.QuestName, self.Entity, true)\n"
            << "        self.CurrentState = 1\n"
            << "      end\n"
            << "      if self.Interacted then\n"
            << "        QuestTracker.SetObjectiveEntity(QuestManager.HeroEntity, self.ParentQuest.QuestName, self.Entity, false)\n";
        if (returning.remove_item) {
            out << "        Inventory.RemoveAllItemsOfType(QuestManager.HeroEntity, "
                << lua_quote(returning.item.internal_name) << ")\n";
        }
        out << "        self.ParentQuest.MissionSucceeded = true\n"
            << "        self.CurrentState = 2\n"
            << "      end\n"
            << "    end\n";
    };

    lua << "QuestManager.NewEntityThread("
        << lua_quote(approach.entity.entity_name) << ")\n\n"
        << "function " << approach.entity.entity_name << ":Init()\n"
        << "  self.CurrentState = 0\n"
        << "  self.OfferInProgress = false\n"
        << "end\n\n"
        << "function " << approach.entity.entity_name << ":CustomUpdate()\n"
        << "  QuestTracker.SetQuestGiver(QuestManager.HeroEntity, self.ParentQuest.QuestName, self.Entity)\n"
        << "  while true do\n"
        << "    if not self.ParentQuest.MissionStarted and "
        << "IsDistanceBetweenThingsUnder(self.Entity, QuestManager.HeroEntity, "
        << approach.approach_radius << ") and not self.OfferInProgress then\n"
        << "      self.OfferInProgress = true\n";
    if (dialogue.entity.entity_name == approach.entity.entity_name) {
        lua << "      ScriptFunction.SaySimLine(self.Entity, "
            << lua_quote(dialogue_tag) << ")\n";
    } else {
        lua << "      local speaker = self:GetEntityWithName("
            << lua_quote(dialogue.entity.entity_name) << ", \"creature\")\n"
            << "      if speaker and speaker:IsAlive() then\n"
            << "        ScriptFunction.SaySimLine(speaker, "
            << lua_quote(dialogue_tag) << ")\n"
            << "      end\n";
    }
    lua << "      local accepted = self:ShowToasterAcceptBoxWithDialogueUntilCondition(\n"
        << "        " << lua_quote(accept_tag) << ", "
        << lua_quote("Quest_" + quest.quest_id) << ",\n"
        << "        {QuestName = self.ParentQuest.QuestName})\n"
        << "      if accepted then\n"
        << "        self.ParentQuest:BeginObjective()\n"
        << "      end\n"
        << "      while IsDistanceBetweenThingsUnder(self.Entity, QuestManager.HeroEntity, "
        << approach.approach_radius << ") do coroutine.yield() end\n"
        << "      self.OfferInProgress = false\n"
        << "    end\n";
    if (same_return_actor) emit_return_logic(lua);
    lua << "    coroutine.yield()\n"
        << "  end\n"
        << "end\n";

    if (!same_return_actor) {
        lua << "\nQuestManager.NewEntityThread("
            << lua_quote(returning.entity.entity_name) << ")\n\n"
            << "function " << returning.entity.entity_name << ":Init()\n"
            << "  self.CurrentState = 0\n"
            << "end\n\n"
            << "function " << returning.entity.entity_name << ":CustomUpdate()\n"
            << "  while true do\n";
        emit_return_logic(lua);
        lua << "    coroutine.yield()\n"
            << "  end\n"
            << "end\n";
    }
    return lua.str();
}

std::string GenerateEligibilityLua(const AuthoredQuest& quest) {
    std::string start = "ScriptEnum.GAMEFLOW_START";
    std::string end = "ScriptEnum.GAMEFLOW_END";
    std::vector<std::string> requirements;
    auto append_prerequisite = [&](const AuthoredNode& node) {
        if (node.kind == AuthoredNodeKind::PrerequisiteStoryProgress) {
            if (!node.story_start.empty()) start = node.story_start;
            if (!node.story_end.empty()) end = node.story_end;
            return;
        }
        const std::string expression = prerequisite_expression(node);
        if (!expression.empty()) requirements.push_back(expression);
    };
    for (const AuthoredNode& prerequisite : quest.prerequisites) {
        append_prerequisite(prerequisite);
    }


    for (const AuthoredNode& node : quest.nodes) {
        if (IsPrerequisiteNode(node.kind)) append_prerequisite(node);
    }

    std::string unavailable = "false";
    if (!requirements.empty()) {
        std::ostringstream joined;
        for (std::size_t i = 0; i < requirements.size(); ++i) {
            if (i) joined << " and ";
            joined << '(' << requirements[i] << ')';
        }
        unavailable = "not (" + joined.str() + ")";
    }
    std::ostringstream lua;
    lua << "gameflow:CheckQuestEligibility(\"" << quest.quest_id
        << "\", " << start << ", " << end << ", 0, 0, "
        << unavailable << ")";
    return lua.str();
}

std::string GenerateAuthoringPreview(const AuthoredQuest& quest) {
    std::ostringstream preview;
    preview << "-- Quest script\n" << GenerateQuestLua(quest)
            << "\n-- GameflowQuestUnlocker:Update() eligibility entry\n"
            << GenerateEligibilityLua(quest) << '\n';
    return preview.str();
}

bool PatchGameflowEligibility(const std::string& source,
                              const std::string& quest_id,
                              const std::string& eligibility_lua,
                              std::string& patched,
                              std::string& error) {
    patched.clear();
    error.clear();
    if (!IsValidQuestId(quest_id) || eligibility_lua.empty()) {
        error = "invalid authored quest eligibility";
        return false;
    }

    const std::string newline =
        source.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    const std::string begin_marker =
        "-- FABLE2_ASSET_BROWSER QUEST " + quest_id + " BEGIN";
    const std::string end_marker =
        "-- FABLE2_ASSET_BROWSER QUEST " + quest_id + " END";

    const std::size_t existing_begin = source.find(begin_marker);
    if (existing_begin != std::string::npos) {
        const std::size_t existing_end =
            source.find(end_marker, existing_begin + begin_marker.size());
        if (existing_end == std::string::npos) {
            error = "existing quest eligibility marker is incomplete";
            return false;
        }
        const std::size_t line_start_pos = source.rfind('\n', existing_begin);
        const std::size_t line_start =
            line_start_pos == std::string::npos ? 0 : line_start_pos + 1;
        const std::size_t non_space =
            source.find_first_not_of(" \t\r", line_start);
        const std::string indent = non_space == std::string::npos
            ? std::string{} : source.substr(line_start, non_space - line_start);
        std::size_t replace_end =
            source.find('\n', existing_end + end_marker.size());
        if (replace_end == std::string::npos) replace_end = source.size();
        else ++replace_end;
        const std::string block =
            indent + begin_marker + newline +
            indent + eligibility_lua + newline +
            indent + end_marker + newline;
        patched = source;
        patched.replace(line_start, replace_end - line_start, block);
        return true;
    }

    const std::size_t update =
        source.find("function GameflowQuestUnlocker:Update()");
    if (update == std::string::npos) {
        error = "GameflowQuestUnlocker:Update was not found";
        return false;
    }
    const std::size_t loop = source.find("while true do", update);
    if (loop == std::string::npos) {
        error = "GameflowQuestUnlocker update loop was not found";
        return false;
    }
    const std::size_t gate =
        source.find("Gameflow.GameflowPositionUpdated", loop);
    if (gate == std::string::npos) {
        error = "gameflow eligibility gate was not found";
        return false;
    }
    const std::size_t gate_line_start_pos = source.rfind('\n', gate);
    const std::size_t gate_line_start = gate_line_start_pos == std::string::npos
        ? 0 : gate_line_start_pos + 1;
    const std::size_t gate_non_space =
        source.find_first_not_of(" \t\r", gate_line_start);
    const std::string gate_indent = gate_non_space == std::string::npos
        ? std::string{}
        : source.substr(gate_line_start, gate_non_space - gate_line_start);
    std::size_t insertion = source.find('\n', gate);
    if (insertion == std::string::npos) insertion = source.size();
    else ++insertion;

    const std::string indent = gate_indent + "\t";
    const std::string block =
        indent + begin_marker + newline +
        indent + eligibility_lua + newline +
        indent + end_marker + newline;
    patched = source;
    patched.insert(insertion, block);
    return true;
}

}
