#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Quest {

enum class PrerequisiteKind {
    StoryProgress,
    QuestState,
    GameflowFlag,
    LuaCondition,
};

enum class RequiredQuestState {
    Registered,
    Activated,
    Active,
    Completed,
    Unlocked,
};

enum class HeroRequirementKind {
    Renown,
    Alignment,
    Purity,
    Gold,
    AbilityLevel,
    Age,
    Gender,
    Married,
    SpouseCount,
    HasChildren,
};

enum class NumericComparison {
    AtLeast,
    AtMost,
    Exactly,
};

enum class QuestRewardKind {
    Gold,
    Renown,
    GeneralExperience,
    StrengthExperience,
    SkillExperience,
    WillExperience,
    Item,
    Morality,
    Purity,
};

enum class AuthoredNodeKind {
    PrerequisiteStoryProgress,
    PrerequisiteQuestState,
    PrerequisiteGameflowFlag,
    PrerequisiteLuaCondition,
    PrerequisiteHeroRequirement,
    QuestStart,
    ApproachNpc,
    Dialogue,
    AcceptQuest,
    ObtainItem,
    ReturnToNpc,
    CompleteQuest,
};

struct WorldReference {
    std::string level_path;
    std::string level_id;
    std::string entity_name;
    uint32_t entity_hash = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    std::vector<uint32_t> model_hashes;
    bool authored_instance = false;

    bool valid() const {
        return !level_id.empty() && !entity_name.empty() &&
               (entity_hash != 0 || authored_instance);
    }
};

struct ItemReference {
    uint32_t record_hash = 0;
    std::string internal_name;
    std::string display_name;
    std::string model_path;
    WorldReference source;

    bool valid() const {
        return record_hash != 0 && !internal_name.empty();
    }
};

struct AuthoredReward {
    int id = 0;
    QuestRewardKind kind = QuestRewardKind::Gold;
    int amount = 0;
    ItemReference item;
    int item_count = 1;
};

struct AuthoredNode {
    int id = 0;
    AuthoredNodeKind kind = AuthoredNodeKind::Dialogue;
    float x = 0.0f;
    float y = 0.0f;


    WorldReference entity;
    float approach_radius = 3.0f;


    std::string text;


    ItemReference item;
    int item_count = 1;
    bool remove_item = false;


    std::string story_start;
    std::string story_end;
    std::string other_quest;
    RequiredQuestState quest_state = RequiredQuestState::Completed;
    std::string gameflow_flag;
    std::string lua_condition;
    HeroRequirementKind hero_requirement = HeroRequirementKind::Renown;
    NumericComparison numeric_comparison = NumericComparison::AtLeast;
    std::string hero_value = "0";
    std::string hero_option;
    bool expected = true;
};

struct AuthoredLink {
    int id = 0;
    int from_node = 0;
    int to_node = 0;
};

struct AuthoredQuest {
    std::string quest_id;
    std::string quest_title;
    int next_node_id = 1;
    int next_link_id = 1;
    int next_prerequisite_id = 1;
    int next_reward_id = 1;
    std::vector<AuthoredNode> nodes;
    std::vector<AuthoredLink> links;



    std::vector<AuthoredNode> prerequisites;


    std::vector<AuthoredReward> rewards;
};

bool IsValidQuestId(const std::string& quest_id);
AuthoredQuest CreateAuthoredQuest(const std::string& quest_id);
int AddAuthoredNode(AuthoredQuest& quest, AuthoredNodeKind kind,
                    float x, float y);
bool RemoveAuthoredNode(AuthoredQuest& quest, int node_id);
int AddAuthoredLink(AuthoredQuest& quest, int from_node, int to_node);
bool RemoveAuthoredLink(AuthoredQuest& quest, int link_id);
AuthoredNode* FindAuthoredNode(AuthoredQuest& quest, int node_id);
const AuthoredNode* FindAuthoredNode(const AuthoredQuest& quest, int node_id);
bool IsPrerequisiteNode(AuthoredNodeKind kind);
int AddAuthoredPrerequisite(AuthoredQuest& quest, AuthoredNodeKind kind);
bool RemoveAuthoredPrerequisite(AuthoredQuest& quest, int prerequisite_id);
int AddAuthoredReward(AuthoredQuest& quest, QuestRewardKind kind);
bool RemoveAuthoredReward(AuthoredQuest& quest, int reward_id);

const char* AuthoredNodeKindName(AuthoredNodeKind kind);
const char* RequiredQuestStateName(RequiredQuestState state);
const char* HeroRequirementKindName(HeroRequirementKind kind);
const char* NumericComparisonName(NumericComparison comparison);
const char* QuestRewardKindName(QuestRewardKind kind);
const std::vector<std::string>& HeroAbilityTypes();
bool ParseAuthoredInteger(const std::string& text, int& value);

struct StoryProgressMilestone {
    const char* value;
    const char* title_tag;
    const char* fallback_title;
    const char* stage_detail;
};

const std::vector<StoryProgressMilestone>& StoryProgressMilestones();

bool ValidateSimpleQuest(const AuthoredQuest& quest, std::string& error);
std::vector<std::pair<std::string, std::string>>
AuthoredTextEntries(const AuthoredQuest& quest);



std::string GenerateQuestLua(const AuthoredQuest& quest);
std::string GenerateEligibilityLua(const AuthoredQuest& quest);
std::string GenerateAuthoringPreview(const AuthoredQuest& quest);



bool PatchGameflowEligibility(const std::string& source,
                              const std::string& quest_id,
                              const std::string& eligibility_lua,
                              std::string& patched,
                              std::string& error);

}
