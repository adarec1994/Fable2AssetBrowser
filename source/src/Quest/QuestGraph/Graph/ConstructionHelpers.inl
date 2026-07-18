std::vector<StateBranch> find_states(const FunctionBlock& function,
                                     const std::vector<std::string>& lines) {
    static const std::regex state_re(
        R"(^\s*(?:if|elseif)\s+self\.CurrentState\s*==\s*(-?[0-9]+)\s+then)");
    std::vector<StateBranch> branches;
    for (std::size_t i = function.begin + 1; i < function.end; ++i) {
        std::smatch match;
        if (!std::regex_search(lines[i], match, state_re)) continue;
        if (!branches.empty()) branches.back().end = i;
        StateBranch branch;
        branch.value = std::stoi(match[1].str());
        branch.begin = i;
        branch.end = function.end;
        branches.push_back(branch);
    }
    return branches;
}

GraphNode& add_node(Graph& graph, NodeKind kind, std::string title,
                    std::string subtitle, float x, float y) {
    GraphNode node;
    node.id = static_cast<int>(graph.nodes.size()) + 1;
    node.kind = kind;
    node.title = std::move(title);
    node.subtitle = std::move(subtitle);
    node.x = x;
    node.y = y;
    graph.nodes.push_back(std::move(node));
    return graph.nodes.back();
}

GraphNode* node_by_id(Graph& graph, int id) {
    for (GraphNode& node : graph.nodes) {
        if (node.id == id) return &node;
    }
    return nullptr;
}

void add_link(Graph& graph,
              std::set<std::tuple<int, int, std::string>>& unique_links,
              int from, int to, std::string label, bool inferred = false) {
    if (from <= 0 || to <= 0 || from == to) return;
    auto key = std::make_tuple(from, to, label);
    if (!unique_links.insert(key).second) return;
    GraphLink link;
    link.id = static_cast<int>(graph.links.size()) + 1;
    link.from_node = from;
    link.to_node = to;
    link.label = std::move(label);
    link.inferred = inferred;
    graph.links.push_back(std::move(link));
}

std::string join(const std::vector<std::string>& values,
                 const std::string& separator, std::size_t limit = 8) {
    std::string result;
    for (std::size_t i = 0; i < values.size() && i < limit; ++i) {
        if (!result.empty()) result += separator;
        result += values[i];
    }
    if (values.size() > limit) result += separator + "+" +
        std::to_string(values.size() - limit) + " more";
    return result;
}

void append_narrative_details(GraphNode& node, const Narrative& narrative,
                              std::size_t action_limit = 12,
                              const std::string& indent = {}) {
    for (const NarrativeAction& action : narrative.actions) {
        node.events.push_back(action.event);
    }
    node.events.insert(node.events.end(), narrative.supplemental_events.begin(),
                       narrative.supplemental_events.end());
    for (std::size_t i = 0;
         i < narrative.actions.size() && i < action_limit; ++i) {
        const NarrativeAction& action = narrative.actions[i];
        node.details.push_back(indent + std::to_string(i + 1) + ". " +
                               action.summary);
        for (const std::string& extra : action.extra) {
            node.details.push_back(indent + "   " + extra);
        }
    }
    if (narrative.actions.size() > action_limit) {
        node.details.push_back(indent + "+ " +
            std::to_string(narrative.actions.size() - action_limit) +
            " more actions; see Lua Script for the complete source");
    }
    if (!narrative.entities.empty()) {
        node.details.push_back(indent + "NPCs/entities: " +
                               join(narrative.entities, ", "));
    }
    if (!narrative.locations.empty()) {
        node.details.push_back(indent + "Areas/levels: " +
                               join(narrative.locations, ", "));
    }
    if (!narrative.markers.empty()) {
        node.details.push_back(indent + "Markers: " +
                               join(narrative.markers, ", "));
    }
    if (!narrative.coordinates.empty()) {
        node.details.push_back(indent + "Coordinates: " +
                               join(narrative.coordinates, " | "));
    }
}

void count_references(Graph& graph, const Narrative& narrative) {
    for (const NarrativeAction& action : narrative.actions) {
        graph.dialogue_lines += action.dialogue_lines;
        graph.audio_matches += action.audio_matches;
    }
}

std::string narrative_headline(const Narrative& narrative,
                               const std::string& fallback) {
    for (const NarrativeAction& action : narrative.actions) {
        if (!action.summary.empty()) return shorten(action.summary, 76);
    }
    if (!narrative.markers.empty()) return "Reach " + humanize(narrative.markers.front());
    if (!narrative.locations.empty()) return "Continue in " + narrative.locations.front();
    return fallback;
}

std::pair<float, float> main_flow_position(std::size_t index) {
    constexpr std::size_t columns = 5;
    constexpr float start_x = 360.0f;
    constexpr float dx = 390.0f;
    constexpr float dy = 390.0f;
    const std::size_t row = index / columns;
    std::size_t column = index % columns;
    if ((row & 1u) != 0) column = columns - 1 - column;
    return {start_x + float(column) * dx, float(row) * dy};
}

const FunctionBlock* find_function(const std::vector<FunctionBlock>& functions,
                                   const std::string& class_name,
                                   const std::string& method) {
    for (const FunctionBlock& function : functions) {
        if (function.class_name == class_name && function.method == method) {
            return &function;
        }
    }
    return nullptr;
}

std::string function_key(const std::string& class_name,
                         const std::string& method) {
    return class_name + "::" + method;
}
