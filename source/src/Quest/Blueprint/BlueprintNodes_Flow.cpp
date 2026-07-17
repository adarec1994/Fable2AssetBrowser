#include "BlueprintNodeRegistry.h"

#include "BlueprintCompiler.h"

#include "IconsFontAwesome6.h"

namespace Quest {
namespace Bp {

namespace {
constexpr uint32_t kFlowColor = 0xFF3D4148;
}

void RegisterFlowNodes() {
    {
        NodeDef def;
        def.type = "flow.branch";
        def.title = "Branch";
        def.category = "Flow Control";
        def.icon = ICON_FA_CODE_BRANCH;
        def.header_color = kFlowColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Condition", PinType::Bool, PinDir::Input},
            {"True", PinType::Exec, PinDir::Output},
            {"False", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            ctx.Open("if " + ctx.Expr(node, "Condition") + " then");
            ctx.Chain(node, "True");
            ctx.Close("else");
            ++ctx.indent;
            ctx.Chain(node, "False");
            ctx.Close("end");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "flow.sequence";
        def.title = "Sequence";
        def.category = "Flow Control";
        def.icon = ICON_FA_LIST_OL;
        def.header_color = kFlowColor;
        def.dynamic_outputs = true;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Then 0", PinType::Exec, PinDir::Output},
            {"Then 1", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            for (const Pin& p : node.pins) {
                if (p.dir == PinDir::Output && p.type == PinType::Exec) {
                    ctx.ChainFromPin(p);
                }
            }
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "flow.delay";
        def.title = "Delay";
        def.category = "Flow Control";
        def.icon = ICON_FA_CLOCK;
        def.header_color = kFlowColor;
        def.latent = true;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Seconds", PinType::Number, PinDir::Input, "1"},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            ctx.Line("self:WaitForTimeInSeconds(" +
                     ctx.Expr(node, "Seconds") + ")");
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "flow.wait_until";
        def.title = "Wait Until";
        def.category = "Flow Control";
        def.icon = ICON_FA_HOURGLASS_HALF;
        def.header_color = kFlowColor;
        def.latent = true;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Condition", PinType::Bool, PinDir::Input},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            
            ctx.Open("while not (" + ctx.Expr(node, "Condition") + ") do");
            ctx.Line("coroutine.yield()");
            ctx.Close("end");
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "flow.while";
        def.title = "While Loop";
        def.category = "Flow Control";
        def.icon = ICON_FA_ROTATE;
        def.header_color = kFlowColor;
        def.latent = true;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Condition", PinType::Bool, PinDir::Input},
            {"Body", PinType::Exec, PinDir::Output},
            {"Completed", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            ctx.Open("while " + ctx.Expr(node, "Condition") + " do");
            ctx.Chain(node, "Body");
            ctx.Line("coroutine.yield()");
            ctx.Close("end");
            ctx.Chain(node, "Completed");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "flow.for_loop";
        def.title = "For Loop";
        def.category = "Flow Control";
        def.icon = ICON_FA_REPEAT;
        def.header_color = kFlowColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"First", PinType::Number, PinDir::Input, "1"},
            {"Last", PinType::Number, PinDir::Input, "10"},
            {"Body", PinType::Exec, PinDir::Output},
            {"Index", PinType::Number, PinDir::Output},
            {"Completed", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            const Pin* index = node.FindPin("Index", PinDir::Output);
            const std::string it = "i_" + std::to_string(node.id);
            ctx.Open("for " + it + " = " + ctx.Expr(node, "First") + ", " +
                     ctx.Expr(node, "Last") + " do");
            if (index) {
                ctx.Line("self.Out_" + std::to_string(index->id) + " = " +
                         it);
            }
            ctx.Chain(node, "Body");
            ctx.Close("end");
            ctx.Chain(node, "Completed");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "flow.do_n";
        def.title = "Do N Times";
        def.category = "Flow Control";
        def.icon = ICON_FA_HASHTAG;
        def.header_color = kFlowColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Reset", PinType::Exec, PinDir::Input, "", true},
            {"N", PinType::Number, PinDir::Input, "3"},
            {"", PinType::Exec, PinDir::Output},
            {"Count", PinType::Number, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin& entered) {
            const std::string field = ctx.StateField(node, "DoN", "0");
            if (entered.name == "Reset") {
                ctx.Line(field + " = 0");
                return;
            }
            const Pin* count = node.FindPin("Count", PinDir::Output);
            ctx.Open("if " + field + " < " + ctx.Expr(node, "N") + " then");
            ctx.Line(field + " = " + field + " + 1");
            if (count) {
                ctx.Line("self.Out_" + std::to_string(count->id) + " = " +
                         field);
            }
            ctx.Chain(node, "");
            ctx.Close("end");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "flow.do_once";
        def.title = "Do Once";
        def.category = "Flow Control";
        def.icon = ICON_FA_1;
        def.header_color = kFlowColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Reset", PinType::Exec, PinDir::Input, "", true},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin& entered) {
            const std::string field = ctx.StateField(node, "Once", "false");
            if (entered.name == "Reset") {
                ctx.Line(field + " = false");
                return;
            }
            ctx.Open("if not " + field + " then");
            ctx.Line(field + " = true");
            ctx.Chain(node, "");
            ctx.Close("end");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "flow.flip_flop";
        def.title = "FlipFlop";
        def.category = "Flow Control";
        def.icon = ICON_FA_SHUFFLE;
        def.header_color = kFlowColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"A", PinType::Exec, PinDir::Output},
            {"B", PinType::Exec, PinDir::Output},
            {"Is A", PinType::Bool, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            const std::string next = ctx.StateField(node, "FlipA", "true");
            const Pin* is_a = node.FindPin("Is A", PinDir::Output);
            const std::string taken =
                is_a ? "self.Out_" + std::to_string(is_a->id) : "";
            ctx.Open("if " + next + " then");
            ctx.Line(next + " = false");
            if (!taken.empty()) ctx.Line(taken + " = true");
            ctx.Chain(node, "A");
            ctx.Close("else");
            ++ctx.indent;
            ctx.Line(next + " = true");
            if (!taken.empty()) ctx.Line(taken + " = false");
            ctx.Chain(node, "B");
            ctx.Close("end");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "flow.gate";
        def.title = "Gate";
        def.category = "Flow Control";
        def.icon = ICON_FA_DOOR_OPEN;
        def.header_color = kFlowColor;
        def.pins = {
            {"Enter", PinType::Exec, PinDir::Input},
            {"Open", PinType::Exec, PinDir::Input, "", true},
            {"Close", PinType::Exec, PinDir::Input, "", true},
            {"Toggle", PinType::Exec, PinDir::Input, "", true},
            {"Start closed", PinType::Bool, PinDir::Input, "false"},
            {"Exit", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin& entered) {
            const Pin* start_closed = node.FindPin("Start closed",
                                                   PinDir::Input);
            const std::string field = ctx.StateField(
                node, "GateOpen",
                (start_closed && start_closed->value.b) ? "false" : "true");
            if (entered.name == "Open") {
                ctx.Line(field + " = true");
            } else if (entered.name == "Close") {
                ctx.Line(field + " = false");
            } else if (entered.name == "Toggle") {
                ctx.Line(field + " = not " + field);
            } else {
                ctx.Open("if " + field + " then");
                ctx.Chain(node, "Exit");
                ctx.Close("end");
            }
        };
        Registry::Register(std::move(def));
    }
}

}
}
