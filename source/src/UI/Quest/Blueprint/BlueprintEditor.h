#pragma once

#include <string>
#include <utility>
#include <vector>

#include "Quest/Blueprint/BlueprintTypes.h"

namespace Quest {
namespace Bp {
struct BlueprintQuest;
}
}




namespace BlueprintUI {

bool CreateQuest(const std::string& quest_id, std::string& error);
bool OpenQuest(const std::string& quest_id);
bool HasQuest(const std::string& quest_id);
bool DeleteQuest(const std::string& quest_id, std::string& error);
std::vector<std::string> QuestIds();

bool IsActive();
std::string ActiveQuestId();
Quest::Bp::BlueprintQuest* ActiveQuest();
void CloseActive();   


std::string ActiveLua();
std::string ActiveQuestLua();
std::string ActiveEligibilityLua();
std::vector<std::pair<std::string, std::string>> ActiveTextEntries();
bool ValidateActive(std::string& error);
std::vector<Quest::Bp::Diagnostic> ActiveDiagnostics();




void ArmPinPick(int pin_id);
int  PendingPickPin();
bool BindPendingPin(const std::string& level_path,
                    const std::string& level_id,
                    const std::string& entity_name, uint32_t entity_hash,
                    float x, float y, float z,
                    const std::vector<uint32_t>& model_hashes,
                    bool authored_instance, std::string& error);

void Draw();       
void Shutdown();

}
