#include "BlueprintInspector.h"

#include "BlueprintPinWidgets.h"
#include "BlueprintPrerequisites.h"
#include "Quest/Blueprint/BlueprintNodeRegistry.h"

#include "IconsFontAwesome6.h"
#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>

namespace BlueprintUIDetail {

using namespace Quest::Bp;

namespace {

const char* kVariableTypeNames[] = {"Bool", "Number", "String"};
const PinType kVariableTypes[] = {PinType::Bool, PinType::Number,
                                  PinType::String};

int         s_selected_var = -1;
int         s_renaming_var = -1;
bool        s_rename_focus = false;
std::string s_rename_original;

bool draw_value_editor(const BlueprintQuest& quest, Pin& pin) {
    bool changed = false;
    ImGui::PushID(pin.id);
    switch (pin.type) {
        case PinType::Bool:
            changed = ImGui::Checkbox("##val", &pin.value.b);
            break;
        case PinType::Number:
            ImGui::SetNextItemWidth(120.0f);
            changed = ImGui::InputDouble("##val", &pin.value.num, 0.0, 0.0,
                                         "%g");
            break;
        case PinType::String:
        case PinType::TextTag:
            ImGui::SetNextItemWidth(-1.0f);
            changed = ImGui::InputTextMultiline(
                "##val", &pin.value.str,
                ImVec2(-1.0f, ImGui::GetTextLineHeight() * 3.0f));
            break;
        case PinType::EntityDef:
            changed = DrawEntityDefPicker(pin);
            break;
        case PinType::Entity:
        case PinType::Marker:
            changed = DrawEntityEditor(quest, pin);
            break;
        case PinType::Level:
            changed = DrawLevelPicker(pin);
            break;
        case PinType::Item:
            changed = DrawItemPicker(pin);
            break;
        case PinType::Vector3: {
            changed = ImGui::InputFloat3("##val", pin.value.vec);
            break;
        }
        default:
            ImGui::TextDisabled("(no editable default)");
            break;
    }
    ImGui::PopID();
    return changed;
}

}

void DrawNodeInspector(BlueprintQuest& quest, int& node_id) {
    Node* node = quest.NodeById(node_id);
    if (!node) {
        node_id = 0;
        return;
    }
    const NodeDef* def = Registry::Find(node->type);
    if (!def) return;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.84f, 0.0f, 1.0f));
    ImGui::Text("%s", def->title.c_str());
    ImGui::PopStyleColor();
    ImGui::TextDisabled("%s", def->category.c_str());
    ImGui::Separator();

    bool changed = false;

    
    auto prop_combo = [&](const char* label,
                          const std::vector<std::string>& options) {
        ImGui::TextUnformatted(label);
        ImGui::SetNextItemWidth(-1.0f);
        const std::string preview =
            node->prop.empty() ? "(choose...)" : node->prop;
        if (ImGui::BeginCombo("##bp_prop", preview.c_str())) {
            for (const std::string& opt : options) {
                if (ImGui::Selectable(opt.c_str(), opt == node->prop)) {
                    node->prop = opt;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::Spacing();
    };
    if (node->type == "var.get" || node->type == "var.set") {
        std::vector<std::string> names;
        for (const auto& var : quest.variables) names.push_back(var.name);
        prop_combo("Variable", names);
        if (changed) Registry::SyncVariableNode(quest, *node);
        if (quest.variables.empty()) {
            ImGui::TextDisabled("(add variables in the quest panel below)");
        }
    } else if (node->type == "data.compare") {
        prop_combo("Operator", {"==", "~=", "<", "<=", ">", ">="});
    } else if (node->type == "data.arithmetic") {
        prop_combo("Operator", {"+", "-", "*", "/"});
    } else if (node->type == "inv.give_experience") {
        prop_combo("Experience type",
                   {"GENERAL", "STRENGTH", "SKILL", "WILL"});
    } else if (node->type == "inv.modify_stat") {
        prop_combo("Stat", {"Renown", "Morality", "Purity"});
    } else if (node->type == "entity.spawn") {
        prop_combo("Spawn kind", {"creature", "object", "effect"});
    } else if (node->type == "event.message") {
        prop_combo("Message type",
                   {"LEVEL_LOADED", "HIT", "KILLED", "OBJECT_DESTROYED",
                    "INTERACTED_WITH", "INFOBOX",
                    "GENERIC_A_BUTTON_PRESSED", "TRIGGER"});
    } else if (node->type == "game.get_experience") {
        prop_combo("Experience type",
                   {"GENERAL", "STRENGTH", "SKILL", "WILL"});
    } else if (node->type.rfind("prereq.", 0) == 0) {
        changed |= DrawPrerequisiteConfig(node->prereq);
        ImGui::Spacing();
    } else if (node->type == "util.lua_snippet" ||
               node->type == "util.lua_expression") {
        ImGui::TextUnformatted(node->type == "util.lua_snippet"
                                   ? "Lua code ($1..$4 = input pins)"
                                   : "Lua expression ($1..$2 = input pins)");
        ImGui::SetNextItemWidth(-1.0f);
        changed |= ImGui::InputTextMultiline(
            "##bp_lua_code", &node->prop,
            ImVec2(-1.0f, ImGui::GetTextLineHeight() * 6.0f));
        ImGui::Spacing();
    }
    for (Pin& pin : node->pins) {
        if (pin.dir != PinDir::Input || pin.type == PinType::Exec) continue;
        if (quest.LinkInto(pin.id)) {
            ImGui::TextDisabled("%s: linked", pin.name.c_str());
            continue;
        }
        ImGui::TextUnformatted(pin.name.empty() ? "Value" : pin.name.c_str());
        changed |= draw_value_editor(quest, pin);
        ImGui::Spacing();
    }

    if (def->dynamic_outputs) {
        if (ImGui::SmallButton("+ Add output pin")) {
            Registry::AddDynamicExecOutput(quest, *node);
        }
        ImGui::Spacing();
    }

    ImGui::TextUnformatted("Comment");
    ImGui::SetNextItemWidth(-1.0f);
    changed |= ImGui::InputText("##bp_comment", &node->comment);

    if (changed) quest.Touch();
}

void DrawQuestInspector(BlueprintQuest& quest) {
    ImGui::TextDisabled("Quest");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputTextWithHint("##bp_quest_title", "Display title",
                                 &quest.quest_title)) {
        quest.Touch();
    }
    ImGui::Text("%zu nodes, %zu links", quest.nodes.size(),
                quest.links.size());
    ImGui::Separator();

    
    
    
    
    ImGui::TextDisabled("Variables");
    ImGui::SameLine();
    const float add_w = ImGui::CalcTextSize("+").x +
                        ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),
                                  ImGui::GetContentRegionMax().x - add_w));
    if (ImGui::SmallButton("+##bp_add_var")) {
        Variable var;
        var.name = "NewVar" + std::to_string(quest.variables.size() + 1);
        quest.variables.push_back(std::move(var));
        s_renaming_var = (int)quest.variables.size() - 1;
        s_selected_var = s_renaming_var;
        quest.Touch();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add variable");

    int remove_index = -1;
    for (size_t i = 0; i < quest.variables.size(); ++i) {
        Variable& var = quest.variables[i];
        ImGui::PushID((int)i);

        const uint32_t argb = PinTypeColor(var.type);
        const ImU32 dot = IM_COL32((argb >> 16) & 0xFF, (argb >> 8) & 0xFF,
                                   argb & 0xFF, 255);
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        const float line = ImGui::GetTextLineHeight();
        ImGui::GetWindowDrawList()->AddCircleFilled(
            ImVec2(cursor.x + line * 0.5f, cursor.y + line * 0.55f),
            line * 0.28f, dot);
        ImGui::Dummy(ImVec2(line, line));
        ImGui::SameLine();

        if ((int)i == s_renaming_var) {
            ImGui::SetNextItemWidth(-1.0f);
            if (s_rename_focus) {
                ImGui::SetKeyboardFocusHere();
                s_rename_focus = false;
            }
            const bool committed = ImGui::InputText(
                "##rename", &var.name,
                ImGuiInputTextFlags_EnterReturnsTrue);
            if (committed || ImGui::IsItemDeactivated()) {
                s_renaming_var = -1;
                
                if (var.name != s_rename_original) {
                    for (Node& n : quest.nodes) {
                        if ((n.type == "var.get" || n.type == "var.set") &&
                            n.prop == s_rename_original) {
                            n.prop = var.name;
                        }
                    }
                }
                quest.Touch();
            }
        } else {
            if (ImGui::Selectable(var.name.c_str(),
                                  s_selected_var == (int)i)) {
                s_selected_var = (int)i;
            }
            if (ImGui::IsItemHovered() &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                s_renaming_var = (int)i;
                s_rename_original = var.name;
                s_rename_focus = true;
            }
            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("BP_VARIABLE", var.name.c_str(),
                                          var.name.size() + 1);
                ImGui::TextUnformatted(var.name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginPopupContextItem("##var_ctx")) {
                if (ImGui::MenuItem("Rename")) {
                    s_renaming_var = (int)i;
                    s_rename_original = var.name;
                    s_rename_focus = true;
                }
                if (ImGui::MenuItem("Delete")) remove_index = (int)i;
                ImGui::EndPopup();
            }
        }
        ImGui::PopID();
    }
    if (remove_index >= 0) {
        if (s_selected_var == remove_index) s_selected_var = -1;
        quest.variables.erase(quest.variables.begin() + remove_index);
        quest.Touch();
    }

    
    if (s_selected_var >= 0 &&
        s_selected_var < (int)quest.variables.size()) {
        Variable& var = quest.variables[(size_t)s_selected_var];
        ImGui::Spacing();
        ImGui::TextDisabled("Variable type");
        int type_index = 0;
        for (int t = 0; t < 3; ++t) {
            if (kVariableTypes[t] == var.type) type_index = t;
        }
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("##bp_var_type", &type_index, kVariableTypeNames,
                         3)) {
            var.type = kVariableTypes[type_index];
            for (Node& n : quest.nodes) {
                Registry::SyncVariableNode(quest, n);
            }
            quest.Touch();
        }
        ImGui::TextDisabled("Default value");
        switch (var.type) {
            case PinType::Bool:
                if (ImGui::Checkbox("##bp_var_def", &var.def.b)) {
                    quest.Touch();
                }
                break;
            case PinType::Number:
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::InputDouble("##bp_var_def", &var.def.num, 0.0,
                                       0.0, "%g")) {
                    quest.Touch();
                }
                break;
            default:
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::InputText("##bp_var_def", &var.def.str)) {
                    quest.Touch();
                }
                break;
        }
    }
}

}
