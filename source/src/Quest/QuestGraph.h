#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "QuestReferences.h"

namespace Quest {

enum class NodeKind {
    Quest,
    Thread,
    Function,
    State,
    Action,
};




enum class QuestEventKind {
    Unknown,
    Dialogue,
    Cutscene,
    Interaction,
    Condition,
    ObjectiveSet,
    ObjectiveRemove,
    ObjectiveComplete,
    ObjectiveTarget,
    InventoryAdd,
    InventoryRemove,
    InventoryClear,
    DigSpotEnable,
    DigSpotComplete,
    TimerStart,
    TimerWait,
    TimerStop,
    ActorMove,
    ActorAnimation,
    ActorState,
    Spawn,
    Despawn,
    DoorState,
    LayerState,
    Camera,
    Travel,
    Reward,
    Morality,
    QuestStart,
    QuestComplete,
    QuestFail,
    ScriptCall,
};

struct QuestWorldPosition {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    bool resolved = false;
    std::string level;
    std::string marker;
};

struct QuestEvent {
    QuestEventKind kind = QuestEventKind::Unknown;
    std::string title;
    std::string actor;
    std::string target;
    std::string item;
    std::string objective;
    std::string condition;
    std::string dialogue_id;
    std::string cutscene_id;
    std::string animation_id;
    std::optional<double> amount;
    std::optional<double> duration_seconds;
    QuestWorldPosition world;
    std::vector<std::string> properties;



    std::string source_class;
    std::string source_method;
    int source_state = -1;
    std::size_t source_line = 0;
    std::string source_statement;
};

struct WorldEntityPlacement {
    std::string level;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct QuestRewardReference {
    std::string label;
    std::string item_text_tag;
    std::optional<double> amount;
};

struct GraphNode {
    int id = 0;
    NodeKind kind = NodeKind::Function;
    std::string badge;
    std::string title;
    std::string subtitle;
    std::vector<std::string> details;
    std::vector<std::string> metadata;
    std::vector<QuestEvent> events;
    float x = 0.0f;
    float y = 0.0f;
};

struct GraphLink {
    int id = 0;
    int from_node = 0;
    int to_node = 0;
    std::string label;
    bool inferred = false;
};

struct ReferenceCatalog {
    std::unordered_map<std::string, std::string> localized_text;
    std::unordered_map<std::string, CutsceneReference> cutscenes;
    std::unordered_map<std::string, std::vector<std::string>> audio_by_dialogue;
    std::vector<std::string> audio_assets;
    std::vector<std::string> level_assets;



    std::unordered_map<std::string, std::vector<WorldEntityPlacement>>
        world_entities;


    std::vector<QuestRewardReference> quest_rewards;
};

struct Graph {
    std::string title;
    std::vector<GraphNode> nodes;
    std::vector<GraphLink> links;
    std::size_t quest_threads = 0;
    std::size_t entity_threads = 0;
    std::size_t functions = 0;
    std::size_t states = 0;
    std::size_t flow_steps = 0;
    std::size_t dialogue_lines = 0;
    std::size_t audio_matches = 0;
};

Graph BuildGraph(const std::string& title, const std::string& decompiled_lua);
Graph BuildGraph(const std::string& title, const std::string& decompiled_lua,
                 const ReferenceCatalog& references);
std::vector<std::string> FindLuaStringLiterals(const std::string& text);
const char* NodeKindName(NodeKind kind);
const char* QuestEventKindName(QuestEventKind kind);

}
