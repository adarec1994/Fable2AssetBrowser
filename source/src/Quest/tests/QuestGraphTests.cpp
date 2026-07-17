#include "Quest/QuestGraph.h"
#include "Quest/QuestAuthoring.h"
#include "Quest/QuestStoryGraph.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>

namespace {

const Quest::GraphNode* find_node(const Quest::Graph& graph,
                                  Quest::NodeKind kind,
                                  const std::string& title_fragment) {
    for (const Quest::GraphNode& node : graph.nodes) {
        if (node.kind == kind &&
            node.title.find(title_fragment) != std::string::npos) return &node;
    }
    return nullptr;
}

bool has_detail(const Quest::GraphNode& node, const std::string& fragment) {
    for (const std::string& detail : node.details) {
        if (detail.find(fragment) != std::string::npos) return true;
    }
    return false;
}

bool has_link(const Quest::Graph& graph, int from, int to) {
    for (const Quest::GraphLink& link : graph.links) {
        if (link.from_node == from && link.to_node == to) return true;
    }
    return false;
}

bool has_link_label(const Quest::Graph& graph, int from, int to,
                    const std::string& label) {
    for (const Quest::GraphLink& link : graph.links) {
        if (link.from_node == from && link.to_node == to &&
            link.label == label) return true;
    }
    return false;
}

const Quest::GraphNode* find_badge(const Quest::Graph& graph,
                                   const std::string& badge,
                                   const std::string& title_fragment = {}) {
    for (const Quest::GraphNode& node : graph.nodes) {
        if (node.badge == badge &&
            (title_fragment.empty() ||
             node.title.find(title_fragment) != std::string::npos)) {
            return &node;
        }
    }
    return nullptr;
}

const Quest::GraphNode* find_detail_node(const Quest::Graph& graph,
                                         const std::string& fragment) {
    for (const Quest::GraphNode& node : graph.nodes) {
        if (has_detail(node, fragment)) return &node;
    }
    return nullptr;
}

const Quest::QuestEvent* find_event(
    const Quest::Graph& graph, Quest::QuestEventKind kind,
    const std::string& value = {}) {
    for (const Quest::GraphNode& node : graph.nodes) {
        for (const Quest::QuestEvent& event : node.events) {
            if (event.kind != kind) continue;
            if (value.empty() || event.title.find(value) != std::string::npos ||
                event.target.find(value) != std::string::npos ||
                event.item.find(value) != std::string::npos) {
                return &event;
            }
        }
    }
    return nullptr;
}

}

int main() {
    assert(Quest::IsValidQuestId("QO999_CustomQuest"));
    assert(!Quest::IsValidQuestId("999 Bad Quest"));
    Quest::AuthoredQuest authored =
        Quest::CreateAuthoredQuest("QO999_CustomQuest");
    assert(authored.nodes.size() == 1);
    assert(authored.links.empty());
    const int quest_start_id = authored.nodes.front().id;
    assert(authored.nodes.front().kind ==
           Quest::AuthoredNodeKind::QuestStart);
    assert(!Quest::RemoveAuthoredNode(authored, quest_start_id));
    const int story_id = Quest::AddAuthoredNode(
        authored, Quest::AuthoredNodeKind::PrerequisiteStoryProgress,
        10.0f, 20.0f);
    authored.nodes.back().story_start = "ScriptEnum.DebugQC100";
    authored.nodes.back().story_end = "ScriptEnum.DebugQC140";
    const int quest_state_id = Quest::AddAuthoredNode(
        authored, Quest::AuthoredNodeKind::PrerequisiteQuestState,
        30.0f, 40.0f);
    authored.nodes.back().other_quest = "QO100_BrightwoodFarmer";
    authored.nodes.back().quest_state =
        Quest::RequiredQuestState::Completed;
    const int flag_id = Quest::AddAuthoredNode(
        authored, Quest::AuthoredNodeKind::PrerequisiteGameflowFlag,
        50.0f, 60.0f);
    authored.nodes.back().gameflow_flag = "GypsiesNeeded";
    authored.nodes.back().expected = false;
    const std::string eligibility = Quest::GenerateEligibilityLua(authored);
    assert(eligibility.find("ScriptEnum.DebugQC100") != std::string::npos);
    assert(eligibility.find("ScriptEnum.DebugQC140") != std::string::npos);
    assert(eligibility.find("QuestTracker.IsCompleted") !=
           std::string::npos);
    assert(eligibility.find("not (Gameflow.GypsiesNeeded)") !=
           std::string::npos);
    int parsed_integer = 0;
    assert(Quest::ParseAuthoredInteger("5000", parsed_integer));
    assert(parsed_integer == 5000);
    assert(!Quest::ParseAuthoredInteger("5000.5", parsed_integer));
    const auto& story_milestones = Quest::StoryProgressMilestones();
    assert(story_milestones.size() == 21);
    const auto story_stage = [&](const std::string& value) {
        return std::find_if(
            story_milestones.begin(), story_milestones.end(),
            [&](const Quest::StoryProgressMilestone& milestone) {
                return value == milestone.value;
            });
    };
    const auto road_to_westcliff =
        story_stage("ScriptEnum.DebugQC110");
    assert(road_to_westcliff != story_milestones.end());
    assert(std::string(road_to_westcliff->fallback_title) ==
           "Road to Westcliff");
    assert(story_stage("ScriptEnum.DebugQC170") !=
           story_milestones.end());
    assert(story_stage("ScriptEnum.DebugQC220") !=
           story_milestones.end());
    const int hero_requirement_id = Quest::AddAuthoredPrerequisite(
        authored,
        Quest::AuthoredNodeKind::PrerequisiteHeroRequirement);
    assert(hero_requirement_id != 0);
    authored.prerequisites.back().hero_requirement =
        Quest::HeroRequirementKind::Gold;
    authored.prerequisites.back().numeric_comparison =
        Quest::NumericComparison::AtLeast;
    authored.prerequisites.back().hero_value = "5000";
    const std::string hero_eligibility =
        Quest::GenerateEligibilityLua(authored);
    assert(hero_eligibility.find(
               "Money.Get(QuestManager.HeroEntity) >= 5000") !=
           std::string::npos);
    assert(Quest::RemoveAuthoredPrerequisite(authored,
                                              hero_requirement_id));

    authored.quest_title = "A Small Favour";
    const int approach_id = Quest::AddAuthoredNode(
        authored, Quest::AuthoredNodeKind::ApproachNpc, 0.0f, 100.0f);
    auto* approach = Quest::FindAuthoredNode(authored, approach_id);
    approach->entity.level_id = "BowerLake";
    approach->entity.level_path = "worlds\\albion\\bowerlake";
    approach->entity.entity_name = "QO999_TestGiver";
    approach->entity.authored_instance = true;
    assert(approach->entity.valid());
    const int dialogue_id = Quest::AddAuthoredNode(
        authored, Quest::AuthoredNodeKind::Dialogue, 0.0f, 200.0f);
    auto* dialogue = Quest::FindAuthoredNode(authored, dialogue_id);
    dialogue->entity =
        Quest::FindAuthoredNode(authored, approach_id)->entity;
    dialogue->text = "I need your help.";
    const int accept_id = Quest::AddAuthoredNode(
        authored, Quest::AuthoredNodeKind::AcceptQuest, 0.0f, 300.0f);
    Quest::FindAuthoredNode(authored, accept_id)->text =
        "Will you help me?";
    const int obtain_id = Quest::AddAuthoredNode(
        authored, Quest::AuthoredNodeKind::ObtainItem, 0.0f, 400.0f);
    auto* obtain = Quest::FindAuthoredNode(authored, obtain_id);
    obtain->item.record_hash = 0x5678;
    obtain->item.internal_name = "ObjectInventoryGiftToyDoll";
    obtain->item.display_name = "Toy Doll";
    obtain->item.source.level_id = "BowerLake";
    obtain->item.source.entity_name = "QO999_ItemChest";
    obtain->item.source.entity_hash = 0x9ABC;
    obtain->text = "Find the toy doll.";
    const int return_id = Quest::AddAuthoredNode(
        authored, Quest::AuthoredNodeKind::ReturnToNpc, 0.0f, 500.0f);
    auto* returning = Quest::FindAuthoredNode(authored, return_id);
    returning->entity =
        Quest::FindAuthoredNode(authored, approach_id)->entity;
    returning->text = "Return to the quest giver.";
    returning->remove_item = true;
    returning->item =
        Quest::FindAuthoredNode(authored, obtain_id)->item;
    const int completion_id = Quest::AddAuthoredNode(
        authored, Quest::AuthoredNodeKind::CompleteQuest, 0.0f, 600.0f);
    const int gold_reward = Quest::AddAuthoredReward(
        authored, Quest::QuestRewardKind::Gold);
    authored.rewards.back().amount = 500;
    assert(gold_reward != 0);
    Quest::AddAuthoredReward(authored, Quest::QuestRewardKind::Renown);
    authored.rewards.back().amount = 250;
    Quest::AddAuthoredReward(
        authored, Quest::QuestRewardKind::GeneralExperience);
    authored.rewards.back().amount = 1000;
    Quest::AddAuthoredReward(authored, Quest::QuestRewardKind::Item);
    authored.rewards.back().item =
        Quest::FindAuthoredNode(authored, obtain_id)->item;
    authored.rewards.back().item_count = 2;
    Quest::AddAuthoredReward(authored, Quest::QuestRewardKind::Morality);
    authored.rewards.back().amount = -10;
    Quest::AddAuthoredReward(authored, Quest::QuestRewardKind::Purity);
    authored.rewards.back().amount = 5;
    for (int prerequisite : {story_id, quest_state_id, flag_id}) {
        Quest::AddAuthoredLink(authored, prerequisite, quest_start_id);
    }
    Quest::AddAuthoredLink(authored, quest_start_id, approach_id);
    Quest::AddAuthoredLink(authored, approach_id, dialogue_id);
    Quest::AddAuthoredLink(authored, dialogue_id, accept_id);
    Quest::AddAuthoredLink(authored, accept_id, obtain_id);
    Quest::AddAuthoredLink(authored, obtain_id, return_id);
    Quest::AddAuthoredLink(authored, return_id, completion_id);
    std::string authored_error;
    assert(Quest::ValidateSimpleQuest(authored, authored_error));
    const std::string simple_lua = Quest::GenerateQuestLua(authored);
    assert(simple_lua.find(
               "QuestManager.NewEntityThread(\"QO999_TestGiver\")") !=
           std::string::npos);
    assert(simple_lua.find("Inventory.GetNumberOfItemsOfType") !=
           std::string::npos);
    assert(simple_lua.find("Inventory.RemoveAllItemsOfType") !=
           std::string::npos);
    assert(simple_lua.find("Money.Modify(QuestManager.HeroEntity, 500)") !=
           std::string::npos);
    assert(simple_lua.find(
               "Stats.ModifyRenown(QuestManager.HeroEntity, 250)") !=
           std::string::npos);
    assert(simple_lua.find(
               "EExperienceType.EXPERIENCE_GENERAL, 1000, false") !=
           std::string::npos);
    assert(simple_lua.find(
               "Inventory.AddItemOfType(QuestManager.HeroEntity, \"ObjectInventoryGiftToyDoll\")") !=
           std::string::npos);
    assert(simple_lua.find(
               "Stats.ModifyMorality(QuestManager.HeroEntity, -10)") !=
           std::string::npos);
    assert(simple_lua.find(
               "Stats.ModifyPurity(QuestManager.HeroEntity, 5)") !=
           std::string::npos);
    assert(simple_lua.find("QuestTracker.ClearAllObjectiveEntities") !=
           std::string::npos);
    assert(simple_lua.find("QuestTracker.SetObjectiveAsCompleted") !=
           std::string::npos);
    assert(simple_lua.find(
               "QuestTracker.SetAsCompleted(QuestManager.HeroEntity, self.QuestName, true, true)") !=
           std::string::npos);
    const std::size_t approach_call =
        simple_lua.find("IsDistanceBetweenThingsUnder");
    const std::size_t dialogue_call =
        simple_lua.find("ScriptFunction.SaySimLine");
    const std::size_t accept_call =
        simple_lua.find(
            "ShowToasterAcceptBoxWithDialogueUntilCondition");
    assert(approach_call < dialogue_call && dialogue_call < accept_call);
    assert(simple_lua.find(
               "QuestTracker.SetAsPrimary(QuestManager.HeroEntity, self.QuestName)") !=
           std::string::npos);
    assert(simple_lua.find("GUI.AskYesNoQuestion") == std::string::npos);
    const auto authored_text = Quest::AuthoredTextEntries(authored);
    assert(authored_text.size() == 5);
    assert(authored_text.front().second == "A Small Favour");
    assert(Quest::RemoveAuthoredNode(authored, story_id));

    Quest::AuthoredQuest skip_patch =
        Quest::CreateAuthoredQuest("QMOD_SkipChildhood");
    skip_patch.quest_title = "Skip the Introduction?";
    assert(skip_patch.nodes.size() == 1);
    const int skip_start_id = skip_patch.nodes.front().id;
    const int skip_approach_id = Quest::AddAuthoredNode(
        skip_patch, Quest::AuthoredNodeKind::ApproachNpc,
        0.0f, 100.0f);
    auto* skip_approach =
        Quest::FindAuthoredNode(skip_patch, skip_approach_id);
    skip_approach->entity.level_id = "Bowerstone\\BWSSlums";
    skip_approach->entity.level_path =
        "worlds\\albion\\bowerstone\\bwsslums";
    skip_approach->entity.entity_name =
        "QMOD_SkipChildhood_QuestGiver";
    skip_approach->entity.authored_instance = true;
    skip_approach->approach_radius = 3.5f;
    const int hold_id = Quest::AddAuthoredNode(
        skip_patch, Quest::AuthoredNodeKind::HoldInteraction,
        0.0f, 200.0f);
    Quest::FindAuthoredNode(skip_patch, hold_id)->text =
        "Hold A to skip the childhood introduction.";
    const int skip_ending_id = Quest::AddAuthoredNode(
        skip_patch, Quest::AuthoredNodeKind::SkipChildhoodEnding,
        0.0f, 300.0f);
    Quest::AddAuthoredLink(skip_patch, skip_start_id,
                           skip_approach_id);
    Quest::AddAuthoredLink(skip_patch, skip_approach_id, hold_id);
    Quest::AddAuthoredLink(skip_patch, hold_id, skip_ending_id);
    assert(Quest::ValidateSimpleQuest(skip_patch, authored_error));
    const std::string skip_lua =
        Quest::GenerateQuestLua(skip_patch);
    assert(skip_lua.find(
               "QuestManager.NewEntityThread("
               "\"QMOD_SkipChildhood_QuestGiver\")") !=
           std::string::npos);
    assert(skip_lua.find("GUI.DisplayInfoBoxParams") !=
           std::string::npos);
    assert(skip_lua.find("IsHoldAButton = true") !=
           std::string::npos);
    assert(skip_lua.find(
               "Gameflow.ChildhoodVars.SkipToLuciensStudy()") !=
           std::string::npos);
    assert(skip_lua.find("created_dog:IsAlive()") !=
           std::string::npos);
    assert(skip_lua.find("childhood.EndChildhood = true") !=
           std::string::npos);
    assert(skip_lua.find("Fall_m.bik") == std::string::npos);
    const std::string skip_eligibility =
        Quest::GenerateEligibilityLua(skip_patch);
    assert(skip_eligibility.find("ScriptEnum.GAMEFLOW_START") !=
           std::string::npos);
    assert(skip_eligibility.find("ScriptEnum.DebugQC060") !=
           std::string::npos);
    const auto skip_text = Quest::AuthoredTextEntries(skip_patch);
    assert(skip_text.size() == 3);
    assert(skip_text[1].second == "Skip");
    skip_approach->entity.authored_instance = false;
    assert(!Quest::ValidateSimpleQuest(skip_patch, authored_error));
    assert(authored_error.find("newly placed custom NPC") !=
           std::string::npos);
    skip_approach->entity.authored_instance = true;

    const std::string gameflow_source =
        "function GameflowQuestUnlocker:Update()\r\n"
        "\twhile true do\r\n"
        "\t\tif (Gameflow.GameflowPositionUpdated) then\r\n"
        "\t\t\tgameflow:CheckQuestEligibility(\"Existing\", A, B, 0, 0, false)\r\n"
        "\t\tend\r\n"
        "\tend\r\n"
        "end\r\n";
    std::string patched_gameflow;
    std::string patch_error;
    assert(Quest::PatchGameflowEligibility(
        gameflow_source, authored.quest_id, eligibility,
        patched_gameflow, patch_error));
    assert(patched_gameflow.find(eligibility) != std::string::npos);
    const std::size_t first_marker = patched_gameflow.find(
        "FABLE2_ASSET_BROWSER QUEST QO999_CustomQuest BEGIN");
    assert(first_marker != std::string::npos);
    const std::string changed_eligibility =
        "gameflow:CheckQuestEligibility(\"QO999_CustomQuest\", A, B, 0, 0, false)";
    std::string repatched_gameflow;
    assert(Quest::PatchGameflowEligibility(
        patched_gameflow, authored.quest_id, changed_eligibility,
        repatched_gameflow, patch_error));
    assert(repatched_gameflow.find(changed_eligibility) != std::string::npos);
    assert(repatched_gameflow.find(
               "FABLE2_ASSET_BROWSER QUEST QO999_CustomQuest BEGIN",
               first_marker + 1) == std::string::npos);

    const std::vector<std::string> string_literals =
        Quest::FindLuaStringLiterals(
            "print(\"Victor's experiment\") "
            "self:GetEntityWithName(\"QO570_DigSpot\", \"marker\")");
    assert(std::find(string_literals.begin(), string_literals.end(),
                     "QO570_DigSpot") != string_literals.end());

    const char* source = R"LUA(
QuestManager.NewQuestThread("QTest")
QuestManager.NewEntityThread("QTestActor")

function QTest:Init()
  self.CurrentState = 0
  self.Rose = self:GetEntityWithName("Rose", "creature")
  self.MeetingPoint = CVector3(10.5, 2, -7.25)
  self.GreetingLine = "TEXT_QTEST_ROSE_HELLO"
end

function QTest:Update()
  if self.CurrentState == 0 then
    ScriptFunction.SaySimLine(self.Rose, self.GreetingLine)
    self:PlayCutscene({Cutscene = "QTestRoseGreeting"})
    self:SetLookAtCamera({
      source_position = CVector3(11, 3, -6),
      target_position = CVector3(10, 2, -7),
      fov = 50
    })
    self:MoveToPosition(self.Rose, self.MeetingPoint)
    self:StartNewEntityThread("Rose", QTestActor)
    self.CurrentState = 1
  elseif self.CurrentState == 1 then
    self:SetObjectiveTag("TEXT_QTEST_GO_TO_SLUMS")
    self:LoadAndWaitForLevel("Albion", "BowerstoneSlums", "")
    Money.Modify(QuestManager.HeroEntity, 500)
    QuestTracker.SetAsCompleted(QuestManager.HeroEntity, self.QuestName)
  end
end

function QTestActor:CustomUpdate()
  if self.CurrentState == 0 then
    self:WaitForTriggerToFire("QTestMarketTrigger")
    self.CurrentState = 1
  elseif self.CurrentState == 1 then
    CombatRegister:Attack(self.Entity, QuestManager.HeroEntity)
    ScriptFunction.SaySimLine(self.Entity, "TEXT_QTEST_ROSE_DONE")
  end
end
)LUA";

    Quest::ReferenceCatalog references;
    references.localized_text["TEXT_QTEST_ROSE_HELLO"] =
        "Meet me by the market.";
    references.localized_text["TEXT_QTEST_GO_TO_SLUMS"] =
        "Go to Bowerstone Slums.";
    references.localized_text["TEXT_QTEST_ROSE_SCENE_10"] =
        "Come on. We should get moving.";
    references.localized_text["TEXT_QTEST_ROSE_DONE"] =
        "You did it!";
    references.cutscenes["QTestRoseGreeting"].dialogue_tags.push_back(
        "TEXT_QTEST_ROSE_SCENE_10");
    references.cutscenes["QTestRoseGreeting"].dialogue_lines.push_back(
        {"TEXT_QTEST_ROSE_SCENE_10", "Rose"});
    references.cutscenes["QTestRoseGreeting"].speakers.push_back("Rose");
    Quest::CutsceneTimelineEntry greeting_animation;
    greeting_animation.kind = Quest::CutsceneTimelineKind::ActorAction;
    greeting_animation.description = "Rose plays the Greeting Wave animation";
    greeting_animation.metadata.push_back("Animation ID: GreetingWave");
    references.cutscenes["QTestRoseGreeting"].timeline.push_back(
        std::move(greeting_animation));
    Quest::CutsceneTimelineEntry greeting_dialogue;
    greeting_dialogue.kind = Quest::CutsceneTimelineKind::Dialogue;
    greeting_dialogue.text_tag = "TEXT_QTEST_ROSE_SCENE_10";
    greeting_dialogue.speaker = "Rose";
    references.cutscenes["QTestRoseGreeting"].timeline.push_back(
        std::move(greeting_dialogue));
    references.audio_assets.push_back(
        "audio/quests/qtest/TEXT_QTEST_ROSE_HELLO.wav");
    references.audio_assets.push_back(
        "language/speech/TEXT_QTEST_ROSE_SCENE_10.wav");
    references.audio_by_dialogue["text_qtest_rose_hello"].push_back(
        "audio/quests/qtest/TEXT_QTEST_ROSE_HELLO.wav");
    references.audio_by_dialogue["text_qtest_rose_scene_10"].push_back(
        "language/speech/TEXT_QTEST_ROSE_SCENE_10.wav");
    references.level_assets.push_back(
        "levels/albion/bowerstone_slums.engine_level");

    const Quest::Graph graph = Quest::BuildGraph(
        "QTest.lua", source, references);
    assert(graph.quest_threads == 1);
    assert(graph.entity_threads == 1);
    assert(graph.functions == 3);
    assert(graph.flow_steps == 2);
    assert(graph.dialogue_lines == 3);
    assert(graph.audio_matches == 2);

    const Quest::GraphNode* quest = find_node(
        graph, Quest::NodeKind::Quest, "QTest");
    const Quest::GraphNode* step1 = find_node(
        graph, Quest::NodeKind::State, "Step 1");
    const Quest::GraphNode* step2 = find_node(
        graph, Quest::NodeKind::State, "Step 2");
    const Quest::GraphNode* rose = find_node(
        graph, Quest::NodeKind::Thread, "Rose - behaviour");
    const Quest::GraphNode* rose_phase1 = find_node(
        graph, Quest::NodeKind::State, "Rose - Phase 1");
    const Quest::GraphNode* rose_phase2 = find_node(
        graph, Quest::NodeKind::State, "Rose - Phase 2");
    assert(quest && step1 && step2 && rose && rose_phase1 && rose_phase2);

    assert(has_link(graph, quest->id, step1->id));
    assert(has_link(graph, step1->id, step2->id));
    assert(has_link(graph, step1->id, rose->id));
    assert(has_link(graph, rose->id, rose_phase1->id));
    assert(has_link(graph, rose_phase1->id, rose_phase2->id));

    assert(has_detail(*step1, "Rose says: \"Meet me by the market.\""));
    assert(has_detail(*step1, "Dialogue ID: TEXT_QTEST_ROSE_HELLO"));
    assert(has_detail(*step1, "TEXT_QTEST_ROSE_HELLO.wav"));
    assert(has_detail(*step1, "Come on. We should get moving."));
    assert(has_detail(*step1, "TEXT_QTEST_ROSE_SCENE_10.wav"));
    assert(has_detail(*step1, "X 10.5, Y 2, Z -7.25"));
    assert(has_detail(*step2, "Go to Bowerstone Slums."));
    assert(has_detail(*step2, "Albion"));
    assert(has_detail(*step2, "Bowerstone Slums"));
    assert(has_detail(*rose_phase1, "Wait for trigger QTest Market Trigger to fire"));
    assert(has_detail(*rose_phase2, "attacks Hero"));
    assert(!has_detail(*rose, "Runs behaviour for a named level entity"));

    const Quest::Graph story = Quest::BuildStoryGraph(
        "QTest.lua", source, references);
    assert(story.dialogue_lines == 3);
    const Quest::GraphNode* story_start = find_badge(story, "Quest start");
    const Quest::GraphNode* story_rose = find_detail_node(
        story, "Rose: \"Meet me by the market.\"");
    const Quest::GraphNode* story_scene = find_detail_node(
        story, "Rose: \"Come on. We should get moving.\"");
    const Quest::GraphNode* story_scene_animation = find_badge(
        story, "Actor action", "Rose plays the Greeting Wave animation");
    const Quest::GraphNode* story_move = find_badge(
        story, "Actor action", "Move Rose to a world position");
    const Quest::GraphNode* story_camera = find_badge(
        story, "Camera event", "camera focuses on a story point");
    const Quest::GraphNode* story_objective = find_node(
        story, Quest::NodeKind::Function, "Go to Bowerstone Slums.");
    const Quest::GraphNode* story_condition = find_node(
        story, Quest::NodeKind::State, "triggered?");
    const Quest::GraphNode* story_attack = find_badge(
        story, "Actor action", "attacks Hero");
    const Quest::GraphNode* story_done = find_detail_node(
        story, "Rose: \"You did it!\"");
    const Quest::GraphNode* story_end = find_node(
        story, Quest::NodeKind::Action, "Quest complete");
    const Quest::GraphNode* story_reward = find_node(
        story, Quest::NodeKind::Action, "Reward: 500 gold");
    assert(story_start && story_rose && story_scene &&
           story_scene_animation && story_camera && story_move && story_objective &&
           story_condition && story_attack && story_done && story_reward &&
           story_end);
    assert(story_rose->id != story_scene_animation->id);
    assert(story_scene_animation->id != story_scene->id);
    assert(story_rose->badge == "Dialogue");
    assert(story_scene_animation->badge == "Actor action");
    assert(story_scene_animation->subtitle.find("Scene: ") == 0);
    assert(story_scene->badge == "Dialogue");
    assert(has_link(story, story_rose->id, story_scene_animation->id));
    assert(has_link(story, story_scene_animation->id, story_scene->id));
    assert(has_link(story, story_scene->id, story_camera->id));
    assert(has_link(story, story_camera->id, story_move->id));
    assert(has_detail(*story_camera, "Camera position: X 11, Y 3, Z -6"));
    assert(has_detail(*story_camera, "Camera focus: X 10, Y 2, Z -7"));
    assert(has_detail(*story_camera, "Field of view: 50 degrees"));
    assert(!story_scene_animation->metadata.empty());
    bool yes_link = false;
    bool no_link = false;
    for (const Quest::GraphLink& link : story.links) {
        if (link.from_node == story_condition->id && link.label == "Yes") {
            yes_link = true;
        }
        if (link.from_node == story_condition->id && link.label == "No") {
            no_link = true;
        }
    }
    assert(yes_link && no_link);
    assert(has_link(story, story_attack->id, story_done->id));
    assert(has_detail(*story_move, "X 10.5, Y 2, Z -7.25"));
    assert(!story_scene->metadata.empty());
    for (const Quest::GraphNode& node : story.nodes) {
        assert(node.title.find("CustomUpdate") == std::string::npos);
        assert(node.title.find("script state") == std::string::npos);
        assert(node.title.find("Layer ID") == std::string::npos);
    }

    const char* gender_source = R"LUA(
QuestManager.NewQuestThread("QGender")
function QGender:Init()
  self.Rose = self:GetEntityWithName("Rose", "creature")
end
function QGender:Update()
  if self.CurrentState == 0 then
    ScriptFunction.SaySimLine(self.Rose, "TEXT_QGENDER_FOLLOW")
    self.CurrentState = 1
  elseif self.CurrentState == 1 then
    self:SetObjectiveTag("TEXT_QGENDER_CONTINUE")
  end
end
)LUA";
    Quest::ReferenceCatalog gender_references;
    gender_references.localized_text["TEXT_QGENDER_FOLLOW"] =
        "Male Hero: \"Come with me, little brother.\" / "
        "Female Hero: \"Come with me, sis.\"";
    gender_references.localized_text["TEXT_QGENDER_CONTINUE"] =
        "Continue the quest.";
    const Quest::Graph gender_story = Quest::BuildStoryGraph(
        "QGender.lua", gender_source, gender_references);
    const Quest::GraphNode* male_branch = find_node(
        gender_story, Quest::NodeKind::Thread, "If male");
    const Quest::GraphNode* female_branch = find_node(
        gender_story, Quest::NodeKind::Thread, "If female");
    const Quest::GraphNode* branch_merge = find_badge(
        gender_story, "Continue", "Rejoin quest flow");
    const Quest::GraphNode* gender_continue = find_node(
        gender_story, Quest::NodeKind::Function, "Continue the quest.");
    assert(male_branch && female_branch && gender_continue);
    assert(!branch_merge);
    assert(has_detail(*male_branch, "Come with me, little brother."));
    assert(has_detail(*female_branch, "Come with me, sis."));
    assert(has_link(gender_story, male_branch->id, gender_continue->id));
    assert(has_link(gender_story, female_branch->id, gender_continue->id));
    assert(!has_link(gender_story, male_branch->id, female_branch->id));
    assert(male_branch->x == female_branch->x);
    assert(female_branch->y > male_branch->y);
    assert(gender_continue->x > male_branch->x);
    assert(gender_continue->y > male_branch->y);
    assert(gender_continue->y < female_branch->y);

    const char* choice_source = R"LUA(
QuestManager.NewQuestThread("QChoice")
function QChoice:Init()
  self.Rose = self:GetEntityWithName("Rose", "creature")
end
function QChoice:Update()
  if self.CurrentState == 0 then
    self:SetObjectiveTag("TEXT_QCHOICE_CHOOSE")
    self.CurrentState = 1
    self.CurrentState = 2
  elseif self.CurrentState == 1 then
    ScriptFunction.SaySimLine(self.Rose, "TEXT_QCHOICE_KIND")
    self.CurrentState = 3
  elseif self.CurrentState == 2 then
    ScriptFunction.SaySimLine(self.Rose, "TEXT_QCHOICE_CRUEL")
    self.CurrentState = 3
  elseif self.CurrentState == 3 then
    self:SetObjectiveTag("TEXT_QCHOICE_CONTINUE")
  end
end
)LUA";
    Quest::ReferenceCatalog choice_references;
    choice_references.localized_text["TEXT_QCHOICE_CHOOSE"] =
        "Choose how to respond.";
    choice_references.localized_text["TEXT_QCHOICE_KIND"] =
        "That was kind of you.";
    choice_references.localized_text["TEXT_QCHOICE_CRUEL"] =
        "That was cruel.";
    choice_references.localized_text["TEXT_QCHOICE_CONTINUE"] =
        "Continue after the choice.";
    const Quest::Graph choice_story = Quest::BuildStoryGraph(
        "QChoice.lua", choice_source, choice_references);
    const Quest::GraphNode* choice = find_node(
        choice_story, Quest::NodeKind::Function, "Choose how to respond.");
    const Quest::GraphNode* kind_path = find_detail_node(
        choice_story, "That was kind of you.");
    const Quest::GraphNode* cruel_path = find_detail_node(
        choice_story, "That was cruel.");
    const Quest::GraphNode* after_choice = find_node(
        choice_story, Quest::NodeKind::Function,
        "Continue after the choice.");
    assert(choice && kind_path && cruel_path && after_choice);
    assert(has_link_label(choice_story, choice->id, kind_path->id, "Path 1"));
    assert(has_link_label(choice_story, choice->id, cruel_path->id, "Path 2"));
    assert(kind_path->x == cruel_path->x);
    assert(cruel_path->y > kind_path->y);
    assert(has_link(choice_story, kind_path->id, after_choice->id));
    assert(has_link(choice_story, cruel_path->id, after_choice->id));
    assert(after_choice->x > kind_path->x);
    assert(after_choice->y > kind_path->y);
    assert(after_choice->y < cruel_path->y);

    const char* frankenbride_source = R"LUA(
QuestManager.NewQuestThread("QO570_FrankenBride")
function QO570_FrankenBride:Update()
  self.HangingAroundTime = 45
end
)LUA";
    Quest::ReferenceCatalog frankenbride_references;
    frankenbride_references.localized_text["TEXT_QUEST_QO570_NAME"] =
        "Love Hurts";
    frankenbride_references.localized_text[
        "TEXT_QUEST_QO570_V2_TIMER_40"] =
        "Male Hero: \"Please leave.\" / Female Hero: \"Please go.\"";
    frankenbride_references.localized_text[
        "TEXT_QUEST_QO570_V2_GOOD_10"] =
        "I find you strangely alluring.";
    frankenbride_references.localized_text[
        "TEXT_QUEST_QO570_V2_EVIL_10"] =
        "You are the only one I could ever love.";
    frankenbride_references.world_entities["qo570_digspot"].push_back(
        {"Caves/Dunecrest/HobbeCave", 101.25, 7.5, -33.0});
    const Quest::Graph frankenbride_story = Quest::BuildStoryGraph(
        "qo570_frankenbride.lua", frankenbride_source,
        frankenbride_references);
    const Quest::GraphNode* quest_start = find_badge(
        frankenbride_story, "Quest start", "Resurrect Lady Grey");
    const Quest::GraphNode* lower_body = find_badge(
        frankenbride_story, "Quest step 2", "Recover the lower body");
    const Quest::GraphNode* ending_choice = find_badge(
        frankenbride_story, "Ending choice",
        "Does the Hero leave before 45 seconds expire?");
    const Quest::GraphNode* good_ending = find_badge(
        frankenbride_story, "Quest complete: Good ending",
        "Lady Grey marries Victor");
    const Quest::GraphNode* evil_ending = find_badge(
        frankenbride_story, "Quest complete: Evil ending",
        "Lady Grey remains available to the Hero");
    const Quest::GraphNode* timer_male = find_badge(
        frankenbride_story, "If male", "Victor pleads with a male Hero");
    const Quest::GraphNode* timer_female = find_badge(
        frankenbride_story, "If female", "Victor pleads with a female Hero");
    assert(quest_start && lower_body);
    assert(quest_start->subtitle.empty());
    assert(has_detail(*lower_body, "Dig spot marker: QO570_DigSpot"));
    assert(has_detail(*lower_body,
                      "Dig spot coordinates: X 101.25, Y 7.50, Z -33.00"));
    assert(ending_choice && good_ending && evil_ending);
    assert(timer_male && timer_female);
    assert(has_detail(*timer_male, "Please leave."));
    assert(has_detail(*timer_female, "Please go."));
    assert(good_ending->x > ending_choice->x);
    assert(evil_ending->x > ending_choice->x);
    assert(good_ending->y != evil_ending->y);
    assert(has_link_label(frankenbride_story, ending_choice->id,
                          ending_choice->id + 1, "Yes - leave"));
    assert(has_link_label(frankenbride_story, ending_choice->id,
                          evil_ending->id - 2, "No - stay"));

    const char* event_source = R"LUA(
QuestManager.NewQuestThread("QEventCoverage")
function QEventCoverage:Init()
  self.DigSpot = self:GetEntityWithName("QEvent_DigSpot", "object")
  self.LabDoor = self:GetEntityWithName("QEvent_LabDoor", "object")
end
function QEventCoverage:Update()
  if self.CurrentState == 0 then
    DiggingSpot.SetAsDiggableWithoutDog(self.DigSpot, true)
    self:WaitFor(function() return not DiggingSpot.IsActive(self.DigSpot) end)
    Inventory.AddItemOfType(QuestManager.HeroEntity, "ZombieBrideLegs")
    Inventory.RemoveAllItemsOfType(QuestManager.HeroEntity, "ZombieBrideLegs")
    GUI.SetTimer("EscapeTimer", 45)
    self:WaitFor(function() return self.EscapeTimer:GetTime() == 0 end)
    GUI.RemoveTimer("EscapeTimer")
    Door.SetLocked(self.LabDoor, false)
    Stats.ModifyMorality(QuestManager.HeroEntity, -10)
    CreatureGenerator.Trigger(self.Generator)
    self.ItemRemoved = true
    MessageEvents.IsMessageSentTo(
      EMessageEventType.MESSAGE_EVENT_INTERACTED, self.DigSpot)
  end
end
)LUA";
    Quest::ReferenceCatalog event_references;
    event_references.world_entities["qevent_digspot"].push_back(
        {"Caves/Dunecrest/HobbeCave", 101.25, 7.5, -33.0});
    const Quest::Graph event_graph = Quest::BuildGraph(
        "QEventCoverage.lua", event_source, event_references);
    const Quest::QuestEvent* dig_enable = find_event(
        event_graph, Quest::QuestEventKind::DigSpotEnable);
    const Quest::QuestEvent* dig_complete = find_event(
        event_graph, Quest::QuestEventKind::DigSpotComplete);
    const Quest::QuestEvent* item_add = find_event(
        event_graph, Quest::QuestEventKind::InventoryAdd,
        "ZombieBrideLegs");
    const Quest::QuestEvent* item_remove = find_event(
        event_graph, Quest::QuestEventKind::InventoryRemove,
        "ZombieBrideLegs");
    const Quest::QuestEvent* timer_start = find_event(
        event_graph, Quest::QuestEventKind::TimerStart);
    const Quest::QuestEvent* timer_wait = find_event(
        event_graph, Quest::QuestEventKind::TimerWait);
    const Quest::QuestEvent* timer_stop = find_event(
        event_graph, Quest::QuestEventKind::TimerStop);
    const Quest::QuestEvent* unlock = find_event(
        event_graph, Quest::QuestEventKind::DoorState, "Unlock");
    const Quest::QuestEvent* morality = find_event(
        event_graph, Quest::QuestEventKind::Morality);
    const Quest::QuestEvent* interaction = find_event(
        event_graph, Quest::QuestEventKind::Interaction);
    const Quest::QuestEvent* raw_generator_call = find_event(
        event_graph, Quest::QuestEventKind::ScriptCall,
        "CreatureGenerator.Trigger");
    const Quest::QuestEvent* raw_flag_change = find_event(
        event_graph, Quest::QuestEventKind::ActorState,
        "self.ItemRemoved");
    assert(dig_enable && dig_complete && item_add && item_remove);
    assert(timer_start && timer_wait && timer_stop && unlock && morality);
    assert(interaction);
    assert(raw_generator_call && raw_flag_change);
    assert(dig_enable->world.resolved);
    assert(dig_enable->world.level == "Caves/Dunecrest/HobbeCave");
    assert(dig_enable->world.x == 101.25 && dig_enable->world.y == 7.5 &&
           dig_enable->world.z == -33.0);
    assert(timer_start->duration_seconds == 45.0);
    assert(morality->amount == -10.0);
    assert(item_remove->source_class == "QEventCoverage");
    assert(item_remove->source_method == "Update");
    assert(item_remove->source_state == 0);
    assert(item_remove->source_line > 0);
    assert(!item_remove->source_statement.empty());

    const Quest::Graph event_story = Quest::BuildStoryGraph(
        "QEventCoverage.lua", event_source, event_references);
    assert(find_badge(event_story, "Quest item"));
    assert(find_badge(event_story, "Interaction"));
    assert(find_badge(event_story, "Timer"));
    assert(find_badge(event_story, "World event"));

    const char* reward_source = R"LUA(
QuestManager.NewQuestThread("QRewardCoverage")
function QRewardCoverage:Init()
  QuestTracker.Register(QuestManager.HeroEntity, self.QuestName, "Quest_QRewardCoverage")
end
function QRewardCoverage:Update()
  Stats.ModifyRenown(QuestManager.HeroEntity, 250)
  Experience.Modify(QuestManager.HeroEntity, EExperienceType.EXPERIENCE_SKILL, 1000, false)
  Stats.ModifyPurity(QuestManager.HeroEntity, -5)
  QuestTracker.SetAsCompleted(QuestManager.HeroEntity, self.QuestName, true, true)
end
)LUA";
    Quest::ReferenceCatalog reward_references;
    reward_references.quest_rewards.push_back(
        {"Gold", {}, 500.0});
    reward_references.quest_rewards.push_back(
        {"The Table of Life", "INV_ITEM_TROPHY_TABLE_LIFE_NAME",
         std::nullopt});
    const Quest::Graph reward_technical = Quest::BuildGraph(
        "QRewardCoverage.lua", reward_source, reward_references);
    const Quest::QuestEvent* renown_reward = find_event(
        reward_technical, Quest::QuestEventKind::Reward, "renown");
    const Quest::QuestEvent* skill_reward = find_event(
        reward_technical, Quest::QuestEventKind::Reward,
        "Skill experience");
    const Quest::QuestEvent* purity_reward = find_event(
        reward_technical, Quest::QuestEventKind::Reward, "purity");
    assert(renown_reward && renown_reward->amount == 250.0);
    assert(skill_reward && skill_reward->amount == 1000.0);
    assert(purity_reward && purity_reward->amount == -5.0);
    const Quest::Graph reward_story = Quest::BuildStoryGraph(
        "QRewardCoverage.lua", reward_source, reward_references);
    bool completion_has_gold = false;
    bool completion_has_item = false;
    bool completion_has_renown = false;
    bool completion_has_skill = false;
    bool completion_has_purity = false;
    for (const Quest::GraphNode& node : reward_story.nodes) {
        bool completion = node.badge == "Quest end" ||
                          node.title.find("Quest complete") !=
                              std::string::npos;
        for (const Quest::QuestEvent& event : node.events) {
            completion |= event.kind == Quest::QuestEventKind::QuestComplete;
        }
        if (!completion) continue;
        for (const Quest::QuestEvent& event : node.events) {
            completion_has_gold |= event.kind == Quest::QuestEventKind::Reward &&
                                   event.item == "Gold" &&
                                   event.amount == 500.0;
            completion_has_item |= event.kind == Quest::QuestEventKind::Reward &&
                                   event.item == "The Table of Life";
            completion_has_renown |=
                event.kind == Quest::QuestEventKind::Reward &&
                event.item == "Renown" && event.amount == 250.0;
            completion_has_skill |=
                event.kind == Quest::QuestEventKind::Reward &&
                event.item == "Skill experience" &&
                event.amount == 1000.0;
            completion_has_purity |=
                event.kind == Quest::QuestEventKind::Reward &&
                event.item == "Purity" && event.amount == -5.0;
        }
    }
    assert(completion_has_gold && completion_has_item);
    assert(completion_has_renown && completion_has_skill &&
           completion_has_purity);

    const char* movement_source = R"LUA(
QuestManager.NewQuestThread("QMovement")
QuestManager.NewEntityThread("QMovementRose")
function QMovement:Init()
  self:StartNewEntityThread("QC010_Rose", QMovementRose)
end
function QMovementRose:Update()
  if self.CurrentState == 0 then
    self:MoveAndRotateToMarkerNamed("QC010_RoseNearArfurMarker")
    self.CurrentState = 1
  elseif self.CurrentState == 1 then
    ScriptFunction.StartScriptControlledMovement(self.Entity, self:GetPositionOfEntity("QC010_RoseInCrowdMarker", "marker"), false, ENavigationSpeed.NAV_SPEED_SPRINT)
  end
end
)LUA";
    Quest::ReferenceCatalog movement_references;
    movement_references.world_entities[
        "qc010_roseneararfurmarker"].push_back(
            {"Albion/Bowerstone/BWSMarket", 183.72, 120.4, 50.31});
    movement_references.world_entities[
        "qc010_roseincrowdmarker"].push_back(
            {"Albion/Bowerstone/BWSMarket", 191.0, 126.0, 50.0});
    const Quest::Graph movement_graph = Quest::BuildGraph(
        "QMovement.lua", movement_source, movement_references);
    const Quest::QuestEvent* near_arfur = find_event(
        movement_graph, Quest::QuestEventKind::ActorMove,
        "QC010_RoseNearArfurMarker");
    const Quest::QuestEvent* in_crowd = find_event(
        movement_graph, Quest::QuestEventKind::ActorMove,
        "QC010_RoseInCrowdMarker");
    assert(near_arfur && in_crowd);
    assert(!near_arfur->actor.empty());
    assert(near_arfur->world.resolved && in_crowd->world.resolved);
    assert(near_arfur->world.level == "Albion/Bowerstone/BWSMarket");
    assert(near_arfur->world.x == 183.72);

    std::cout << "Narrative quest graph test passed: " << graph.nodes.size()
              << " nodes, " << graph.links.size() << " links\n";
    return 0;
}
