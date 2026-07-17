#include "BlueprintNodeRegistry.h"

#include "BlueprintCompiler.h"

#include "IconsFontAwesome6.h"

namespace Quest {
namespace Bp {

namespace {

constexpr uint32_t kDataColor = 0xFF2E5A2A;
constexpr uint32_t kVarColor = 0xFF1E6E62;



std::string var_ref(EmitContext& ctx, const Node& node) {
    return ctx.QuestSelf() + ".Var_" + node.prop;
}

bool var_exists(const EmitContext& ctx, const Node& node) {
    for (const Variable& v : ctx.quest->variables) {
        if (v.name == node.prop) return true;
    }
    return false;
}

}

void RegisterDataNodes() {
    {
        NodeDef def;
        def.type = "var.get";
        def.title = "Get Variable";
        def.category = "Variables";
        def.icon = ICON_FA_CIRCLE_DOWN;
        def.header_color = kVarColor;
        def.pure = true;
        def.pins = {
            {"Value", PinType::Any, PinDir::Output},
        };
        def.emit_expr = [](EmitContext& ctx, const Node& node, const Pin&)
            -> std::string {
            if (!var_exists(ctx, node)) {
                ctx.Error(node, "Pick a variable for Get Variable "
                                "(inspector).");
                return "nil";
            }
            return var_ref(ctx, node);
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "var.set";
        def.title = "Set Variable";
        def.category = "Variables";
        def.icon = ICON_FA_CIRCLE_UP;
        def.header_color = kVarColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Value", PinType::Any, PinDir::Input},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            if (!var_exists(ctx, node)) {
                ctx.Error(node, "Pick a variable for Set Variable "
                                "(inspector).");
                return;
            }
            ctx.Line(var_ref(ctx, node) + " = " + ctx.Expr(node, "Value"));
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "data.compare";
        def.title = "Compare";
        def.category = "Math & Logic";
        def.icon = ICON_FA_EQUALS;
        def.header_color = kDataColor;
        def.pure = true;
        def.pins = {
            {"A", PinType::Number, PinDir::Input},
            {"B", PinType::Number, PinDir::Input},
            {"Result", PinType::Bool, PinDir::Output},
        };
        
        def.emit_expr = [](EmitContext& ctx, const Node& node, const Pin&)
            -> std::string {
            const std::string op = node.prop.empty() ? ">=" : node.prop;
            return "(" + ctx.Expr(node, "A") + " " + op + " " +
                   ctx.Expr(node, "B") + ")";
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "data.and";
        def.title = "AND";
        def.category = "Math & Logic";
        def.icon = ICON_FA_DIAGRAM_PROJECT;
        def.header_color = kDataColor;
        def.pure = true;
        def.pins = {
            {"A", PinType::Bool, PinDir::Input},
            {"B", PinType::Bool, PinDir::Input},
            {"Result", PinType::Bool, PinDir::Output},
        };
        def.emit_expr = [](EmitContext& ctx, const Node& node, const Pin&)
            -> std::string {
            return "(" + ctx.Expr(node, "A") + " and " +
                   ctx.Expr(node, "B") + ")";
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "data.or";
        def.title = "OR";
        def.category = "Math & Logic";
        def.icon = ICON_FA_DIAGRAM_PROJECT;
        def.header_color = kDataColor;
        def.pure = true;
        def.pins = {
            {"A", PinType::Bool, PinDir::Input},
            {"B", PinType::Bool, PinDir::Input},
            {"Result", PinType::Bool, PinDir::Output},
        };
        def.emit_expr = [](EmitContext& ctx, const Node& node, const Pin&)
            -> std::string {
            return "(" + ctx.Expr(node, "A") + " or " + ctx.Expr(node, "B") +
                   ")";
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "data.not";
        def.title = "NOT";
        def.category = "Math & Logic";
        def.icon = ICON_FA_EXCLAMATION;
        def.header_color = kDataColor;
        def.pure = true;
        def.pins = {
            {"Value", PinType::Bool, PinDir::Input},
            {"Result", PinType::Bool, PinDir::Output},
        };
        def.emit_expr = [](EmitContext& ctx, const Node& node, const Pin&)
            -> std::string {
            return "(not " + ctx.Expr(node, "Value") + ")";
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "data.arithmetic";
        def.title = "Arithmetic";
        def.category = "Math & Logic";
        def.icon = ICON_FA_CALCULATOR;
        def.header_color = kDataColor;
        def.pure = true;
        def.pins = {
            {"A", PinType::Number, PinDir::Input},
            {"B", PinType::Number, PinDir::Input},
            {"Result", PinType::Number, PinDir::Output},
        };
        
        def.emit_expr = [](EmitContext& ctx, const Node& node, const Pin&)
            -> std::string {
            const std::string op = node.prop.empty() ? "+" : node.prop;
            return "(" + ctx.Expr(node, "A") + " " + op + " " +
                   ctx.Expr(node, "B") + ")";
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "data.random";
        def.title = "Random Range";
        def.category = "Math & Logic";
        def.icon = ICON_FA_DICE;
        def.header_color = kDataColor;
        def.pure = true;
        def.pins = {
            {"Min", PinType::Number, PinDir::Input, "0"},
            {"Max", PinType::Number, PinDir::Input, "1"},
            {"Result", PinType::Number, PinDir::Output},
        };
        def.emit_expr = [](EmitContext& ctx, const Node& node, const Pin&)
            -> std::string {
            return "math.random(" + ctx.Expr(node, "Min") + ", " +
                   ctx.Expr(node, "Max") + ")";
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "data.get_hero";
        def.title = "Get Hero";
        def.category = "Entity";
        def.icon = ICON_FA_USER;
        def.header_color = 0xFF1F4468;
        def.pure = true;
        def.pins = {
            {"Hero", PinType::Entity, PinDir::Output},
        };
        def.emit_expr = [](EmitContext&, const Node&, const Pin&)
            -> std::string { return "QuestManager.HeroEntity"; };
        Registry::Register(std::move(def));
    }
}

}
}
