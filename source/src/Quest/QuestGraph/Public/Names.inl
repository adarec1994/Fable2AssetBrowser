const char* NodeKindName(NodeKind kind) {
    switch (kind) {
        case NodeKind::Quest: return "Quest start";
        case NodeKind::Thread: return "NPC / entity behaviour";
        case NodeKind::Function: return "Supporting quest logic";
        case NodeKind::State: return "Quest step";
        case NodeKind::Action: return "Quest action";
    }
    return "Quest node";
}

const char* QuestEventKindName(QuestEventKind kind) {
    switch (kind) {
        case QuestEventKind::Unknown: return "Unknown";
        case QuestEventKind::Dialogue: return "Dialogue";
        case QuestEventKind::Cutscene: return "Cutscene";
        case QuestEventKind::Interaction: return "Interaction";
        case QuestEventKind::Condition: return "Condition";
        case QuestEventKind::ObjectiveSet: return "Objective set";
        case QuestEventKind::ObjectiveRemove: return "Objective removed";
        case QuestEventKind::ObjectiveComplete: return "Objective complete";
        case QuestEventKind::ObjectiveTarget: return "Objective target";
        case QuestEventKind::InventoryAdd: return "Inventory add";
        case QuestEventKind::InventoryRemove: return "Inventory remove";
        case QuestEventKind::InventoryClear: return "Inventory clear";
        case QuestEventKind::DigSpotEnable: return "Dig spot enabled";
        case QuestEventKind::DigSpotComplete: return "Dig completed";
        case QuestEventKind::TimerStart: return "Timer started";
        case QuestEventKind::TimerWait: return "Timer wait";
        case QuestEventKind::TimerStop: return "Timer stopped";
        case QuestEventKind::ActorMove: return "Actor movement";
        case QuestEventKind::ActorAnimation: return "Actor animation";
        case QuestEventKind::ActorState: return "Actor state";
        case QuestEventKind::Spawn: return "Spawn";
        case QuestEventKind::Despawn: return "Despawn";
        case QuestEventKind::DoorState: return "Door state";
        case QuestEventKind::LayerState: return "Layer state";
        case QuestEventKind::Camera: return "Camera";
        case QuestEventKind::Travel: return "Travel";
        case QuestEventKind::Reward: return "Reward";
        case QuestEventKind::Morality: return "Morality";
        case QuestEventKind::QuestStart: return "Quest start";
        case QuestEventKind::QuestComplete: return "Quest complete";
        case QuestEventKind::QuestFail: return "Quest failed";
        case QuestEventKind::ScriptCall: return "Script call";
    }
    return "Unknown";
}
