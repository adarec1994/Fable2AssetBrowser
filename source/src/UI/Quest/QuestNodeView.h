#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace QuestUI {



bool CreateNewBlueprintQuest(const std::string& quest_id,
                             std::string& error);
bool OpenAuthoredQuest(const std::string& quest_id);
bool DeleteAuthoredQuest(const std::string& quest_id, std::string& error);
std::vector<std::string> AuthoredQuestIds();
bool IsAuthoredQuestActive();
std::string ActiveAuthoredQuestId();
std::string ActiveAuthoredLua();
std::string ActiveAuthoredQuestLua();
std::string ActiveAuthoredEligibilityLua();
std::vector<std::pair<std::string, std::string>>
ActiveAuthoredTextEntries();
bool ValidateActiveAuthoredQuest(std::string& error);

enum class LevelReferenceTarget {
    QuestGiver,
    RequiredItemSource,
};

struct LevelReferenceCandidate {
    bool is_npc = false;
    bool is_container = false;
    std::string level_path;
    std::string level_id;
    std::string entity_name;
    uint32_t entity_hash = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    std::vector<uint32_t> model_hashes;
    bool authored_instance = false;
};

struct NpcCreationRequest {
    std::string instance_name;
    std::string creature_name;
    std::string display_name;
    uint32_t creature_entity = 0;
    std::vector<uint32_t> model_hashes;
};

LevelReferenceTarget PendingLevelReferenceTarget();
std::string PendingLevelReferenceLabel();
uint32_t PendingLevelReferenceItemHash();
bool BindActiveLevelReference(const LevelReferenceCandidate& candidate,
                              std::string& error);
bool GetPendingNpcCreation(NpcCreationRequest& request);
bool BindCreatedNpcInstance(const LevelReferenceCandidate& candidate,
                            std::string& error);
void CancelPendingNpcCreation();



void RequestOpenPrerequisiteMenu();
void AddPrerequisiteExamplesForCapture();
void SetPrerequisiteInspectorCaptureScrollBottom(bool enabled);
void SetPrerequisiteInspectorCaptureScrollFraction(float fraction);


void RefreshReferenceCatalog();



void SetQuestSource(const std::string& title,
                    const std::string& decompiled_lua);


void SetInitialViewAll(bool enabled);


void SetInitialFocusNodeCount(std::size_t count);



void SetInitialFocusNodeRange(std::size_t start, std::size_t count);



void RequestSelectCompletionNode(std::size_t index);

void Clear();
void DrawNodeView();
void Shutdown();

}
