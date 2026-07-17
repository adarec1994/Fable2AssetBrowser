#include "BlueprintPalette.h"

#include "Quest/Blueprint/BlueprintNodeRegistry.h"

#include "imgui_stdlib.h"
#include <imgui_node_editor.h>

#include <algorithm>
#include <cctype>
#include <string>

namespace BlueprintUIDetail {

using namespace Quest::Bp;

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });
    return s;
}




bool def_compatible_with(const NodeDef& def, const Pin& from) {
    for (const PinSpec& spec : def.pins) {
        if (spec.dir == from.dir) continue;
        const bool from_is_exec = from.type == PinType::Exec;
        const bool spec_is_exec = spec.type == PinType::Exec;
        if (from_is_exec != spec_is_exec) continue;
        const bool ok = from.dir == PinDir::Output
                            ? PinTypesCompatible(from.type, spec.type)
                            : PinTypesCompatible(spec.type, from.type);
        if (ok) return true;
    }
    return false;
}

}

int DrawPaletteContents(BlueprintQuest& quest, ImVec2 spawn_pos,
                        int pending_pin) {
    static std::string filter;
    if (ImGui::IsWindowAppearing()) {
        filter.clear();
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(240.0f);
    ImGui::InputTextWithHint("##bp_palette_filter", "Search nodes...", &filter);
    ImGui::Separator();

    const Pin* from = pending_pin ? quest.PinById(pending_pin) : nullptr;
    const std::string needle = lower(filter);

    int created = 0;
    ImGui::BeginChild("##bp_palette_list", ImVec2(280.0f, 320.0f), false);
    for (const std::string& category : Registry::Categories()) {
        bool header_drawn = false;
        for (const NodeDef& def : Registry::All()) {
            if (def.category != category) continue;
            if (from && !def_compatible_with(def, *from)) continue;
            if (!needle.empty() &&
                lower(def.title).find(needle) == std::string::npos &&
                lower(def.category).find(needle) == std::string::npos) {
                continue;
            }
            if (!header_drawn) {
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      ImVec4(1.0f, 0.84f, 0.0f, 1.0f));
                ImGui::TextUnformatted(category.c_str());
                ImGui::PopStyleColor();
                header_drawn = true;
            }
            if (ImGui::Selectable(def.title.c_str())) {
                created = Registry::Instantiate(quest, def.type, spawn_pos.x,
                                                spawn_pos.y);
                if (created) {
                    ax::NodeEditor::SetNodePosition(
                        ax::NodeEditor::NodeId((uintptr_t)created),
                        spawn_pos);
                }
                if (created && from) {
                    
                    Node* node = quest.NodeById(created);
                    for (Pin& p : node->pins) {
                        std::string reason;
                        const int a = from->dir == PinDir::Output ? from->id
                                                                  : p.id;
                        const int b = from->dir == PinDir::Output ? p.id
                                                                  : from->id;
                        if (p.dir != from->dir &&
                            quest.AddLink(a, b, reason)) {
                            break;
                        }
                    }
                }
                ImGui::CloseCurrentPopup();
                break;
            }
        }
        if (created) break;
    }
    ImGui::EndChild();
    return created;
}

}
