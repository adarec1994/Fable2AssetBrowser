#include "BlueprintNodeRegistry.h"

#include "BlueprintCompiler.h"

#include "IconsFontAwesome6.h"

namespace Quest {
namespace Bp {

namespace {
constexpr uint32_t kQuestColor = 0xFF6E5A1E;

std::string tracker_args(EmitContext& ctx) {
    return "QuestManager.HeroEntity, " + ctx.QuestSelf() + ".QuestName";
}
}

void RegisterQuestNodes() {
    {
        NodeDef def;
        def.type = "quest.set_active";
        def.title = "Set Quest Active";
        def.category = "Quest";
        def.icon = ICON_FA_STAR;
        def.header_color = kQuestColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Primary", PinType::Bool, PinDir::Input, "true"},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            ctx.Line("QuestTracker.SetAsActive(" + tracker_args(ctx) +
                     ", true)");
            const Pin* primary = node.FindPin("Primary", PinDir::Input);
            if (primary && primary->value.b) {
                ctx.Line("QuestTracker.SetAsPrimary(" + tracker_args(ctx) +
                         ")");
            }
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "quest.set_objective_text";
        def.title = "Set Objective Text";
        def.category = "Quest";
        def.icon = ICON_FA_BULLSEYE;
        def.header_color = kQuestColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Text", PinType::String, PinDir::Input},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            const Pin* text = node.FindPin("Text", PinDir::Input);
            const std::string tag =
                ctx.TextTag(node, *text, text->value.str);
            ctx.Line("QuestTracker.SetObjectiveTag(" + tracker_args(ctx) +
                     ", " + LuaQuote(tag) + ")");
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "quest.complete_objective";
        def.title = "Complete Objective";
        def.category = "Quest";
        def.icon = ICON_FA_CHECK;
        def.header_color = kQuestColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Text", PinType::String, PinDir::Input},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            const Pin* text = node.FindPin("Text", PinDir::Input);
            const std::string tag =
                ctx.TextTag(node, *text, text->value.str);
            ctx.Line("QuestTracker.SetObjectiveAsCompleted(" +
                     tracker_args(ctx) + ", " + LuaQuote(tag) + ")");
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "quest.set_objective_level";
        def.title = "Set Objective Level (golden trail)";
        def.category = "Quest";
        def.icon = ICON_FA_MAP_PIN;
        def.header_color = kQuestColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Level", PinType::Level, PinDir::Input},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            ctx.Line("QuestTracker.SetObjectiveLevel(" + tracker_args(ctx) +
                     ", " + ctx.Expr(node, "Level") + ")");
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "quest.set_objective_entity";
        def.title = "Set Trail Target (golden trail)";
        def.category = "Quest";
        def.icon = ICON_FA_LOCATION_CROSSHAIRS;
        def.header_color = kQuestColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Entity", PinType::Entity, PinDir::Input},
            {"On", PinType::Bool, PinDir::Input, "true"},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            ctx.Line("QuestTracker.SetObjectiveEntity(" + tracker_args(ctx) +
                     ", " + ctx.Expr(node, "Entity") + ", " +
                     ctx.Expr(node, "On") + ")");
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "quest.complete";
        def.title = "Complete Quest";
        def.category = "Quest";
        def.icon = ICON_FA_FLAG_CHECKERED;
        def.header_color = kQuestColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Epilogue text", PinType::String, PinDir::Input, "", true},
            {"Renown shown", PinType::Number, PinDir::Input, "0"},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            ctx.Line("QuestTracker.ClearAllObjectiveEntities(" +
                     tracker_args(ctx) + ")");
            const Pin* epilogue = node.FindPin("Epilogue text",
                                               PinDir::Input);
            if (epilogue && !epilogue->value.str.empty()) {
                const std::string tag =
                    ctx.TextTag(node, *epilogue, epilogue->value.str);
                ctx.Line("QuestTracker.ShowEpilogueScreen(" +
                         tracker_args(ctx) + ", " +
                         LuaQuote("Quest_" + ctx.quest_class) + ", " +
                         LuaQuote(tag) + ", " +
                         ctx.Expr(node, "Renown shown") + ", \"\")");
            }
            ctx.Line("QuestTracker.SetAsCompleted(" + tracker_args(ctx) +
                     ", true, true)");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "quest.unlock_other";
        def.title = "Unlock Quest";
        def.category = "Quest";
        def.icon = ICON_FA_UNLOCK;
        def.header_color = kQuestColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Quest ID", PinType::String, PinDir::Input},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            ctx.Line("QuestTracker.Unlock(QuestManager.HeroEntity, " +
                     ctx.Expr(node, "Quest ID") + ")");
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "quest.is_completed";
        def.title = "Is Quest Completed";
        def.category = "Quest";
        def.icon = ICON_FA_CLIPBOARD_CHECK;
        def.header_color = kQuestColor;
        def.pure = true;
        def.pins = {
            {"Quest ID", PinType::String, PinDir::Input},
            {"Completed", PinType::Bool, PinDir::Output},
        };
        def.emit_expr = [](EmitContext& ctx, const Node& node, const Pin&)
            -> std::string {
            return "QuestTracker.IsCompleted(QuestManager.HeroEntity, " +
                   ctx.Expr(node, "Quest ID") + ")";
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "quest.breadcrumb_radius";
        def.title = "Set Breadcrumb Radius";
        def.category = "Quest";
        def.icon = ICON_FA_CIRCLE_DOT;
        def.header_color = kQuestColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Radius", PinType::Number, PinDir::Input, "5"},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            ctx.Line("QuestTracker.SetObjectiveBreadcrumbRadius(" +
                     tracker_args(ctx) + ", " + ctx.Expr(node, "Radius") +
                     ")");
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "quest.clear_trail";
        def.title = "Clear Golden Trail";
        def.category = "Quest";
        def.icon = ICON_FA_ERASER;
        def.header_color = kQuestColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            ctx.Line("QuestTracker.ClearAllObjectiveEntities(" +
                     tracker_args(ctx) + ")");
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "quest.fail";
        def.title = "Fail Quest";
        def.category = "Quest";
        def.icon = ICON_FA_XMARK;
        def.header_color = kQuestColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            (void)node;
            ctx.Line("QuestTracker.ClearAllObjectiveEntities(" +
                     tracker_args(ctx) + ")");
            ctx.Line("QuestTracker.SetAsFailed(" + tracker_args(ctx) + ")");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "quest.set_quest_giver";
        def.title = "Set Quest Giver";
        def.category = "Quest";
        def.icon = ICON_FA_USER_TAG;
        def.header_color = kQuestColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Entity", PinType::Entity, PinDir::Input},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            ctx.Line("QuestTracker.SetQuestGiver(" + tracker_args(ctx) +
                     ", " + ctx.Expr(node, "Entity") + ")");
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
}

}
}
