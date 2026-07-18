bool parse_dialogue(const std::string& line, std::string& tag,
                    std::string& speaker, std::string& text) {
    if (!starts_ci(line, "Dialogue [")) return false;
    const std::size_t close = line.find(']');
    if (close == std::string::npos) return false;
    tag = line.substr(std::string("Dialogue [").size(),
                      close - std::string("Dialogue [").size());
    std::string rest = trim(line.substr(close + 1));
    if (!rest.empty() && rest.front() == ':') {
        text = trim(rest.substr(1));
        return !text.empty();
    }
    const std::size_t colon = rest.find(':');
    if (colon == std::string::npos) {
        text = rest;
        return !text.empty();
    }
    speaker = clean_names(rest.substr(0, colon));
    text = trim(rest.substr(colon + 1));
    return !text.empty();
}

bool parse_direct_dialogue(const std::string& line, Beat& beat) {
    const std::string marker = " says: \"";
    const std::size_t says = line.find(marker);
    if (says == std::string::npos) return false;
    std::string text = line.substr(says + marker.size());
    if (!text.empty() && text.back() == '"') text.pop_back();
    const std::string speaker = clean_names(line.substr(0, says));
    beat.kind = BeatKind::Dialogue;
    beat.title = speaker + ": " + text;
    return true;
}

bool classify_action(const std::string& line, Beat& beat) {
    if (starts_ci(line, "Camera event: ")) {
        beat.kind = BeatKind::Camera;
        beat.title = clean_names(line.substr(
            std::string("Camera event: ").size()));
        beat.subtitle = "Camera direction";
        return !beat.title.empty();
    }
    if (starts_ci(line, "Actor action: ")) {
        beat.kind = BeatKind::ActorAction;
        beat.title = clean_names(line.substr(
            std::string("Actor action: ").size()));
        beat.subtitle = "Actor staging";
        return !beat.title.empty();
    }
    if (starts_ci(line, "Set objective: ")) {
        const std::string objective = trim(line.substr(
            std::string("Set objective: ").size()));
        if (objective.empty()) return false;
        beat.kind = BeatKind::Objective;
        beat.title = "Objective: " + clean_names(objective);
        return true;
    }
    if (starts_ci(line, "Point the objective marker at ") ||
        starts_ci(line, "Set the objective destination to ")) {
        beat.kind = BeatKind::Objective;
        beat.title = clean_names(line);
        return true;
    }
    if (starts_ci(line, "Mark the current objective complete") ||
        starts_ci(line, "Complete the objective")) {
        beat.kind = BeatKind::Objective;
        beat.title = "Objective complete";
        return true;
    }
    if ((starts_ci(line, "Wait until ") ||
         starts_ci(line, "Wait for trigger ")) &&
        meaningful_condition(line)) {
        beat.kind = BeatKind::Decision;
        beat.title = decision_title(line);
        return true;
    }
    if (starts_ci(line, "Travel to ")) {
        beat.kind = BeatKind::Task;
        std::string destination = line.substr(std::string("Travel to ").size());
        const std::size_t first = destination.find('/');
        const std::size_t second = first == std::string::npos
            ? std::string::npos : destination.find('/', first + 1);
        if (second != std::string::npos) destination.resize(second);
        beat.title = "Travel to " + clean_names(destination);
        beat.subtitle = "Location";
        return true;
    }
    if (starts_ci(line, "Guide Hero to marker ")) {
        beat.kind = BeatKind::Task;
        beat.title = "Go to " + clean_names(line.substr(
            std::string("Guide Hero to marker ").size()));
        beat.subtitle = "Location";
        return true;
    }
    if (starts_ci(line, "Move ") || starts_ci(line, "Teleport ") ||
        starts_ci(line, "Guide ") || contains_ci(line, " plays animation ") ||
        contains_ci(line, " follows ") || contains_ci(line, " attacks ") ||
        contains_ci(line, " turns to face ") ||
        contains_ci(line, " starts looking at ") ||
        contains_ci(line, " stops looking at ")) {
        beat.kind = BeatKind::ActorAction;
        beat.title = clean_names(line);
        beat.subtitle = "Actor staging";
        return true;
    }
    if (starts_ci(line, "Spawn ")) {
        beat.kind = BeatKind::Task;
        beat.title = clean_names(line);
        beat.subtitle = "Target";
        return true;
    }
    if (starts_ci(line, "Kill ")) {
        beat.kind = BeatKind::Task;
        beat.title = "Defeat " + clean_names(line.substr(5));
        beat.subtitle = "Combat";
        return true;
    }
    if (contains_ci(line, " kills ")) {
        beat.kind = BeatKind::Task;
        beat.title = clean_names(line);
        beat.subtitle = "Story event";
        return true;
    }
    if (starts_ci(line, "Reward: ") ||
        starts_ci(line, "Give the player ")) {
        if (contains_ci(line, "scripted reward")) return false;
        beat.kind = BeatKind::Reward;
        beat.title = starts_ci(line, "Reward: ")
            ? clean_names(line)
            : "Reward: " + clean_names(line.substr(
                  std::string("Give the player ").size()));
        return true;
    }
    if (starts_ci(line, "Complete the quest")) {
        beat.kind = BeatKind::Ending;
        beat.title = "Quest complete";
        return true;
    }
    if (starts_ci(line, "Fail the quest")) {
        beat.kind = BeatKind::Ending;
        beat.title = "Quest failed";
        return true;
    }
    if (starts_ci(line, "Play cutscene ")) {
        std::string scene = line.substr(std::string("Play cutscene ").size());
        const std::size_t spoken = lower_ascii(scene).find(" (", 1);
        if (spoken != std::string::npos) scene.resize(spoken);
        beat.kind = BeatKind::Task;
        beat.title = "Cutscene: " + clean_names(scene);
        beat.subtitle = "Story event";
        return true;
    }
    if (starts_ci(line, "Play movie ")) {
        beat.kind = BeatKind::Task;
        beat.title = "Cinematic: " + clean_names(line.substr(
            std::string("Play movie ").size()));
        beat.subtitle = "Story event";
        return true;
    }
    return false;
}

bool classify_event(const QuestEvent& event, const std::string& line,
                    Beat& beat) {
    beat.title = clean_names(event.title.empty() ? line : event.title);
    switch (event.kind) {
        case QuestEventKind::Interaction:
            beat.kind = BeatKind::Interaction;
            beat.subtitle = "Player interaction";
            return true;
        case QuestEventKind::InventoryAdd:
        case QuestEventKind::InventoryRemove:
        case QuestEventKind::InventoryClear:
            beat.kind = BeatKind::Inventory;
            beat.subtitle = "Quest inventory change";
            return true;
        case QuestEventKind::DigSpotEnable:
            beat.kind = BeatKind::WorldState;
            beat.subtitle = "Dig spot";
            return true;
        case QuestEventKind::DigSpotComplete:
            beat.kind = BeatKind::Interaction;
            beat.subtitle = "Dig interaction";
            return true;
        case QuestEventKind::TimerStart:
        case QuestEventKind::TimerWait:
        case QuestEventKind::TimerStop:
            beat.kind = BeatKind::Timer;
            beat.subtitle = "Timed quest event";
            return true;
        case QuestEventKind::DoorState:
        case QuestEventKind::LayerState:
        case QuestEventKind::Despawn:
        case QuestEventKind::Morality:
            beat.kind = BeatKind::WorldState;
            beat.subtitle = "World state change";
            return true;
        default:
            return false;
    }
}
