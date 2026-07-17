#include "BlueprintNodeRegistry.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace Quest {
namespace Bp {
namespace Registry {

namespace {

std::vector<NodeDef>& defs() {
    static std::vector<NodeDef> d;
    return d;
}

PinValue parse_default(const PinSpec& spec) {
    PinValue v;
    if (!spec.default_value || !*spec.default_value) return v;
    switch (spec.type) {
        case PinType::Bool:
            v.b = std::strcmp(spec.default_value, "true") == 0;
            break;
        case PinType::Number:
            v.num = std::atof(spec.default_value);
            break;
        default:
            v.str = spec.default_value;
            break;
    }
    return v;
}

}

void Register(NodeDef def) {
    defs().push_back(std::move(def));
}

void EnsureRegistered() {
    static bool done = false;
    if (done) return;
    done = true;
    RegisterEventNodes();
    RegisterFlowNodes();
    RegisterDataNodes();
    RegisterQuestNodes();
    RegisterDialogueNodes();
    RegisterInventoryNodes();
    RegisterEntityNodes();
    RegisterWorldNodes();
    RegisterUtilNodes();
    RegisterPrereqNodes();
    RegisterGameVarNodes();
}

const NodeDef* Find(const std::string& type) {
    EnsureRegistered();
    for (const NodeDef& d : defs()) {
        if (d.type == type) return &d;
    }
    return nullptr;
}

const std::vector<NodeDef>& All() {
    EnsureRegistered();
    return defs();
}

std::vector<std::string> Categories() {
    EnsureRegistered();
    std::vector<std::string> out;
    for (const NodeDef& d : defs()) {
        if (std::find(out.begin(), out.end(), d.category) == out.end()) {
            out.push_back(d.category);
        }
    }
    return out;
}

int Instantiate(BlueprintQuest& quest, const std::string& type,
                float x, float y) {
    const NodeDef* def = Find(type);
    if (!def) return 0;
    Node node;
    node.id = quest.AllocId();
    node.type = def->type;
    node.x = x;
    node.y = y;
    node.pins.reserve(def->pins.size());
    for (const PinSpec& spec : def->pins) {
        Pin pin;
        pin.id = quest.AllocId();
        pin.name = spec.name;
        pin.type = spec.type;
        pin.dir = spec.dir;
        pin.optional = spec.optional;
        pin.value = parse_default(spec);
        node.pins.push_back(std::move(pin));
    }
    
    
    if (def->type == "prereq.story") {
        node.prereq.kind = Quest::AuthoredNodeKind::PrerequisiteStoryProgress;
    } else if (def->type == "prereq.quest_state") {
        node.prereq.kind = Quest::AuthoredNodeKind::PrerequisiteQuestState;
    } else if (def->type == "prereq.gameflow") {
        node.prereq.kind = Quest::AuthoredNodeKind::PrerequisiteGameflowFlag;
    } else if (def->type == "prereq.lua") {
        node.prereq.kind = Quest::AuthoredNodeKind::PrerequisiteLuaCondition;
    } else if (def->type == "prereq.hero") {
        node.prereq.kind =
            Quest::AuthoredNodeKind::PrerequisiteHeroRequirement;
    }
    node.prereq.id = node.id;

    quest.nodes.push_back(std::move(node));
    quest.Touch();
    return quest.nodes.back().id;
}

void SyncVariableNode(BlueprintQuest& quest, Node& node) {
    if (node.type != "var.get" && node.type != "var.set") return;
    const Variable* var = nullptr;
    for (const Variable& v : quest.variables) {
        if (v.name == node.prop) var = &v;
    }
    if (!var) return;
    for (Pin& pin : node.pins) {
        if (pin.name == "Value") {
            if (pin.type != var->type) {
                pin.type = var->type;
                quest.Touch();
            }
        }
    }
}

int AddDynamicExecOutput(BlueprintQuest& quest, Node& node) {
    const NodeDef* def = Find(node.type);
    if (!def || !def->dynamic_outputs) return 0;
    Pin pin;
    pin.id = quest.AllocId();
    pin.name = "Then " +
               std::to_string(node.CountPins(PinDir::Output, PinType::Exec));
    pin.type = PinType::Exec;
    pin.dir = PinDir::Output;
    pin.dynamic = true;
    node.pins.push_back(std::move(pin));
    quest.Touch();
    return node.pins.back().id;
}

}
}
}
