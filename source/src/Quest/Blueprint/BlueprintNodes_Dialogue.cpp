#include "BlueprintNodeRegistry.h"

#include "BlueprintCompiler.h"

#include "IconsFontAwesome6.h"

namespace Quest {
namespace Bp {

namespace {
constexpr uint32_t kDialogueColor = 0xFF4A2E5C;
}

void RegisterDialogueNodes() {
    {
        NodeDef def;
        def.type = "dialogue.say_line";
        def.title = "Say Line";
        def.category = "Dialogue & UI";
        def.icon = ICON_FA_COMMENT;
        def.header_color = kDialogueColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Speaker", PinType::Entity, PinDir::Input},
            {"Text", PinType::String, PinDir::Input},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            const Pin* text = node.FindPin("Text", PinDir::Input);
            const std::string tag =
                ctx.TextTag(node, *text, text->value.str);
            ctx.Line("ScriptFunction.SaySimLine(" +
                     ctx.Expr(node, "Speaker") + ", " + LuaQuote(tag) + ")");
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "dialogue.offer_quest";
        def.title = "Offer Quest (hold A toaster)";
        def.category = "Dialogue & UI";
        def.icon = ICON_FA_HANDSHAKE;
        def.header_color = kDialogueColor;
        def.scope = NodeScope::Entity;
        def.latent = true;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Text", PinType::String, PinDir::Input},
            {"Accepted", PinType::Exec, PinDir::Output},
            {"Declined", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            if (!ctx.entity_scope) {
                ctx.Error(node,
                          "Offer Quest must run on an entity chain (start "
                          "it from an entity event such as On Hero Near "
                          "Entity).");
                return;
            }
            const Pin* text = node.FindPin("Text", PinDir::Input);
            const std::string tag =
                ctx.TextTag(node, *text, text->value.str);
            const std::string suffix = std::to_string(node.id);
            const std::string box = "box_tag_" + suffix;
            const std::string accepted = "accepted_" + suffix;
            const std::string posted = "posted_" + suffix;
            const std::string message = "message_" + suffix;
            const std::string within_range =
                "(self.BpInteractionRadius == nil or "
                "IsDistanceBetweenThingsUnder(self.Entity, "
                "QuestManager.HeroEntity, self.BpInteractionRadius))";

            ctx.Line("local " + box + " = " +
                     LuaQuote("F2AB_" + ctx.quest_class + "_NODE_" + suffix));
            ctx.Line("local " + accepted + " = false");
            ctx.Line("GUI.DisplayInfoBoxParams({");
            ctx.Line("  Name = " + box + ",");
            ctx.Line("  Title = " +
                     LuaQuote("Quest_" + ctx.quest_class) + ",");
            ctx.Line("  AcceptText = \"GUI_ACCEPT\",");
            ctx.Line("  IsHoldAButton = true,");
            ctx.Line("  ShowYButton = false,");
            ctx.Line("  DisplayBoxStyle = "
                     "EDisplayBoxStyle.DBS_QUEST_ACCEPTANCE");
            ctx.Line("}, " + LuaQuote(tag) + ")");
            ctx.Open("while not " + accepted + " and " + within_range +
                     " do");
            ctx.Line("local " + posted + ", " + message +
                     " = MessageEvents.IsMessagePosted("
                     "EMessageEventType.MESSAGE_EVENT_INFOBOX, "
                     "self.LastMessageID_PressedAButton)");
            ctx.Open("if " + posted + " then");
            ctx.Line("self.LastMessageID_PressedAButton = " + message +
                     ":GetID()");
            ctx.Line(accepted + " = (" + message +
                     ":GetExtraDataAsNumber() == 1)");
            ctx.Close("end");
            ctx.Open("if not " + accepted + " then");
            ctx.Line("coroutine.yield()");
            ctx.Close("end");
            ctx.Close("end");
            ctx.Line("GUI.RemoveDisplayBox(" + box + ")");
            ctx.Open("if " + accepted + " then");
            ctx.Chain(node, "Accepted");
            ctx.Close("else");
            ++ctx.indent;
            ctx.Chain(node, "Declined");
            ctx.Close("end");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "dialogue.message_box";
        def.title = "Message Box";
        def.category = "Dialogue & UI";
        def.icon = ICON_FA_MESSAGE;
        def.header_color = kDialogueColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Text", PinType::String, PinDir::Input},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            
            ctx.Line("GUI.DisplayMessageBox(" + ctx.Expr(node, "Text") + ")");
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "dialogue.yes_no";
        def.title = "Yes/No Question";
        def.category = "Dialogue & UI";
        def.icon = ICON_FA_CIRCLE_QUESTION;
        def.header_color = kDialogueColor;
        def.latent = true;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Text", PinType::String, PinDir::Input},
            {"Yes", PinType::Exec, PinDir::Output},
            {"No", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            
            ctx.Open("if GUI.AskYesNoQuestion(" + ctx.Expr(node, "Text") +
                     ", {}) then");
            ctx.Chain(node, "Yes");
            ctx.Close("else");
            ++ctx.indent;
            ctx.Chain(node, "No");
            ctx.Close("end");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "dialogue.play_movie";
        def.title = "Play Movie";
        def.category = "Dialogue & UI";
        def.icon = ICON_FA_FILM;
        def.header_color = kDialogueColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Movie", PinType::String, PinDir::Input},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            ctx.Line("GUI.PlayMovie(" + ctx.Expr(node, "Movie") + ")");
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "dialogue.play_cutscene";
        def.title = "Play Cutscene";
        def.category = "Dialogue & UI";
        def.icon = ICON_FA_CLAPPERBOARD;
        def.header_color = kDialogueColor;
        def.scope = NodeScope::Entity;
        def.latent = true;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Cutscene", PinType::String, PinDir::Input},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            if (!ctx.entity_scope) {
                ctx.Error(node, "Play Cutscene must run on an entity chain.");
                return;
            }
            ctx.Line("self:PlayCutscene({Cutscene = " +
                     ctx.Expr(node, "Cutscene") + "})");
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
}

}
}
