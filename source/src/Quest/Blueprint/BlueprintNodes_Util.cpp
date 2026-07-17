#include "BlueprintNodeRegistry.h"

#include "BlueprintCompiler.h"

#include "IconsFontAwesome6.h"

#include <sstream>

namespace Quest {
namespace Bp {

namespace {

constexpr uint32_t kUtilColor = 0xFF44474D;
constexpr uint32_t kDataColor = 0xFF2E5A2A;



std::string substitute_snippet(EmitContext& ctx, const Node& node) {
    std::string code = node.prop;
    const char* names[4] = {"$1", "$2", "$3", "$4"};
    const char* pins[4] = {"1", "2", "3", "4"};
    for (int i = 3; i >= 0; --i) {
        size_t at;
        while ((at = code.find(names[i])) != std::string::npos) {
            code.replace(at, 2, ctx.Expr(node, pins[i]));
        }
    }
    return code;
}

}

void RegisterUtilNodes() {
    {
        NodeDef def;
        def.type = "util.lua_snippet";
        def.title = "Lua Snippet";
        def.category = "Utilities";
        def.icon = ICON_FA_CODE;
        def.header_color = kUtilColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"1", PinType::Any, PinDir::Input, "", true},
            {"2", PinType::Any, PinDir::Input, "", true},
            {"3", PinType::Any, PinDir::Input, "", true},
            {"4", PinType::Any, PinDir::Input, "", true},
            {"", PinType::Exec, PinDir::Output},
        };
        
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            if (node.prop.empty()) {
                ctx.Warn(node, "Lua Snippet is empty.");
            }
            std::istringstream lines(substitute_snippet(ctx, node));
            std::string line;
            while (std::getline(lines, line)) {
                ctx.Line(line);
            }
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "util.lua_expression";
        def.title = "Lua Expression";
        def.category = "Utilities";
        def.icon = ICON_FA_TERMINAL;
        def.header_color = kUtilColor;
        def.pure = true;
        def.pins = {
            {"1", PinType::Any, PinDir::Input, "", true},
            {"2", PinType::Any, PinDir::Input, "", true},
            {"Result", PinType::Any, PinDir::Output},
        };
        def.emit_expr = [](EmitContext& ctx, const Node& node, const Pin&)
            -> std::string {
            if (node.prop.empty()) {
                ctx.Error(node, "Lua Expression has no code (inspector).");
                return "nil";
            }
            return "(" + substitute_snippet(ctx, node) + ")";
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "data.to_string";
        def.title = "To String";
        def.category = "Math & Logic";
        def.icon = ICON_FA_QUOTE_LEFT;
        def.header_color = kDataColor;
        def.pure = true;
        def.pins = {
            {"Value", PinType::Any, PinDir::Input},
            {"Text", PinType::String, PinDir::Output},
        };
        def.emit_expr = [](EmitContext& ctx, const Node& node, const Pin&)
            -> std::string {
            return "tostring(" + ctx.Expr(node, "Value") + ")";
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "data.concat";
        def.title = "Append Text";
        def.category = "Math & Logic";
        def.icon = ICON_FA_LINK;
        def.header_color = kDataColor;
        def.pure = true;
        def.pins = {
            {"A", PinType::String, PinDir::Input},
            {"B", PinType::String, PinDir::Input},
            {"Text", PinType::String, PinDir::Output},
        };
        def.emit_expr = [](EmitContext& ctx, const Node& node, const Pin&)
            -> std::string {
            return "(" + ctx.Expr(node, "A") + " .. " + ctx.Expr(node, "B") +
                   ")";
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "util.skip_childhood";
        def.title = "Skip Childhood";
        def.category = "Utilities";
        def.header_color = kUtilColor;
        def.latent = true;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"", PinType::Exec, PinDir::Output},
        };
        
        
        
        
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            const std::string q = ctx.QuestSelf();
            ctx.Open("while Gameflow.Childhood == nil and "
                     "Gameflow.PositionInGameflow <= ScriptEnum.DebugQC060 "
                     "do");
            ctx.Line("coroutine.yield()");
            ctx.Close("end");
            ctx.Open("if Gameflow.Childhood ~= nil then");
            ctx.Open("while Gameflow.ChildhoodVars == nil or "
                     "Gameflow.ChildhoodVars.SkipToLuciensStudy == nil do");
            ctx.Line("coroutine.yield()");
            ctx.Close("end");
            ctx.Line("local dog = GetDog()");
            ctx.Open("if dog == nil or not dog:IsAlive() then");
            ctx.Line("ScriptFunction.CreateDog()");
            ctx.Line("self:WaitFor(function()");
            ctx.Line("  local created_dog = GetDog()");
            ctx.Line("  return created_dog ~= nil and created_dog:IsAlive()");
            ctx.Line("end)");
            ctx.Close("end");
            ctx.Line("QuestManager.DogEntity = GetDog()");
            ctx.Line("Gameflow.ChildhoodVars.SkipToLuciensStudy()");
            ctx.Line("self:WaitFor(function()");
            ctx.Line("  return IsLevelLoaded(\"FairfaxCastleGardens\")");
            ctx.Line("end)");
            ctx.Line("local childhood = Gameflow.Childhood");
            ctx.Open("if childhood ~= nil and not childhood.Terminated then");
            ctx.Line("childhood.DisperseCrowd = true");
            ctx.Line("childhood.MurgoPitchDone = true");
            ctx.Line("childhood.Morning = true");
            ctx.Line("childhood.CarriageBoarded = true");
            ctx.Line("childhood.GoToCastle = true");
            ctx.Line("childhood.CircleActive = true");
            ctx.Line("childhood.EndChildhood = true");
            ctx.Close("end");
            ctx.Close("end");
            (void)q;
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
}

}
}
