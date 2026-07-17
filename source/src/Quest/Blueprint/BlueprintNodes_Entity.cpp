#include "BlueprintNodeRegistry.h"

#include "BlueprintCompiler.h"

#include "IconsFontAwesome6.h"

namespace Quest {
namespace Bp {

namespace {
constexpr uint32_t kEntityColor = 0xFF1F4468;


void store_output(EmitContext& ctx, const Node& node, const char* pin_name,
                  const std::string& expr) {
    const Pin* out = node.FindPin(pin_name, PinDir::Output);
    if (out) {
        ctx.Line("self.Out_" + std::to_string(out->id) + " = " + expr);
    }
}
}

void RegisterEntityNodes() {
    {
        NodeDef def;
        def.type = "entity.get_dog";
        def.title = "Get Dog";
        def.category = "Entity";
        def.icon = ICON_FA_DOG;
        def.header_color = kEntityColor;
        def.pure = true;
        def.pins = {
            {"Dog", PinType::Entity, PinDir::Output},
        };
        def.emit_expr = [](EmitContext&, const Node&, const Pin&)
            -> std::string { return "GetDog()"; };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "entity.is_alive";
        def.title = "Is Alive";
        def.category = "Entity";
        def.icon = ICON_FA_HEART_PULSE;
        def.header_color = kEntityColor;
        def.pure = true;
        def.pins = {
            {"Entity", PinType::Entity, PinDir::Input},
            {"Alive", PinType::Bool, PinDir::Output},
        };
        def.emit_expr = [](EmitContext& ctx, const Node& node, const Pin&)
            -> std::string {
            const std::string e = ctx.Expr(node, "Entity");
            return "((" + e + ") ~= nil and (" + e + "):IsAlive())";
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "entity.get_position";
        def.title = "Get Position";
        def.category = "Entity";
        def.icon = ICON_FA_LOCATION_DOT;
        def.header_color = kEntityColor;
        def.pure = true;
        def.pins = {
            {"Entity", PinType::Entity, PinDir::Input},
            {"Position", PinType::Vector3, PinDir::Output},
        };
        def.emit_expr = [](EmitContext& ctx, const Node& node, const Pin&)
            -> std::string {
            return "GetPositionOfEntity(" + ctx.Expr(node, "Entity") + ")";
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "entity.is_within_distance";
        def.title = "Is Within Distance";
        def.category = "Entity";
        def.icon = ICON_FA_RULER;
        def.header_color = kEntityColor;
        def.pure = true;
        def.pins = {
            {"A", PinType::Entity, PinDir::Input},
            {"B", PinType::Entity, PinDir::Input},
            {"Distance", PinType::Number, PinDir::Input, "3"},
            {"Within", PinType::Bool, PinDir::Output},
        };
        def.emit_expr = [](EmitContext& ctx, const Node& node, const Pin&)
            -> std::string {
            return "IsDistanceBetweenThingsUnder(" + ctx.Expr(node, "A") +
                   ", " + ctx.Expr(node, "B") + ", " +
                   ctx.Expr(node, "Distance") + ")";
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "entity.wait_for";
        def.title = "Wait For Entity";
        def.category = "Entity";
        def.icon = ICON_FA_USER_CLOCK;
        def.header_color = kEntityColor;
        def.latent = true;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Name", PinType::Entity, PinDir::Input},
            {"", PinType::Exec, PinDir::Output},
            {"Entity", PinType::Entity, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            const Pin* name = node.FindPin("Name", PinDir::Input);
            if (ctx.quest->LinkInto(name->id)) {
                ctx.Error(node, "Wait For Entity resolves by name - set the "
                                "entity on the node instead of wiring it.");
                return;
            }
            const std::string quoted =
                LuaQuote(name->value.world.entity_name);
            const std::string local = "found_" + std::to_string(node.id);
            ctx.Line("local " + local + " = self:GetEntityWithName(" +
                     quoted + ")");
            ctx.Open("while not " + local + " do");
            ctx.Line("coroutine.yield()");
            ctx.Line(local + " = self:GetEntityWithName(" + quoted + ")");
            ctx.Close("end");
            store_output(ctx, node, "Entity", local);
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "entity.spawn";
        def.title = "Spawn Entity";
        def.category = "Entity";
        def.icon = ICON_FA_WAND_MAGIC_SPARKLES;
        def.header_color = kEntityColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Definition", PinType::EntityDef, PinDir::Input},
            {"Position", PinType::Vector3, PinDir::Input},
            {"", PinType::Exec, PinDir::Output},
            {"Entity", PinType::Entity, PinDir::Output},
        };
        
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            const std::string kind =
                node.prop.empty() ? "creature" : node.prop;
            const std::string local = "spawned_" + std::to_string(node.id);
            ctx.Line("local " + local + " = Debug.CreateEntityAtPosition(" +
                     ctx.Expr(node, "Definition") + ", " + LuaQuote(kind) +
                     ", " + ctx.Expr(node, "Position") + ")");
            store_output(ctx, node, "Entity", local);
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "entity.destroy";
        def.title = "Destroy Entity";
        def.category = "Entity";
        def.icon = ICON_FA_TRASH;
        def.header_color = kEntityColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Entity", PinType::Entity, PinDir::Input},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            const std::string local = "victim_" + std::to_string(node.id);
            ctx.Line("local " + local + " = " + ctx.Expr(node, "Entity"));
            ctx.Open("if " + local + " then");
            ctx.Line(local + ":Destroy()");
            ctx.Close("end");
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "entity.teleport_to_marker";
        def.title = "Teleport To Marker";
        def.category = "Entity";
        def.icon = ICON_FA_BOLT_LIGHTNING;
        def.header_color = kEntityColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Entity", PinType::Entity, PinDir::Input},
            {"Marker", PinType::Marker, PinDir::Input},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            const Pin* marker = node.FindPin("Marker", PinDir::Input);
            ctx.Line("self:MoveAndRotateEntityToMarkerNamed(" +
                     ctx.Expr(node, "Entity") + ", " +
                     LuaQuote(marker->value.world.entity_name) + ")");
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "entity.play_animation";
        def.title = "Play Animation";
        def.category = "Entity";
        def.icon = ICON_FA_PERSON_RUNNING;
        def.header_color = kEntityColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Entity", PinType::Entity, PinDir::Input},
            {"Animation", PinType::String, PinDir::Input},
            {"Hold last frame", PinType::Bool, PinDir::Input, "false"},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            const Pin* hold = node.FindPin("Hold last frame", PinDir::Input);
            const char* type = (hold && hold->value.b)
                                   ? "PLAY_ANIMATION_HOLD_LAST_FRAME"
                                   : "PLAY_ANIMATION";
            ctx.Open("Action.SetCurrentAction(" + ctx.Expr(node, "Entity") +
                     ", {");
            ctx.Line("Type = EScriptableAction." + std::string(type) + ",");
            ctx.Line("Anim = " + ctx.Expr(node, "Animation"));
            ctx.Close("})");
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "entity.attack";
        def.title = "Attack";
        def.category = "Entity";
        def.icon = ICON_FA_HAND_FIST;
        def.header_color = kEntityColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Attacker", PinType::Entity, PinDir::Input},
            {"Target", PinType::Entity, PinDir::Input},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            ctx.Line("Attack(" + ctx.Expr(node, "Attacker") + ", " +
                     ctx.Expr(node, "Target") + ")");
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "entity.kill";
        def.title = "Kill Entity";
        def.category = "Entity";
        def.icon = ICON_FA_SKULL_CROSSBONES;
        def.header_color = kEntityColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Entity", PinType::Entity, PinDir::Input},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            ctx.Line("Kill(" + ctx.Expr(node, "Entity") + ")");
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "entity.set_invulnerable";
        def.title = "Set Invulnerable";
        def.category = "Entity";
        def.icon = ICON_FA_SHIELD;
        def.header_color = kEntityColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Entity", PinType::Entity, PinDir::Input},
            {"Invulnerable", PinType::Bool, PinDir::Input, "true"},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            ctx.Line("Health.SetAsInvulnerable(" + ctx.Expr(node, "Entity") +
                     ", " + ctx.Expr(node, "Invulnerable") + ")");
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "entity.follow";
        def.title = "Follow";
        def.category = "Entity";
        def.icon = ICON_FA_PEOPLE_ARROWS;
        def.header_color = kEntityColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Follower", PinType::Entity, PinDir::Input},
            {"Target", PinType::Entity, PinDir::Input},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            ctx.Line("Follow(" + ctx.Expr(node, "Follower") + ", " +
                     ctx.Expr(node, "Target") + ")");
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "entity.stand_still";
        def.title = "Make NPC Stand Still";
        def.category = "Entity";
        def.header_color = kEntityColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Entity", PinType::Entity, PinDir::Input},
            {"Invulnerable", PinType::Bool, PinDir::Input, "true"},
            {"Block pushing", PinType::Bool, PinDir::Input, "true"},
            {"", PinType::Exec, PinDir::Output},
        };
        
        
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            const Pin* invuln = node.FindPin("Invulnerable", PinDir::Input);
            const Pin* block = node.FindPin("Block pushing", PinDir::Input);
            const std::string local = "npc_" + std::to_string(node.id);
            ctx.Line("local " + local + " = " + ctx.Expr(node, "Entity"));
            ctx.Open("if " + local + " then");
            ctx.Line("ScriptFunction.DisableSimBehaviours(" + local + ")");
            ctx.Line("Navigation.StopMoving(" + local + ")");
            if (!block || block->value.b) {
                ctx.Line("PhysicsCharacter.SetAsPushableByHero(" + local +
                         ", false)");
            }
            if (!invuln || invuln->value.b) {
                ctx.Line("Health.SetAsInvulnerable(" + local + ", true)");
            }
            ctx.Line("OpinionReaction.SetRespondToExpressions(" + local +
                     ", false)");
            ctx.Close("end");
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "entity.stop_moving";
        def.title = "Stop Moving";
        def.category = "Entity";
        def.header_color = kEntityColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Entity", PinType::Entity, PinDir::Input},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            ctx.Line("Navigation.StopMoving(" + ctx.Expr(node, "Entity") +
                     ")");
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "entity.walk_to_marker";
        def.title = "Walk To Marker";
        def.category = "Entity";
        def.header_color = kEntityColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Entity", PinType::Entity, PinDir::Input},
            {"Marker", PinType::Marker, PinDir::Input},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            const Pin* marker = node.FindPin("Marker", PinDir::Input);
            ctx.Line("MoveToMarker(" + ctx.Expr(node, "Entity") + ", " +
                     LuaQuote(marker->value.world.entity_name) + ")");
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
}

}
}
