#include "BlueprintNodeRegistry.h"

#include "BlueprintCompiler.h"

#include "IconsFontAwesome6.h"

namespace Quest {
namespace Bp {

namespace {
constexpr uint32_t kWorldColor = 0xFF2E5A2A;
}

void RegisterWorldNodes() {
    {
        NodeDef def;
        def.type = "world.load_level";
        def.title = "Load Level";
        def.category = "World & Camera";
        def.icon = ICON_FA_MAP_LOCATION_DOT;
        def.header_color = kWorldColor;
        def.latent = true;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Level", PinType::Level, PinDir::Input},
            {"Start point", PinType::String, PinDir::Input, "", true},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            ctx.Line("Debug.LoadLevel(\"Albion\", " +
                     ctx.Expr(node, "Level") + ", " +
                     ctx.Expr(node, "Start point") + ")");
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "world.is_level_loaded";
        def.title = "Is Level Loaded";
        def.category = "World & Camera";
        def.icon = ICON_FA_LAYER_GROUP;
        def.header_color = kWorldColor;
        def.pure = true;
        def.pins = {
            {"Level", PinType::Level, PinDir::Input},
            {"Loaded", PinType::Bool, PinDir::Output},
        };
        def.emit_expr = [](EmitContext& ctx, const Node& node, const Pin&)
            -> std::string {
            return "IsLevelLoaded(" + ctx.Expr(node, "Level") + ")";
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "world.set_layer";
        def.title = "Activate Layer";
        def.category = "World & Camera";
        def.icon = ICON_FA_LAYER_GROUP;
        def.header_color = kWorldColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Layer", PinType::String, PinDir::Input},
            {"Active", PinType::Bool, PinDir::Input, "true"},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            const Pin* active = node.FindPin("Active", PinDir::Input);
            const char* fn = (active && !active->value.b)
                                 ? "DeactivateLayer" : "ActivateLayer";
            ctx.Line("Layers." + std::string(fn) + "(" +
                     ctx.Expr(node, "Layer") + ")");
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "world.set_door_open";
        def.title = "Set Door Open";
        def.category = "World & Camera";
        def.icon = ICON_FA_DOOR_OPEN;
        def.header_color = kWorldColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Door", PinType::Entity, PinDir::Input},
            {"Open", PinType::Bool, PinDir::Input, "true"},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            ctx.Line("Door.SetOpen(" + ctx.Expr(node, "Door") + ", " +
                     ctx.Expr(node, "Open") + ", false)");
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "world.set_door_locked";
        def.title = "Set Door Locked";
        def.category = "World & Camera";
        def.icon = ICON_FA_LOCK;
        def.header_color = kWorldColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Door", PinType::Entity, PinDir::Input},
            {"Locked", PinType::Bool, PinDir::Input, "true"},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            ctx.Line("Door.SetLocked(" + ctx.Expr(node, "Door") + ", " +
                     ctx.Expr(node, "Locked") + ")");
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "world.set_time";
        def.title = "Set Time Of Day";
        def.category = "World & Camera";
        def.icon = ICON_FA_SUN;
        def.header_color = kWorldColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Hour", PinType::Number, PinDir::Input, "12"},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            ctx.Line("Timing.SetTimeOfDay(" + ctx.Expr(node, "Hour") + ")");
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "world.set_day_speed";
        def.title = "Set Day Speed";
        def.category = "World & Camera";
        def.icon = ICON_FA_GAUGE_HIGH;
        def.header_color = kWorldColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Day seconds", PinType::Number, PinDir::Input, "1440"},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            ctx.Line("Timing.SetDaySpeed(" + ctx.Expr(node, "Day seconds") +
                     ")");
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "world.camera_look_at";
        def.title = "Camera Look At";
        def.category = "World & Camera";
        def.icon = ICON_FA_VIDEO;
        def.header_color = kWorldColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Target", PinType::Entity, PinDir::Input},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            ctx.Open("self:SetLookAtCamera({");
            ctx.Line("target_position = GetPositionOfEntity(" +
                     ctx.Expr(node, "Target") + ")");
            ctx.Close("})");
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "world.reset_camera";
        def.title = "Reset Camera";
        def.category = "World & Camera";
        def.icon = ICON_FA_CAMERA_ROTATE;
        def.header_color = kWorldColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            ctx.Line("self:SetDefaultCamera()");
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
}

}
}
