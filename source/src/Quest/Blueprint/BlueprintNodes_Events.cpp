#include "BlueprintNodeRegistry.h"

#include "BlueprintCompiler.h"

#include "IconsFontAwesome6.h"

namespace Quest {
namespace Bp {

namespace {

constexpr uint32_t kEventColor = 0xFF7A2B2B;



template <typename WaitFn, typename BodyFn>
void emit_repeatable_event(EmitContext& ctx, const Node& node,
                           WaitFn&& wait, BodyFn&& body) {
    const Pin* repeat = node.FindPin("Repeat", PinDir::Input);
    const bool repeats = repeat && (ctx.quest->LinkInto(repeat->id)
                                        ? true  
                                        : repeat->value.b);
    if (repeats) {
        ctx.Open("while true do");
        wait();
        body();
        ctx.Line("coroutine.yield()");
        ctx.Close("end");
    } else {
        wait();
        body();
    }
}

}





void RegisterEventNodes() {
    {
        NodeDef def;
        def.type = "event.quest_start";
        def.title = "On Quest Start";
        def.category = "Events";
        def.icon = ICON_FA_PLAY;
        def.header_color = kEventColor;
        def.scope = NodeScope::Quest;
        def.is_event = true;
        def.pins = {
            {"Prerequisites", PinType::Prereq, PinDir::Input, "", true},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "event.hero_near";
        def.title = "On Hero Near Entity";
        def.category = "Events";
        def.icon = ICON_FA_PERSON_RAYS;
        def.header_color = kEventColor;
        def.scope = NodeScope::Entity;
        def.is_event = true;
        def.latent = true;
        def.pins = {
            {"Entity", PinType::Entity, PinDir::Input},
            {"Radius", PinType::Number, PinDir::Input, "3"},
            {"Repeat", PinType::Bool, PinDir::Input, "true"},
            {"Triggered", PinType::Exec, PinDir::Output},
            {"Entity", PinType::Entity, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            const std::string radius = ctx.Expr(node, "Radius");
            const std::string near =
                "IsDistanceBetweenThingsUnder(self.Entity, "
                "QuestManager.HeroEntity, " + radius + ")";
            const Pin* out = node.FindPin("Entity", PinDir::Output);
            emit_repeatable_event(
                ctx, node,
                [&] {
                    ctx.Open("while not (" + near + ") do");
                    ctx.Line("coroutine.yield()");
                    ctx.Close("end");
                    
                    
                    
                    ctx.Line("self.BpInteractionRadius = " + radius);
                    if (out) {
                        ctx.Line("self.Out_" + std::to_string(out->id) +
                                 " = self.Entity");
                    }
                },
                [&] {
                    ctx.Chain(node, "Triggered");
                    
                    ctx.Open("while " + near + " do");
                    ctx.Line("coroutine.yield()");
                    ctx.Close("end");
                });
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "event.interacted";
        def.title = "On Interacted (hold A)";
        def.category = "Events";
        def.icon = ICON_FA_HAND_POINTER;
        def.header_color = kEventColor;
        def.scope = NodeScope::Entity;
        def.is_event = true;
        def.latent = true;
        def.pins = {
            {"Entity", PinType::Entity, PinDir::Input},
            {"Repeat", PinType::Bool, PinDir::Input, "true"},
            {"", PinType::Exec, PinDir::Output},
            {"Entity", PinType::Entity, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            const Pin* out = node.FindPin("Entity", PinDir::Output);
            emit_repeatable_event(
                ctx, node,
                [&] {
                    
                    
                    ctx.Open("while not self.Interacted do");
                    ctx.Line("coroutine.yield()");
                    ctx.Close("end");
                    if (out) {
                        ctx.Line("self.Out_" + std::to_string(out->id) +
                                 " = self.Entity");
                    }
                },
                [&] { ctx.Chain(node, ""); });
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "event.hit";
        def.title = "On Hit";
        def.category = "Events";
        def.icon = ICON_FA_BURST;
        def.header_color = kEventColor;
        def.scope = NodeScope::Entity;
        def.is_event = true;
        def.latent = true;
        def.pins = {
            {"Entity", PinType::Entity, PinDir::Input},
            {"Only by hero", PinType::Bool, PinDir::Input, "false"},
            {"Repeat", PinType::Bool, PinDir::Input, "true"},
            {"", PinType::Exec, PinDir::Output},
            {"Entity", PinType::Entity, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            const Pin* only_hero = node.FindPin("Only by hero", PinDir::Input);
            const bool hero_only = only_hero && only_hero->value.b;
            const std::string flag = hero_only ? "self.HitByPlayer"
                                               : "self.Hit";
            const Pin* out = node.FindPin("Entity", PinDir::Output);
            emit_repeatable_event(
                ctx, node,
                [&] {
                    ctx.Open("while not " + flag + " do");
                    ctx.Line("coroutine.yield()");
                    ctx.Close("end");
                    if (out) {
                        ctx.Line("self.Out_" + std::to_string(out->id) +
                                 " = self.Entity");
                    }
                },
                [&] { ctx.Chain(node, ""); });
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "event.level_loaded";
        def.title = "On Level Loaded";
        def.category = "Events";
        def.icon = ICON_FA_MAP;
        def.header_color = kEventColor;
        def.scope = NodeScope::Quest;
        def.is_event = true;
        def.latent = true;
        def.pins = {
            {"Level", PinType::Level, PinDir::Input},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            ctx.Open("while not IsLevelLoaded(" + ctx.Expr(node, "Level") +
                     ") do");
            ctx.Line("coroutine.yield()");
            ctx.Close("end");
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "event.timer";
        def.title = "On Timer";
        def.category = "Events";
        def.icon = ICON_FA_STOPWATCH;
        def.header_color = kEventColor;
        def.scope = NodeScope::Quest;
        def.is_event = true;
        def.latent = true;
        def.pins = {
            {"Seconds", PinType::Number, PinDir::Input, "30"},
            {"Repeat", PinType::Bool, PinDir::Input, "false"},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            emit_repeatable_event(
                ctx, node,
                [&] {
                    ctx.Line("self:WaitForTimeInSeconds(" +
                             ctx.Expr(node, "Seconds") + ")");
                },
                [&] { ctx.Chain(node, ""); });
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "event.quest_giver";
        def.title = "Quest Giver (hold A to accept)";
        def.category = "Events";
        def.header_color = kEventColor;
        def.scope = NodeScope::Entity;
        def.is_event = true;
        def.latent = true;
        def.pins = {
            {"Entity", PinType::Entity, PinDir::Input},
            {"Offer text", PinType::String, PinDir::Input},
            {"Stand still", PinType::Bool, PinDir::Input, "true"},
            {"Accepted", PinType::Exec, PinDir::Output},
            {"Giver", PinType::Entity, PinDir::Output},
        };
        
        
        
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            if (!ctx.entity_scope) {
                ctx.Error(node, "Quest Giver runs on an entity thread.");
                return;
            }
            const Pin* stand = node.FindPin("Stand still", PinDir::Input);
            if (!stand || stand->value.b) {
                ctx.Line("ScriptFunction.DisableSimBehaviours(self.Entity)");
                ctx.Line("Navigation.StopMoving(self.Entity)");
                ctx.Line("PhysicsCharacter.SetAsPushableByHero(self.Entity, "
                         "false)");
                ctx.Line("Health.SetAsInvulnerable(self.Entity, true)");
                ctx.Line("OpinionReaction.SetRespondToExpressions("
                         "self.Entity, false)");
            }
            ctx.Line("QuestTracker.SetQuestGiver(QuestManager.HeroEntity, "
                     "self.ParentQuest.QuestName, self.Entity)");
            const Pin* giver = node.FindPin("Giver", PinDir::Output);
            if (giver) {
                ctx.Line("self.Out_" + std::to_string(giver->id) +
                         " = self.Entity");
            }
            const Pin* text = node.FindPin("Offer text", PinDir::Input);
            const std::string tag =
                ctx.TextTag(node, *text, text->value.str);
            ctx.Line("local accepted_" + std::to_string(node.id) +
                     " = self:ShowToasterAcceptBoxWithDialogueUntilCondition(");
            ctx.Line("  " + LuaQuote(tag) + ", " +
                     LuaQuote("Quest_" + ctx.quest_class) + ",");
            ctx.Line("  {QuestName = self.ParentQuest.QuestName})");
            ctx.Open("if accepted_" + std::to_string(node.id) + " then");
            ctx.Chain(node, "Accepted");
            ctx.Close("end");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "event.message";
        def.title = "On Message";
        def.category = "Events";
        def.icon = ICON_FA_ENVELOPE;
        def.header_color = kEventColor;
        def.scope = NodeScope::Quest;
        def.is_event = true;
        def.latent = true;
        def.pins = {
            {"Repeat", PinType::Bool, PinDir::Input, "true"},
            {"", PinType::Exec, PinDir::Output},
        };
        
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            if (node.prop.empty()) {
                ctx.Error(node, "Pick a message type for On Message "
                                "(inspector).");
                return;
            }
            const std::string field = ctx.StateField(node, "LastMsg", "0");
            emit_repeatable_event(
                ctx, node,
                [&] {
                    ctx.Open("while true do");
                    ctx.Line("local posted, msg = "
                             "MessageEvents.IsMessagePosted("
                             "EMessageEventType.MESSAGE_EVENT_" +
                             node.prop + ", " + field + ")");
                    ctx.Open("if posted then");
                    ctx.Line(field + " = msg:GetID()");
                    ctx.Line("break");
                    ctx.Close("end");
                    ctx.Line("coroutine.yield()");
                    ctx.Close("end");
                },
                [&] { ctx.Chain(node, ""); });
        };
        Registry::Register(std::move(def));
    }
}

}
}
