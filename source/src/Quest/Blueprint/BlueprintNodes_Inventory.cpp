#include "BlueprintNodeRegistry.h"

#include "BlueprintCompiler.h"

#include "IconsFontAwesome6.h"

namespace Quest {
namespace Bp {

namespace {
constexpr uint32_t kInventoryColor = 0xFF7A4A16;
}

void RegisterInventoryNodes() {
    {
        NodeDef def;
        def.type = "inv.modify_gold";
        def.title = "Modify Gold";
        def.category = "Inventory & Rewards";
        def.icon = ICON_FA_COINS;
        def.header_color = kInventoryColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Amount", PinType::Number, PinDir::Input, "100"},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            ctx.Line("Money.Modify(QuestManager.HeroEntity, " +
                     ctx.Expr(node, "Amount") + ")");
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "inv.add_item";
        def.title = "Add Item";
        def.category = "Inventory & Rewards";
        def.icon = ICON_FA_PLUS;
        def.header_color = kInventoryColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Item", PinType::Item, PinDir::Input},
            {"Count", PinType::Number, PinDir::Input, "1"},
            {"Show UI", PinType::Bool, PinDir::Input, "true"},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            const Pin* show = node.FindPin("Show UI", PinDir::Input);
            ctx.Open("for _ = 1, " + ctx.Expr(node, "Count") + " do");
            ctx.Line("local given = Inventory.AddItemOfType("
                     "QuestManager.HeroEntity, " + ctx.Expr(node, "Item") +
                     ")");
            if (show && show->value.b) {
                ctx.Line("GUI.DisplayReceivedItem(given)");
            }
            ctx.Close("end");
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "inv.remove_item";
        def.title = "Remove Item";
        def.category = "Inventory & Rewards";
        def.icon = ICON_FA_MINUS;
        def.header_color = kInventoryColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Item", PinType::Item, PinDir::Input},
            {"All", PinType::Bool, PinDir::Input, "true"},
            {"", PinType::Exec, PinDir::Output},
        };
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            const Pin* all = node.FindPin("All", PinDir::Input);
            if (all && all->value.b) {
                ctx.Line("Inventory.RemoveAllItemsOfType("
                         "QuestManager.HeroEntity, " +
                         ctx.Expr(node, "Item") + ")");
            } else {
                ctx.Line("Inventory.RemoveItemOfType("
                         "QuestManager.HeroEntity, " +
                         ctx.Expr(node, "Item") + ")");
            }
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "inv.item_count";
        def.title = "Item Count";
        def.category = "Inventory & Rewards";
        def.icon = ICON_FA_BOXES_STACKED;
        def.header_color = kInventoryColor;
        def.pure = true;
        def.pins = {
            {"Item", PinType::Item, PinDir::Input},
            {"Count", PinType::Number, PinDir::Output},
        };
        def.emit_expr = [](EmitContext& ctx, const Node& node, const Pin&)
            -> std::string {
            return "Inventory.GetNumberOfItemsOfType("
                   "QuestManager.HeroEntity, " + ctx.Expr(node, "Item") +
                   ")";
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "inv.has_item";
        def.title = "Has Item";
        def.category = "Inventory & Rewards";
        def.icon = ICON_FA_BOX_OPEN;
        def.header_color = kInventoryColor;
        def.pure = true;
        def.pins = {
            {"Item", PinType::Item, PinDir::Input},
            {"Count", PinType::Number, PinDir::Input, "1"},
            {"Result", PinType::Bool, PinDir::Output},
        };
        def.emit_expr = [](EmitContext& ctx, const Node& node, const Pin&)
            -> std::string {
            return "(Inventory.GetNumberOfItemsOfType("
                   "QuestManager.HeroEntity, " + ctx.Expr(node, "Item") +
                   ") >= " + ctx.Expr(node, "Count") + ")";
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "inv.give_experience";
        def.title = "Give Experience";
        def.category = "Inventory & Rewards";
        def.icon = ICON_FA_ARROW_TREND_UP;
        def.header_color = kInventoryColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Amount", PinType::Number, PinDir::Input, "100"},
            {"", PinType::Exec, PinDir::Output},
        };
        
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            const std::string kind =
                node.prop.empty() ? "GENERAL" : node.prop;
            ctx.Line("Experience.Modify(QuestManager.HeroEntity, "
                     "EExperienceType.EXPERIENCE_" + kind + ", " +
                     ctx.Expr(node, "Amount") + ", false)");
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
    {
        NodeDef def;
        def.type = "inv.modify_stat";
        def.title = "Modify Stat";
        def.category = "Inventory & Rewards";
        def.icon = ICON_FA_SCALE_BALANCED;
        def.header_color = kInventoryColor;
        def.pins = {
            {"", PinType::Exec, PinDir::Input},
            {"Amount", PinType::Number, PinDir::Input, "10"},
            {"", PinType::Exec, PinDir::Output},
        };
        
        def.emit = [](EmitContext& ctx, const Node& node, const Pin&) {
            const std::string stat =
                node.prop.empty() ? "Renown" : node.prop;
            ctx.Line("Stats.Modify" + stat + "(QuestManager.HeroEntity, " +
                     ctx.Expr(node, "Amount") + ")");
            ctx.Chain(node, "");
        };
        Registry::Register(std::move(def));
    }
}

}
}
