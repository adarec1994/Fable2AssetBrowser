#pragma once

#include <string>

struct FlatAssetEntry;

namespace ContentTabs {

enum class Kind {
    None,
    Model,
    Item,
    Entity,
    Level,
    Lua,
    Quest,
    CustomQuest,
};


void CaptureCurrentModel();



void OpenItem(int item_index, const std::string& title);
void OpenEntity(int entity_index, const std::string& title);
void OpenLevel(const FlatAssetEntry& entry, const std::string& title);
void OpenLua(const std::string& key, const std::string& title,
             bool is_quest);
void OpenCustomQuest(const std::string& quest_id,
                     const std::string& title);
void CloseCustomQuest(const std::string& quest_id);
void CloseLevelByPath(const std::string& full_path);



void FixLooseEntryIndices(const std::string& loose_dir);



void CompleteLua(const std::string& key, const std::string& content);

bool HasTabs();
Kind ActiveKind();
bool ActiveHasModel();
const FlatAssetEntry* ActiveLevelEntry();
void DrawTabBar();
void CloseActive();
void Clear();

}
