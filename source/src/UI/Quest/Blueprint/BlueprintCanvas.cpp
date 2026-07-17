#include "BlueprintCanvas.h"

#include "BlueprintNodeDrawer.h"
#include "BlueprintPalette.h"

#include "imgui.h"

namespace ed = ax::NodeEditor;

namespace BlueprintUIDetail {

using namespace Quest::Bp;

namespace {

void ensure_context(CanvasState& state) {
    if (state.ctx) return;
    ed::Config config;
    config.SettingsFile = nullptr;
    config.CanvasSizeMode = ed::CanvasSizeMode::CenterOnly;
    state.ctx = ed::CreateEditor(&config);
    ed::SetCurrentEditor(state.ctx);
    ed::GetStyle().SourceDirection = ImVec2(1.0f, 0.0f);
    ed::GetStyle().TargetDirection = ImVec2(-1.0f, 0.0f);
    ed::SetCurrentEditor(nullptr);
    state.layout_pending = true;
    state.fit_pending = true;
}

void handle_link_creation(BlueprintQuest& quest, CanvasState& state,
                          bool& open_palette) {
    if (ed::BeginCreate(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), 2.5f)) {
        ed::PinId a = 0;
        ed::PinId b = 0;
        if (ed::QueryNewLink(&a, &b)) {
            const int pin_a = (int)a.Get();
            const int pin_b = (int)b.Get();
            std::string reason;
            if (CanConnect(quest, pin_a, pin_b, reason)) {
                if (ed::AcceptNewItem(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), 3.0f)) {
                    quest.AddLink(pin_a, pin_b, reason);
                }
            } else {
                ed::RejectNewItem(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), 2.5f);
                if (!reason.empty()) {
                    ed::Suspend();
                    ImGui::SetTooltip("%s", reason.c_str());
                    ed::Resume();
                }
            }
        }
        ed::PinId from = 0;
        if (ed::QueryNewNode(&from)) {
            if (ed::AcceptNewItem()) {
                state.pending_palette_pin = (int)from.Get();
                open_palette = true;
            }
        }
        
        ed::EndCreate();
    }
}

void handle_deletion(BlueprintQuest& quest) {
    if (ed::BeginDelete()) {
        ed::LinkId link_id = 0;
        while (ed::QueryDeletedLink(&link_id)) {
            if (ed::AcceptDeletedItem()) {
                quest.RemoveLink((int)link_id.Get());
            }
        }
        ed::NodeId node_id = 0;
        while (ed::QueryDeletedNode(&node_id)) {
            if (ed::AcceptDeletedItem()) {
                quest.RemoveNode((int)node_id.Get());
            }
        }
        
        ed::EndDelete();
    }
}

}

void DrawCanvas(BlueprintQuest& quest, CanvasState& state) {
    ensure_context(state);
    ed::SetCurrentEditor(state.ctx);
    ed::Begin("##bp_canvas");

    {
        const ImVec2 mouse_canvas = ed::ScreenToCanvas(ImGui::GetMousePos());
        state.spawn_x = mouse_canvas.x;
        state.spawn_y = mouse_canvas.y;
    }

    for (Node& node : quest.nodes) {
        if (state.layout_pending) {
            ed::SetNodePosition(ed::NodeId((uintptr_t)node.id),
                                ImVec2(node.x, node.y));
        }
        const auto diag = state.node_diag.find(node.id);
        DrawNode(quest, node,
                 diag == state.node_diag.end() ? 0 : diag->second);
    }
    state.layout_pending = false;

    
    
    if (state.place_topright_node != 0) {
        const ed::NodeId nid((uintptr_t)state.place_topright_node);
        const ImVec2 size = ed::GetNodeSize(nid);
        if (size.x > 0.0f) {
            ed::SetNodePosition(
                nid, ImVec2(state.place_pos_x - size.x, state.place_pos_y));
            state.place_topright_node = 0;
        }
    }

    if (state.focus_node_request != 0) {
        ed::SelectNode(ed::NodeId((uintptr_t)state.focus_node_request));
        ed::NavigateToSelection();
        state.selected_node = state.focus_node_request;
        state.focus_node_request = 0;
    }

    for (const Link& link : quest.links) {
        const Pin* from = quest.PinById(link.from_pin);
        const ImU32 color = from ? PinColorU32(from->type)
                                 : IM_COL32(220, 220, 220, 255);
        const float thickness =
            (from && from->type == PinType::Exec) ? 3.0f : 2.0f;
        ed::Link(ed::LinkId((uintptr_t)link.id),
                 ed::PinId((uintptr_t)link.from_pin),
                 ed::PinId((uintptr_t)link.to_pin),
                 ImGui::ColorConvertU32ToFloat4(color), thickness);
    }

    bool open_palette = false;
    handle_link_creation(quest, state, open_palette);
    handle_deletion(quest);

    
    for (Node& node : quest.nodes) {
        const ImVec2 pos = ed::GetNodePosition(ed::NodeId((uintptr_t)node.id));
        node.x = pos.x;
        node.y = pos.y;
    }

    ed::Suspend();
    if (ed::ShowBackgroundContextMenu()) {
        state.pending_palette_pin = 0;
        state.palette_x = state.spawn_x;
        state.palette_y = state.spawn_y;
        ImGui::OpenPopup("##bp_add_node");
    }
    ed::NodeId context_node = 0;
    if (ed::ShowNodeContextMenu(&context_node)) {
        state.selected_node = (int)context_node.Get();
        ImGui::OpenPopup("##bp_node_menu");
    }
    if (open_palette) {
        state.palette_x = state.spawn_x;
        state.palette_y = state.spawn_y;
        ImGui::OpenPopup("##bp_add_node");
    }

    if (ImGui::BeginPopup("##bp_add_node")) {
        const int created = DrawPaletteContents(
            quest, ImVec2(state.palette_x, state.palette_y),
            state.pending_palette_pin);
        if (created != 0) {
            state.place_topright_node = created;
            state.place_pos_x = state.palette_x;
            state.place_pos_y = state.palette_y;
        }
        ImGui::EndPopup();
    } else {
        state.pending_palette_pin = 0;
    }

    if (ImGui::BeginPopup("##bp_node_menu")) {
        if (ImGui::MenuItem("Duplicate node")) {
            if (const Node* src = quest.NodeById(state.selected_node)) {
                Node copy = *src;
                copy.id = quest.AllocId();
                copy.x += 40.0f;
                copy.y += 40.0f;
                for (Pin& p : copy.pins) p.id = quest.AllocId();
                quest.nodes.push_back(std::move(copy));
                ed::SetNodePosition(
                    ed::NodeId((uintptr_t)quest.nodes.back().id),
                    ImVec2(quest.nodes.back().x, quest.nodes.back().y));
                quest.Touch();
            }
        }
        if (ImGui::MenuItem("Delete node")) {
            quest.RemoveNode(state.selected_node);
            state.selected_node = 0;
        }
        ImGui::EndPopup();
    }
    ed::Resume();

    if (state.fit_pending && !quest.nodes.empty()) {
        ed::NavigateToContent();
        state.fit_pending = false;
    }

    
    {
        ed::NodeId selected[1] = {};
        if (ed::GetSelectedNodes(selected, 1) > 0) {
            state.selected_node = (int)selected[0].Get();
        } else if (ed::GetSelectedObjectCount() == 0) {
            state.selected_node = 0;
        }
    }

    ed::End();
    ed::SetCurrentEditor(nullptr);
}

void DestroyCanvas(CanvasState& state) {
    if (state.ctx) {
        ed::DestroyEditor(state.ctx);
        state.ctx = nullptr;
    }
}

}
