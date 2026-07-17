#include "BlueprintNodeRegistry.h"

#include "BlueprintCompiler.h"

namespace Quest {
namespace Bp {

namespace {

constexpr uint32_t kPrereqColor = 0xFF6E5A1E;





void register_prereq(const char* type, const char* title) {
    NodeDef def;
    def.type = type;
    def.title = title;
    def.category = "Prerequisites";
    def.icon = "";
    def.header_color = kPrereqColor;
    def.pure = true;
    def.pins = {
        {"Gate", PinType::Prereq, PinDir::Output},
    };
    def.emit_expr = [](EmitContext&, const Node&, const Pin&)
        -> std::string { return "true"; };   
    Registry::Register(std::move(def));
}

}

void RegisterPrereqNodes() {
    register_prereq("prereq.story", "Story Progression Window");
    register_prereq("prereq.quest_state", "Quest State Requirement");
    register_prereq("prereq.gameflow", "Named Gameflow Condition");
    register_prereq("prereq.lua", "Lua Condition");
    register_prereq("prereq.hero", "Hero Requirement");
}

}
}
