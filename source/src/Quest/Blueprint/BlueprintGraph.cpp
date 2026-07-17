#include "BlueprintGraph.h"

#include <algorithm>
#include <cstring>
#include <unordered_set>

namespace Quest {
namespace Bp {

const char* PinTypeName(PinType type) {
    switch (type) {
        case PinType::Exec:      return "exec";
        case PinType::Bool:      return "bool";
        case PinType::Number:    return "number";
        case PinType::String:    return "string";
        case PinType::TextTag:   return "text tag";
        case PinType::Entity:    return "entity";
        case PinType::EntityDef: return "entity definition";
        case PinType::Item:      return "item";
        case PinType::Marker:    return "marker";
        case PinType::Level:     return "level";
        case PinType::Vector3:   return "vector";
        case PinType::Any:       return "any";
        case PinType::Prereq:    return "prerequisite";
    }
    return "?";
}

uint32_t PinTypeColor(PinType type) {
    switch (type) {
        case PinType::Exec:      return 0xFFEAEAEA;
        case PinType::Bool:      return 0xFFC1443C;
        case PinType::Number:    return 0xFF7FBF4D;
        case PinType::String:    return 0xFFD069D0;
        case PinType::TextTag:   return 0xFFE39BE3;
        case PinType::Entity:    return 0xFF5AA9E6;
        case PinType::EntityDef: return 0xFF2D6BA3;
        case PinType::Item:      return 0xFFE0913F;
        case PinType::Marker:    return 0xFF43C8C8;
        case PinType::Level:     return 0xFFB3A63F;
        case PinType::Vector3:   return 0xFFE8D44D;
        case PinType::Any:       return 0xFFBFC5CC;
        case PinType::Prereq:    return 0xFFD4AF37;
    }
    return 0xFFFFFFFF;
}

bool PinTypesCompatible(PinType from, PinType to) {
    if (from == to) return true;
    if (from == PinType::Exec || to == PinType::Exec) return false;
    if (from == PinType::Prereq || to == PinType::Prereq) return false;
    if (from == PinType::Any || to == PinType::Any) return true;
    if (from == PinType::Number && to == PinType::String) return true;
    if (from == PinType::String && to == PinType::TextTag) return true;
    return false;
}

Pin* Node::FindPin(int pin_id) {
    for (Pin& p : pins) {
        if (p.id == pin_id) return &p;
    }
    return nullptr;
}

const Pin* Node::FindPin(int pin_id) const {
    for (const Pin& p : pins) {
        if (p.id == pin_id) return &p;
    }
    return nullptr;
}

Pin* Node::FindPin(const char* name, PinDir dir) {
    for (Pin& p : pins) {
        if (p.dir == dir && p.name == name) return &p;
    }
    return nullptr;
}

const Pin* Node::FindPin(const char* name, PinDir dir) const {
    for (const Pin& p : pins) {
        if (p.dir == dir && p.name == name) return &p;
    }
    return nullptr;
}

int Node::CountPins(PinDir dir, PinType type) const {
    int n = 0;
    for (const Pin& p : pins) {
        if (p.dir == dir && p.type == type) ++n;
    }
    return n;
}

Node* BlueprintQuest::NodeById(int node_id) {
    for (Node& n : nodes) {
        if (n.id == node_id) return &n;
    }
    return nullptr;
}

const Node* BlueprintQuest::NodeById(int node_id) const {
    for (const Node& n : nodes) {
        if (n.id == node_id) return &n;
    }
    return nullptr;
}

Node* BlueprintQuest::NodeOfPin(int pin_id) {
    for (Node& n : nodes) {
        if (n.FindPin(pin_id)) return &n;
    }
    return nullptr;
}

const Node* BlueprintQuest::NodeOfPin(int pin_id) const {
    for (const Node& n : nodes) {
        if (n.FindPin(pin_id)) return &n;
    }
    return nullptr;
}

Pin* BlueprintQuest::PinById(int pin_id) {
    for (Node& n : nodes) {
        if (Pin* p = n.FindPin(pin_id)) return p;
    }
    return nullptr;
}

const Pin* BlueprintQuest::PinById(int pin_id) const {
    for (const Node& n : nodes) {
        if (const Pin* p = n.FindPin(pin_id)) return p;
    }
    return nullptr;
}

const Link* BlueprintQuest::LinkById(int link_id) const {
    for (const Link& l : links) {
        if (l.id == link_id) return &l;
    }
    return nullptr;
}

const Link* BlueprintQuest::LinkInto(int input_pin_id) const {
    for (const Link& l : links) {
        if (l.to_pin == input_pin_id) return &l;
    }
    return nullptr;
}

std::vector<const Link*> BlueprintQuest::LinksFrom(int output_pin_id) const {
    std::vector<const Link*> out;
    for (const Link& l : links) {
        if (l.from_pin == output_pin_id) out.push_back(&l);
    }
    return out;
}

std::vector<const Link*> BlueprintQuest::LinksInto(int input_pin_id) const {
    std::vector<const Link*> out;
    for (const Link& l : links) {
        if (l.to_pin == input_pin_id) out.push_back(&l);
    }
    return out;
}

bool BlueprintQuest::IsPinLinked(int pin_id) const {
    for (const Link& l : links) {
        if (l.from_pin == pin_id || l.to_pin == pin_id) return true;
    }
    return false;
}

int BlueprintQuest::ExecInDegree(int node_id) const {
    const Node* node = NodeById(node_id);
    if (!node) return 0;
    int degree = 0;
    for (const Pin& p : node->pins) {
        if (p.dir != PinDir::Input || p.type != PinType::Exec) continue;
        degree += (int)LinksInto(p.id).size();
    }
    return degree;
}

bool ExecReaches(const BlueprintQuest& quest, int from_node, int to_node) {
    if (from_node == to_node) return true;
    std::unordered_set<int> visited;
    std::vector<int> stack{from_node};
    while (!stack.empty()) {
        const int cur = stack.back();
        stack.pop_back();
        if (!visited.insert(cur).second) continue;
        const Node* node = quest.NodeById(cur);
        if (!node) continue;
        for (const Pin& p : node->pins) {
            if (p.dir != PinDir::Output || p.type != PinType::Exec) continue;
            for (const Link* l : quest.LinksFrom(p.id)) {
                const Node* next = quest.NodeOfPin(l->to_pin);
                if (!next) continue;
                if (next->id == to_node) return true;
                stack.push_back(next->id);
            }
        }
    }
    return false;
}

bool CanConnect(const BlueprintQuest& quest, int from_pin, int to_pin,
                std::string& reason) {
    const Pin* out = quest.PinById(from_pin);
    const Pin* in = quest.PinById(to_pin);
    if (!out || !in) {
        reason = "Unknown pin.";
        return false;
    }
    if (out->dir == in->dir) {
        reason = out->dir == PinDir::Output
                     ? "Both pins are outputs."
                     : "Both pins are inputs.";
        return false;
    }
    if (out->dir != PinDir::Output) {
        std::swap(out, in);
        std::swap(from_pin, to_pin);
    }
    const Node* out_node = quest.NodeOfPin(out->id);
    const Node* in_node = quest.NodeOfPin(in->id);
    if (!out_node || !in_node) {
        reason = "Unknown node.";
        return false;
    }
    if (out_node->id == in_node->id) {
        reason = "Cannot connect a node to itself.";
        return false;
    }
    if ((out->type == PinType::Exec) != (in->type == PinType::Exec)) {
        reason = "Execution pins only connect to execution pins.";
        return false;
    }
    if (!PinTypesCompatible(out->type, in->type)) {
        reason = std::string(PinTypeName(out->type)) + " does not fit into " +
                 PinTypeName(in->type) + ".";
        return false;
    }
    if (out->type == PinType::Exec &&
        ExecReaches(quest, in_node->id, out_node->id)) {
        reason = "That would create an execution loop - use a While loop "
                 "node instead.";
        return false;
    }
    return true;
}

int BlueprintQuest::AddLink(int from_pin, int to_pin, std::string& reason) {
    {
        
        const Pin* a = PinById(from_pin);
        const Pin* b = PinById(to_pin);
        if (a && b && a->dir == PinDir::Input && b->dir == PinDir::Output) {
            std::swap(from_pin, to_pin);
            std::swap(a, b);
        }
    }
    if (!CanConnect(*this, from_pin, to_pin, reason)) return 0;

    const Pin* out = PinById(from_pin);
    const Pin* in = PinById(to_pin);

    
    
    
    std::vector<int> to_remove;
    for (const Link& l : links) {
        if (out->type == PinType::Exec && l.from_pin == from_pin) {
            to_remove.push_back(l.id);
        }
        if (out->type != PinType::Exec && out->type != PinType::Prereq &&
            l.to_pin == to_pin) {
            to_remove.push_back(l.id);
        }
        if (l.from_pin == from_pin && l.to_pin == to_pin) {
            to_remove.push_back(l.id);
        }
    }
    for (int id : to_remove) RemoveLink(id);

    Link link;
    link.id = AllocId();
    link.from_pin = from_pin;
    link.to_pin = to_pin;
    links.push_back(link);
    Touch();
    return link.id;
}

bool BlueprintQuest::RemoveLink(int link_id) {
    for (size_t i = 0; i < links.size(); ++i) {
        if (links[i].id == link_id) {
            links.erase(links.begin() + (ptrdiff_t)i);
            Touch();
            return true;
        }
    }
    return false;
}

bool BlueprintQuest::RemoveNode(int node_id) {
    const Node* node = NodeById(node_id);
    if (!node) return false;
    std::unordered_set<int> pin_ids;
    for (const Pin& p : node->pins) pin_ids.insert(p.id);
    for (size_t i = links.size(); i-- > 0;) {
        if (pin_ids.count(links[i].from_pin) ||
            pin_ids.count(links[i].to_pin)) {
            links.erase(links.begin() + (ptrdiff_t)i);
        }
    }
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].id == node_id) {
            nodes.erase(nodes.begin() + (ptrdiff_t)i);
            Touch();
            return true;
        }
    }
    return false;
}

}
}
