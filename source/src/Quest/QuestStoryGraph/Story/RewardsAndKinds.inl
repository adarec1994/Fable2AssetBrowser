bool is_completion_node(const GraphNode& node) {
    for (const QuestEvent& event : node.events) {
        if (event.kind == QuestEventKind::QuestComplete) return true;
        if (event.kind == QuestEventKind::QuestFail) return false;
    }
    if (contains_ci(node.badge, "failed") ||
        contains_ci(node.title, "failed")) {
        return false;
    }
    if (contains_ci(node.badge, "ending choice")) return false;
    if (contains_ci(node.badge, "ending") ||
        contains_ci(node.badge, "quest end")) {
        return true;
    }
    for (const std::string& detail : node.details) {
        if (contains_ci(detail, "quest complete")) return true;
    }
    return false;
}

bool completion_reward_event(const QuestEvent& event) {
    return event.kind == QuestEventKind::Reward ||
           event.kind == QuestEventKind::Morality ||
           event.kind == QuestEventKind::InventoryAdd;
}

void attach_completion_rewards(Graph& graph, const Graph& technical,
                               const ReferenceCatalog& references) {
    std::vector<const QuestEvent*> technical_completions;
    std::vector<const QuestEvent*> scripted_rewards;
    for (const GraphNode& source : technical.nodes) {
        for (const QuestEvent& event : source.events) {
            if (event.kind == QuestEventKind::QuestComplete) {
                technical_completions.push_back(&event);
            } else if (completion_reward_event(event)) {
                scripted_rewards.push_back(&event);
            }
        }
    }
    const std::size_t completion_nodes = std::count_if(
        graph.nodes.begin(), graph.nodes.end(), is_completion_node);
    for (GraphNode& node : graph.nodes) {
        if (!is_completion_node(node)) continue;
        for (const QuestRewardReference& reward : references.quest_rewards) {
            QuestEvent event;
            event.kind = QuestEventKind::Reward;
            event.title = reward.label;
            event.item = reward.label;
            event.amount = reward.amount;
            event.properties.push_back("Quest record reward");
            node.events.push_back(std::move(event));
        }

        std::vector<const QuestEvent*> sources;
        for (const QuestEvent& event : node.events) {
            if (event.kind == QuestEventKind::QuestComplete) {
                sources.push_back(&event);
            }
        }
        if (sources.empty() && completion_nodes == 1) {
            sources = technical_completions;
        }
        for (const QuestEvent* source : sources) {
            for (const QuestEvent* reward : scripted_rewards) {
                if (source->source_class != reward->source_class ||
                    source->source_method != reward->source_method ||
                    source->source_method.empty()) {
                    continue;
                }
                const std::size_t distance =
                    source->source_line < reward->source_line
                        ? reward->source_line - source->source_line
                        : source->source_line - reward->source_line;
                if (distance <= 100) node.events.push_back(*reward);
            }
        }
    }
}

NodeKind node_kind(BeatKind kind) {
    switch (kind) {
        case BeatKind::Dialogue: return NodeKind::Thread;
        case BeatKind::ActorAction: return NodeKind::Action;
        case BeatKind::Camera: return NodeKind::Action;
        case BeatKind::Objective: return NodeKind::Function;
        case BeatKind::Decision: return NodeKind::State;
        case BeatKind::Interaction:
        case BeatKind::Inventory:
        case BeatKind::Timer:
        case BeatKind::WorldState:
        case BeatKind::Task:
        case BeatKind::Reward:
        case BeatKind::Ending: return NodeKind::Action;
    }
    return NodeKind::Action;
}

const char* badge(BeatKind kind) {
    switch (kind) {
        case BeatKind::Dialogue: return "Dialogue";
        case BeatKind::ActorAction: return "Actor action";
        case BeatKind::Camera: return "Camera event";
        case BeatKind::Objective: return "Objective";
        case BeatKind::Decision: return "Condition";
        case BeatKind::Interaction: return "Interaction";
        case BeatKind::Inventory: return "Quest item";
        case BeatKind::Timer: return "Timer";
        case BeatKind::WorldState: return "World event";
        case BeatKind::Task: return "Quest step";
        case BeatKind::Reward: return "Reward";
        case BeatKind::Ending: return "Quest end";
    }
    return "Quest step";
}
