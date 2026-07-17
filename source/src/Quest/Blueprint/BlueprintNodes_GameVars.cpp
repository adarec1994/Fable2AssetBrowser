#include "BlueprintNodeRegistry.h"

#include "BlueprintCompiler.h"

namespace Quest {
namespace Bp {

namespace {

constexpr uint32_t kGameVarColor = 0xFF1E6E62;




void register_stat(const char* key, const char* label,
                   const std::string& get_call,
                   const std::string& modify_prefix) {
    {
        NodeDef def;
        def.type = std::string("game.get_") + key;
        def.title = std::string("Get ") + label;
        def.category = "Game Variables";
        def.header_color = kGameVarColor;
        def.pure = true;
        def.pins = {
            {"Value", PinType::Number, PinDir::Output},
        };
        def.emit_expr = [get_call](EmitContext&, const Node&, const Pin&)
            -> std::string { return get_call; };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = std::string("game.set_") + key;
        def.title = std::string("Set ") + label;
        def.category = "Game Variables";
        def.header_color = kGameVarColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Value", PinType::Number, PinDir::Input},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [get_call, modify_prefix](EmitContext& ctx,
                                             const Node& node, const Pin&) {
            ctx.Line(modify_prefix + "(QuestManager.HeroEntity, (" +
                     ctx.Expr(node, "Value") + ") - " + get_call + ")");
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = std::string("game.add_") + key;
        def.title = std::string("Add ") + label;
        def.category = "Game Variables";
        def.header_color = kGameVarColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Amount", PinType::Number, PinDir::Input, "10"},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [modify_prefix](EmitContext& ctx, const Node& node,
                                   const Pin&) {
            ctx.Line(modify_prefix + "(QuestManager.HeroEntity, " +
                     ctx.Expr(node, "Amount") + ")");
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
}

}

void RegisterGameVarNodes() {
    register_stat("gold", "Gold", "Money.Get(QuestManager.HeroEntity)",
                  "Money.Modify");
    register_stat("renown", "Renown",
                  "Stats.GetRenown(QuestManager.HeroEntity)",
                  "Stats.ModifyRenown");
    register_stat("alignment", "Alignment (morality)",
                  "Stats.GetMorality(QuestManager.HeroEntity)",
                  "Stats.ModifyMorality");
    register_stat("purity", "Purity",
                  "Stats.GetPurity(QuestManager.HeroEntity)",
                  "Stats.ModifyPurity");
    {
        NodeDef def;
        def.type = "game.get_experience";
        def.title = "Get Experience";
        def.category = "Game Variables";
        def.header_color = kGameVarColor;
        def.pure = true;
        def.pins = {
            {"Value", PinType::Number, PinDir::Output},
        };
        
        def.emit_expr = [](EmitContext&, const Node& node, const Pin&)
            -> std::string {
            const std::string kind =
                node.prop.empty() ? "GENERAL" : node.prop;
            return "Experience.Get(QuestManager.HeroEntity, "
                   "EExperienceType.EXPERIENCE_" + kind + ")";
        };
        Registry::Register(std::move(def));
    }
}

}
}
