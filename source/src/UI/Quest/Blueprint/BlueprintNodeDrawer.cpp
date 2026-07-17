#include "BlueprintNodeDrawer.h"

#include "Quest/Blueprint/BlueprintNodeRegistry.h"
#include "vendor/widgets.h"

#include "imgui_stdlib.h"

#include <algorithm>
#include <string>

namespace ed = ax::NodeEditor;

namespace BlueprintUIDetail {

using namespace Quest::Bp;

namespace {

constexpr float kPinIconSize = 22.0f;
constexpr float kRowGap = 6.0f;
constexpr float kMinContentWidth = 170.0f;
constexpr float kMaxContentWidth = 420.0f;

ax::Drawing::IconType pin_icon_type(PinType type) {
    switch (type) {
        case PinType::Exec:      return ax::Drawing::IconType::Flow;
        case PinType::EntityDef: return ax::Drawing::IconType::Diamond;
        case PinType::Prereq:    return ax::Drawing::IconType::Diamond;
        case PinType::Vector3:   return ax::Drawing::IconType::Square;
        default:                 return ax::Drawing::IconType::Circle;
    }
}


float inline_widget_width(const Pin& pin) {
    switch (pin.type) {
        case PinType::Bool:   return 26.0f;
        case PinType::Number: return 64.0f;
        case PinType::String:
        case PinType::TextTag: return 120.0f;
        default: return 0.0f;
    }
}


std::string value_preview(const Pin& pin) {
    switch (pin.type) {
        case PinType::Entity:
        case PinType::Marker:
            return pin.value.world.entity_name.empty()
                       ? std::string("(pick...)")
                       : pin.value.world.entity_name;
        case PinType::Level:
            if (!pin.value.world.level_id.empty()) {
                return pin.value.world.level_id;
            }
            return pin.value.str.empty() ? std::string("(pick...)")
                                         : pin.value.str;
        case PinType::Item:
            return pin.value.item.display_name.empty()
                       ? (pin.value.item.internal_name.empty()
                              ? std::string("(pick...)")
                              : pin.value.item.internal_name)
                       : pin.value.item.display_name;
        case PinType::EntityDef:
            return pin.value.str.empty() ? std::string("(pick...)")
                                         : pin.value.str;
        default:
            return {};
    }
}

float input_row_width(const BlueprintQuest& quest, const Pin& pin) {
    float w = kPinIconSize;
    if (!pin.name.empty()) {
        w += kRowGap + ImGui::CalcTextSize(pin.name.c_str()).x;
    }
    if (pin.type != PinType::Exec && !quest.LinkInto(pin.id)) {
        const float widget = inline_widget_width(pin);
        if (widget > 0.0f) {
            w += kRowGap + widget;
        } else {
            const std::string preview = value_preview(pin);
            if (!preview.empty()) {
                w += kRowGap + ImGui::CalcTextSize(preview.c_str()).x;
            }
        }
    }
    return w;
}

float output_row_width(const Pin& pin) {
    float w = kPinIconSize;
    if (!pin.name.empty()) {
        w += kRowGap + ImGui::CalcTextSize(pin.name.c_str()).x;
    }
    return w;
}

void draw_pin_icon(const BlueprintQuest& quest, const Pin& pin) {
    const bool linked = quest.IsPinLinked(pin.id);
    const ImU32 color = PinColorU32(pin.type);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size(kPinIconSize, kPinIconSize);

    ed::BeginPin(ed::PinId((uintptr_t)pin.id),
                 pin.dir == PinDir::Input ? ed::PinKind::Input
                                          : ed::PinKind::Output);
    ax::Widgets::Icon(size, pin_icon_type(pin.type), linked,
                      ImGui::ColorConvertU32ToFloat4(color),
                      ImVec4(0.12f, 0.13f, 0.15f, 0.9f));
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ed::PinRect(min, max);
    const float cy = (min.y + max.y) * 0.5f;
    const ImVec2 pivot = pin.dir == PinDir::Input ? ImVec2(min.x, cy)
                                                  : ImVec2(max.x, cy);
    ed::PinPivotRect(pivot, pivot);
    ed::EndPin();
    (void)pos;
}


bool draw_inline_value(Pin& pin) {
    ImGui::PushID(pin.id);
    bool changed = false;
    switch (pin.type) {
        case PinType::Bool:
            changed = ImGui::Checkbox("##v", &pin.value.b);
            break;
        case PinType::Number: {
            ImGui::SetNextItemWidth(inline_widget_width(pin));
            changed = ImGui::InputDouble("##v", &pin.value.num, 0.0, 0.0,
                                         "%g");
            break;
        }
        case PinType::String:
        case PinType::TextTag: {
            ImGui::SetNextItemWidth(inline_widget_width(pin));
            changed = ImGui::InputText("##v", &pin.value.str);
            break;
        }
        default: {
            const std::string preview = value_preview(pin);
            if (!preview.empty()) {
                ImGui::TextDisabled("%s", preview.c_str());
            }
            break;
        }
    }
    ImGui::PopID();
    return changed;
}

}

ImU32 PinColorU32(PinType type) {
    const uint32_t argb = PinTypeColor(type);
    return IM_COL32((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF,
                    (argb >> 24) & 0xFF);
}

void DrawNode(BlueprintQuest& quest, Node& node, int severity) {
    const NodeDef* def = Registry::Find(node.type);
    if (!def) return;

    std::vector<Pin*> inputs;
    std::vector<Pin*> outputs;
    for (Pin& p : node.pins) {
        (p.dir == PinDir::Input ? inputs : outputs).push_back(&p);
    }

    std::string header = def->title;
    if ((node.type == "var.get" || node.type == "var.set") &&
        !node.prop.empty()) {
        header = (node.type == "var.get" ? "Get " : "Set ") + node.prop;
    }
    float content_w = ImGui::CalcTextSize(header.c_str()).x + 8.0f;
    const size_t rows = std::max(inputs.size(), outputs.size());
    float max_in = 0.0f;
    float max_out = 0.0f;
    for (Pin* p : inputs) max_in = std::max(max_in, input_row_width(quest, *p));
    for (Pin* p : outputs) max_out = std::max(max_out, output_row_width(*p));
    if (rows > 0) {
        content_w = std::max(content_w, max_in + 26.0f + max_out);
    }
    content_w = std::clamp(content_w, kMinContentWidth, kMaxContentWidth);

    ed::PushStyleColor(ed::StyleColor_NodeBg, ImVec4(0.125f, 0.14f, 0.16f, 0.92f));
    int border_pushes = 0;
    if (severity == 2) {
        ed::PushStyleColor(ed::StyleColor_NodeBorder,
                           ImVec4(0.95f, 0.30f, 0.28f, 0.95f));
        border_pushes = 1;
    } else if (severity == 1) {
        ed::PushStyleColor(ed::StyleColor_NodeBorder,
                           ImVec4(0.95f, 0.78f, 0.25f, 0.85f));
        border_pushes = 1;
    }
    ed::BeginNode(ed::NodeId((uintptr_t)node.id));
    ImGui::PushID(node.id);

    const float content_x = ImGui::GetCursorPosX();
    const ImVec2 header_min = ImGui::GetCursorScreenPos();

    ImGui::TextColored(ImVec4(0.96f, 0.97f, 0.99f, 1.0f), "%s",
                       header.c_str());
    float header_bottom = ImGui::GetItemRectMax().y + 4.0f;
    ImGui::Dummy(ImVec2(content_w, 4.0f));

    bool changed = false;
    for (size_t row = 0; row < rows; ++row) {
        ImGui::SetCursorPosX(content_x);
        if (row < inputs.size()) {
            Pin& pin = *inputs[row];
            draw_pin_icon(quest, pin);
            if (!pin.name.empty()) {
                ImGui::SameLine(0.0f, kRowGap);
                if (pin.optional) {
                    ImGui::TextDisabled("%s", pin.name.c_str());
                } else {
                    ImGui::TextUnformatted(pin.name.c_str());
                }
            }
            if (pin.type != PinType::Exec && !quest.LinkInto(pin.id)) {
                ImGui::SameLine(0.0f, kRowGap);
                changed |= draw_inline_value(pin);
            }
        } else {
            ImGui::Dummy(ImVec2(1.0f, kPinIconSize));
        }

        if (row < outputs.size()) {
            Pin& pin = *outputs[row];
            const float w = output_row_width(pin);
            ImGui::SameLine();
            ImGui::SetCursorPosX(content_x + content_w - w);
            if (!pin.name.empty()) {
                ImGui::TextUnformatted(pin.name.c_str());
                ImGui::SameLine(0.0f, kRowGap);
            }
            draw_pin_icon(quest, pin);
        }
    }

    if (!node.comment.empty()) {
        ImGui::SetCursorPosX(content_x);
        ImGui::PushTextWrapPos(content_x + content_w);
        ImGui::TextDisabled("%s", node.comment.c_str());
        ImGui::PopTextWrapPos();
    }

    ImGui::SetCursorPosX(content_x);
    ImGui::Dummy(ImVec2(content_w, 1.0f));

    ImGui::PopID();
    ed::EndNode();
    ed::PopStyleColor(1 + border_pushes);

    
    if (ImGui::IsItemVisible()) {
        ImDrawList* dl = ed::GetNodeBackgroundDrawList(
            ed::NodeId((uintptr_t)node.id));
        if (dl) {
            const ImVec2 node_min = ImGui::GetItemRectMin();
            const ImVec2 node_max = ImGui::GetItemRectMax();
            const uint32_t argb = def->header_color;
            const ImU32 color = IM_COL32((argb >> 16) & 0xFF,
                                         (argb >> 8) & 0xFF, argb & 0xFF, 235);
            const float border = ed::GetStyle().NodeBorderWidth * 0.5f;
            dl->AddRectFilled(
                ImVec2(node_min.x + border, node_min.y + border),
                ImVec2(node_max.x - border,
                       std::min(header_bottom, node_max.y)),
                color, ed::GetStyle().NodeRounding,
                ImDrawFlags_RoundCornersTop);
            (void)header_min;
        }
    }

    if (changed) quest.Touch();
}

}
