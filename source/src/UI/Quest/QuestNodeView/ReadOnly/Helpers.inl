const Quest::GraphNode* find_graph_node(const Quest::Graph& graph, int id) {
    for (const Quest::GraphNode& node : graph.nodes) {
        if (node.id == id) return &node;
    }
    return nullptr;
}

void draw_read_only_field(const char* label, const std::string& value) {
    ImGui::TextUnformatted(label);
    std::string copy = value;
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::BeginDisabled();
    ImGui::InputText((std::string("##") + label).c_str(), &copy,
                     ImGuiInputTextFlags_ReadOnly);
    ImGui::EndDisabled();
}

std::vector<std::string> read_only_prerequisites(
    const Quest::Graph& graph, const Quest::GraphNode& start) {
    std::vector<std::string> prerequisites;
    auto append = [&](const std::string& value) {
        if (value.empty() ||
            std::find(prerequisites.begin(), prerequisites.end(), value) !=
                prerequisites.end()) return;
        prerequisites.push_back(value);
    };

    for (const Quest::QuestEvent& event : start.events) {
        if (event.kind == Quest::QuestEventKind::Condition) {
            append(event.condition.empty() ? event.title : event.condition);
        }
    }
    for (const std::string& detail : start.details) {
        if (contains_case_insensitive(detail, "prerequisite") ||
            contains_case_insensitive(detail, "available after") ||
            contains_case_insensitive(detail, "unavailable after")) {
            append(detail);
        }
    }




    for (const Quest::GraphLink& link : graph.links) {
        if (link.to_node != start.id) continue;
        const Quest::GraphNode* source = find_graph_node(graph, link.from_node);
        if (!source) continue;
        bool is_condition = source->kind == Quest::NodeKind::State;
        for (const Quest::QuestEvent& event : source->events) {
            is_condition |= event.kind == Quest::QuestEventKind::Condition;
        }
        if (is_condition) append(source->title);
    }
    return prerequisites;
}

bool read_only_completion_node(const Quest::GraphNode& node) {
    for (const Quest::QuestEvent& event : node.events) {
        if (event.kind == Quest::QuestEventKind::QuestComplete) return true;
        if (event.kind == Quest::QuestEventKind::QuestFail) return false;
    }
    if (contains_case_insensitive(node.badge, "failed") ||
        contains_case_insensitive(node.title, "failed")) return false;
    if (contains_case_insensitive(node.badge, "ending choice")) return false;
    if (contains_case_insensitive(node.badge, "ending") ||
        contains_case_insensitive(node.badge, "quest end")) return true;
    return std::any_of(node.details.begin(), node.details.end(),
                       [](const std::string& detail) {
                           return contains_case_insensitive(
                               detail, "quest complete");
                       });
}

bool reward_event(const Quest::QuestEvent& event) {
    return event.kind == Quest::QuestEventKind::Reward ||
           event.kind == Quest::QuestEventKind::Morality ||
           event.kind == Quest::QuestEventKind::InventoryAdd;
}

std::string read_only_reward_text(const Quest::QuestEvent& event) {
    std::string label;
    if (event.kind == Quest::QuestEventKind::Morality) {
        label = "Morality";
    } else if (event.kind == Quest::QuestEventKind::InventoryAdd) {
        label = event.item.empty() ? "Item" : event.item;
        std::replace(label.begin(), label.end(), '_', ' ');
        return "Item: " + label;
    } else {
        label = event.item.empty() ? event.title : event.item;
        if (label.empty()) label = "Reward";
    }
    if (!event.amount) return label;
    const double value = *event.amount;
    const long long whole = static_cast<long long>(value);
    std::string amount = value == static_cast<double>(whole)
        ? std::to_string(whole) : std::to_string(value);
    if ((label == "Morality" || label == "Purity") && value > 0.0) {
        amount.insert(amount.begin(), '+');
    }
    return label + ": " + amount;
}

std::vector<std::string> read_only_completion_rewards(
    const Quest::Graph& graph, const Quest::GraphNode& completion) {
    std::vector<std::string> rewards;
    std::unordered_set<std::string> seen;
    auto append = [&](const std::string& value) {
        if (!value.empty() && seen.insert(value).second) {
            rewards.push_back(value);
        }
    };
    for (const Quest::QuestEvent& event : completion.events) {
        if (reward_event(event)) append(read_only_reward_text(event));
    }




    for (const Quest::QuestEvent& completed : completion.events) {
        if (completed.kind != Quest::QuestEventKind::QuestComplete ||
            completed.source_method.empty()) continue;
        for (const Quest::GraphNode& candidate_node : graph.nodes) {
            for (const Quest::QuestEvent& candidate : candidate_node.events) {
                if (!reward_event(candidate) ||
                    candidate.source_method != completed.source_method ||
                    candidate.source_class != completed.source_class) {
                    continue;
                }
                const std::size_t earlier =
                    candidate.source_line < completed.source_line
                        ? completed.source_line - candidate.source_line
                        : candidate.source_line - completed.source_line;
                if (earlier <= 100) append(read_only_reward_text(candidate));
            }
        }
    }



    for (const std::string& detail : completion.details) {
        if (contains_case_insensitive(detail, "morality:") ||
            contains_case_insensitive(detail, "purity:") ||
            contains_case_insensitive(detail, "reward:")) {
            append(detail);
        }
    }
    return rewards;
}
