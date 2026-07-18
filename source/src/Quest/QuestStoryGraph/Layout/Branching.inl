constexpr std::size_t kVisibleDetailLimit = 12;

float estimated_node_height(const GraphNode& node) {
    float lines = 3.5f;
    std::size_t counted = 0;
    for (const std::string& detail : node.details) {
        if (counted == kVisibleDetailLimit) {
            lines += 1.0f;
            break;
        }
        lines += float(std::max<std::size_t>(1, (detail.size() + 54) / 55));
        ++counted;
    }
    return lines * 22.0f + 30.0f;
}

void add_link(Graph& graph,
              std::set<std::tuple<int, int, std::string>>& unique,
              int from, int to, std::string label) {
    if (from <= 0 || to <= 0) return;
    const auto key = std::make_tuple(from, to, label);
    if (!unique.insert(key).second) return;
    GraphLink link;
    link.id = int(graph.links.size()) + 1;
    link.from_node = from;
    link.to_node = to;
    link.label = std::move(label);
    graph.links.push_back(std::move(link));
}

const GraphNode* find_node(const Graph& graph, int id) {
    for (const GraphNode& node : graph.nodes) {
        if (node.id == id) return &node;
    }
    return nullptr;
}

bool is_decision(const Graph& graph, int id) {
    const GraphNode* node = find_node(graph, id);
    return node && node->badge == "Condition";
}

void layout_branching_story(Graph& graph, int root_story,
                            std::vector<int>& layout_order) {
    std::unordered_map<int, GraphNode*> nodes;
    std::unordered_map<int, std::vector<int>> forward;
    std::unordered_map<int, int> indegree;
    for (GraphNode& node : graph.nodes) {
        nodes[node.id] = &node;
        indegree[node.id] = 0;
    }
    for (const GraphLink& link : graph.links) {
        if (!nodes.count(link.from_node) || !nodes.count(link.to_node) ||
            link.from_node == link.to_node ||
            lower_ascii(link.label) == "no") {
            continue;
        }
        std::vector<int>& targets = forward[link.from_node];
        if (std::find(targets.begin(), targets.end(), link.to_node) ==
            targets.end()) {
            targets.push_back(link.to_node);
            ++indegree[link.to_node];
        }
    }
    for (auto& entry : forward) {
        std::sort(entry.second.begin(), entry.second.end());
        if (entry.second.size() < 2) continue;
        
        
        const GraphNode* source_node = find_node(graph, entry.first);
        if (source_node && source_node->kind == NodeKind::Quest) continue;
        const bool decision = is_decision(graph, entry.first);
        for (std::size_t i = 0; i < entry.second.size(); ++i) {
            for (GraphLink& link : graph.links) {
                if (link.from_node != entry.first ||
                    link.to_node != entry.second[i] || !link.label.empty()) {
                    continue;
                }
                if (decision && entry.second.size() == 2) {
                    link.label = i == 0 ? "Yes" : "No";
                } else {
                    link.label = "Path " + std::to_string(i + 1);
                }
            }
        }
    }

    std::unordered_map<int, int> remaining = indegree;
    std::unordered_map<int, int> depth;
    std::set<int> ready;
    for (const auto& entry : nodes) {
        depth[entry.first] = 0;
        if (remaining[entry.first] == 0) ready.insert(entry.first);
    }
    std::vector<int> topological;
    while (!ready.empty()) {
        const int current = *ready.begin();
        ready.erase(ready.begin());
        topological.push_back(current);
        for (int target : forward[current]) {
            depth[target] = std::max(depth[target], depth[current] + 1);
            if (--remaining[target] == 0) ready.insert(target);
        }
    }
    if (topological.size() != nodes.size()) {
        std::vector<int> unresolved;
        for (const auto& entry : nodes) {
            if (std::find(topological.begin(), topological.end(),
                          entry.first) == topological.end()) {
                unresolved.push_back(entry.first);
            }
        }
        std::sort(unresolved.begin(), unresolved.end());
        int fallback_depth = 0;
        for (const auto& entry : depth) {
            fallback_depth = std::max(fallback_depth, entry.second);
        }
        for (int id : unresolved) {
            depth[id] = ++fallback_depth;
            topological.push_back(id);
        }
    }
    std::unordered_map<int, std::size_t> topo_index;
    for (std::size_t i = 0; i < topological.size(); ++i) {
        topo_index[topological[i]] = i;
    }

    
    
    
    std::unordered_map<int, std::vector<int>> neighbours;
    for (const GraphLink& link : graph.links) {
        if (!nodes.count(link.from_node) || !nodes.count(link.to_node) ||
            link.from_node == link.to_node) {
            continue;
        }
        neighbours[link.from_node].push_back(link.to_node);
        neighbours[link.to_node].push_back(link.from_node);
    }
    std::unordered_map<int, int> component_of;
    std::vector<std::vector<int>> components;
    for (int id : topological) {
        if (component_of.count(id)) continue;
        std::vector<int> component;
        std::deque<int> queue{id};
        component_of[id] = int(components.size());
        while (!queue.empty()) {
            const int current = queue.front();
            queue.pop_front();
            component.push_back(current);
            for (int next : neighbours[current]) {
                if (component_of.count(next)) continue;
                component_of[next] = int(components.size());
                queue.push_back(next);
            }
        }
        components.push_back(std::move(component));
    }
    std::vector<std::size_t> component_order(components.size());
    for (std::size_t i = 0; i < components.size(); ++i) component_order[i] = i;
    if (root_story > 0 && component_of.count(root_story)) {
        const std::size_t root_component =
            std::size_t(component_of[root_story]);
        std::stable_partition(component_order.begin(), component_order.end(),
                              [&](std::size_t component) {
                                  return component == root_component;
                              });
    }

    
    
    
    constexpr float kColumnSpacing = 700.0f;
    constexpr float kRowGap = 70.0f;
    constexpr float kComponentGap = 260.0f;
    layout_order.clear();
    float component_base_y = 0.0f;
    for (std::size_t component_index : component_order) {
        const std::vector<int>& component = components[component_index];
        int min_depth = std::numeric_limits<int>::max();
        int max_depth = 0;
        for (int id : component) {
            min_depth = std::min(min_depth, depth[id]);
            max_depth = std::max(max_depth, depth[id]);
        }
        std::vector<std::vector<int>> columns(
            std::size_t(max_depth - min_depth) + 1);
        for (int id : component) {
            columns[std::size_t(depth[id] - min_depth)].push_back(id);
        }
        for (std::vector<int>& column : columns) {
            std::sort(column.begin(), column.end(), [&](int a, int b) {
                return topo_index[a] < topo_index[b];
            });
        }

        std::unordered_map<int, float> order_position;
        auto refresh_positions = [&]() {
            for (const std::vector<int>& column : columns) {
                for (std::size_t i = 0; i < column.size(); ++i) {
                    order_position[column[i]] = float(i);
                }
            }
        };
        refresh_positions();
        auto barycenter_sort = [&](std::vector<int>& column,
                                   const std::unordered_map<
                                       int, std::vector<int>>& relatives) {
            std::stable_sort(column.begin(), column.end(),
                             [&](int a, int b) {
                                 auto mean = [&](int id) {
                                     const auto found = relatives.find(id);
                                     if (found == relatives.end() ||
                                         found->second.empty()) {
                                         return order_position[id];
                                     }
                                     float total = 0.0f;
                                     for (int other : found->second) {
                                         total += order_position[other];
                                     }
                                     return total / float(found->second.size());
                                 };
                                 return mean(a) < mean(b);
                             });
        };
        std::unordered_map<int, std::vector<int>> parents;
        for (const auto& entry : forward) {
            for (int target : entry.second) {
                parents[target].push_back(entry.first);
            }
        }
        for (int sweep = 0; sweep < 3; ++sweep) {
            for (std::size_t c = 1; c < columns.size(); ++c) {
                barycenter_sort(columns[c], parents);
                refresh_positions();
            }
            for (std::size_t c = columns.size(); c-- > 1;) {
                barycenter_sort(columns[c - 1], forward);
                refresh_positions();
            }
        }

        
        
        std::unordered_map<int, float> node_y;
        std::unordered_map<int, float> node_height;
        for (int id : component) {
            node_height[id] = estimated_node_height(*nodes[id]);
        }
        for (std::size_t c = 0; c < columns.size(); ++c) {
            const std::vector<int>& column = columns[c];
            std::vector<float> desired(column.size(), 0.0f);
            for (std::size_t i = 0; i < column.size(); ++i) {
                const auto found = parents.find(column[i]);
                float centre_total = 0.0f;
                std::size_t centre_count = 0;
                if (found != parents.end()) {
                    for (int parent : found->second) {
                        if (!node_y.count(parent)) continue;
                        centre_total += node_y[parent] +
                            node_height[parent] * 0.5f;
                        ++centre_count;
                    }
                }
                desired[i] = centre_count > 0
                    ? centre_total / float(centre_count) -
                          node_height[column[i]] * 0.5f
                    : (i > 0 ? node_y[column[i - 1]] +
                                   node_height[column[i - 1]] + kRowGap
                             : 0.0f);
            }
            float cursor = -std::numeric_limits<float>::max();
            std::vector<float> placed(column.size(), 0.0f);
            for (std::size_t i = 0; i < column.size(); ++i) {
                placed[i] = std::max(desired[i], cursor);
                cursor = placed[i] + node_height[column[i]] + kRowGap;
            }
            
            
            if (!column.empty()) {
                float shift_total = 0.0f;
                for (std::size_t i = 0; i < column.size(); ++i) {
                    shift_total += placed[i] - desired[i];
                }
                const float shift = shift_total / float(column.size());
                for (std::size_t i = 0; i < column.size(); ++i) {
                    node_y[column[i]] = placed[i] - shift;
                }
            }
        }

        float component_min_y = 0.0f;
        bool first = true;
        for (int id : component) {
            if (first || node_y[id] < component_min_y) {
                component_min_y = node_y[id];
                first = false;
            }
        }
        float component_max_y = component_base_y;
        for (int id : component) {
            GraphNode* node = nodes[id];
            node->x = float(depth[id] - min_depth) * kColumnSpacing;
            node->y = component_base_y + node_y[id] - component_min_y;
            component_max_y = std::max(component_max_y,
                                       node->y + node_height[id]);
        }
        component_base_y = component_max_y + kComponentGap;

        std::vector<int> ordered = component;
        std::sort(ordered.begin(), ordered.end(), [&](int a, int b) {
            if (depth[a] != depth[b]) return depth[a] < depth[b];
            if (nodes[a]->y != nodes[b]->y) return nodes[a]->y < nodes[b]->y;
            return a < b;
        });
        for (int id : ordered) {
            if (id != root_story) layout_order.push_back(id);
        }
    }
}
