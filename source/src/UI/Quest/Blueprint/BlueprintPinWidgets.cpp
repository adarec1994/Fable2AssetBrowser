#include "BlueprintPinWidgets.h"

#include "BlueprintEditor.h"
#include "Level/Core/LevelLoader.h"
#include "Utilities/State.h"

#include "IconsFontAwesome6.h"
#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <cctype>

namespace BlueprintUIDetail {

using namespace Quest::Bp;

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });
    return s;
}

bool filtered_out(const std::string& haystack, const std::string& needle) {
    return !needle.empty() && lower(haystack).find(needle) == std::string::npos;
}



const std::vector<std::string>& level_ids() {
    static std::vector<std::string> ids;
    static size_t indexed_from = (size_t)-1;
    if (indexed_from == S.all_level_files.size()) return ids;
    indexed_from = S.all_level_files.size();
    ids.clear();
    for (const FlatAssetEntry& e : S.all_level_files) {
        std::string p = e.full_path;
        std::replace(p.begin(), p.end(), '/', '\\');
        const std::string low = lower(p);
        const std::string prefix = "worlds\\albion\\";
        const size_t start = low.rfind(prefix, 0) == 0 ? prefix.size()
                                                       : std::string::npos;
        if (start == std::string::npos) continue;
        const size_t last_slash = p.find_last_of('\\');
        if (last_slash == std::string::npos || last_slash <= start) continue;
        const size_t scen_slash = p.find_last_of('\\', last_slash - 1);
        if (scen_slash == std::string::npos || scen_slash <= start) continue;
        std::string region = p.substr(start, scen_slash - start);
        if (std::find(ids.begin(), ids.end(), region) == ids.end()) {
            ids.push_back(region);
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

}

bool DrawItemPicker(Pin& pin) {
    bool changed = false;
    static std::string filter;
    const std::string preview = pin.value.item.display_name.empty()
        ? (pin.value.item.internal_name.empty() ? "(pick an item...)"
                                                : pin.value.item.internal_name)
        : pin.value.item.display_name;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##bp_item", preview.c_str())) {
        if (ImGui::IsWindowAppearing()) {
            filter.clear();
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##bp_item_filter", "Search items...",
                                 &filter);
        const std::string needle = lower(filter);
        ImGui::BeginChild("##bp_item_rows", ImVec2(420.0f, 240.0f), false);
        for (const Gdb::ItemDetail& item : g_item_details) {
            if (item.is_money || item.internal_name.empty()) continue;
            const std::string display = item.display_name.empty()
                ? item.label : item.display_name;
            if (filtered_out(display + ' ' + item.internal_name, needle)) {
                continue;
            }
            ImGui::PushID((int)item.record_hash);
            if (ImGui::Selectable(display.c_str(),
                                  pin.value.item.record_hash ==
                                      item.record_hash)) {
                pin.value.item.record_hash = item.record_hash;
                pin.value.item.internal_name = item.internal_name;
                pin.value.item.display_name = display;
                pin.value.item.model_path = item.model_path;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::EndCombo();
    }
    if (!pin.value.item.internal_name.empty()) {
        ImGui::TextDisabled("%s", pin.value.item.internal_name.c_str());
    }
    return changed;
}

bool DrawLevelPicker(Pin& pin) {
    bool changed = false;
    static std::string filter;
    const std::string preview = pin.value.world.level_id.empty()
        ? "(pick a level...)" : pin.value.world.level_id;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##bp_level", preview.c_str())) {
        if (ImGui::IsWindowAppearing()) {
            filter.clear();
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##bp_level_filter", "Search levels...",
                                 &filter);
        const std::string needle = lower(filter);
        ImGui::BeginChild("##bp_level_rows", ImVec2(360.0f, 220.0f), false);
        for (const std::string& id : level_ids()) {
            if (filtered_out(id, needle)) continue;
            if (ImGui::Selectable(id.c_str(),
                                  id == pin.value.world.level_id)) {
                pin.value.world.level_id = id;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndChild();
        ImGui::EndCombo();
    }
    return changed;
}

bool DrawEntityDefPicker(Pin& pin) {
    bool changed = false;
    static std::string filter;
    const std::string preview =
        pin.value.str.empty() ? "(pick a creature/entity...)" : pin.value.str;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##bp_entitydef", preview.c_str())) {
        if (ImGui::IsWindowAppearing()) {
            filter.clear();
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##bp_entitydef_filter",
                                 "Search entity definitions...", &filter);
        const std::string needle = lower(filter);
        ImGui::BeginChild("##bp_entitydef_rows", ImVec2(420.0f, 240.0f),
                          false);
        for (const Gdb::CreatureCatalogEntry& entity :
             g_global_entity_catalog) {
            if (entity.name.empty()) continue;
            if (filtered_out(entity.display_name + ' ' + entity.name,
                             needle)) {
                continue;
            }
            ImGui::PushID((int)entity.entity_hash);
            const std::string label = entity.display_name.empty()
                ? entity.name
                : entity.display_name + "  (" + entity.name + ")";
            if (ImGui::Selectable(label.c_str(),
                                  pin.value.str == entity.name)) {
                pin.value.str = entity.name;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::EndCombo();
    }
    return changed;
}

bool DrawEntityEditor(const BlueprintQuest& quest, Pin& pin) {
    bool changed = false;
    ImGui::SetNextItemWidth(-1.0f);
    changed |= ImGui::InputTextWithHint(
        "##bp_entity_name",
        pin.type == PinType::Marker ? "marker name in level"
                                    : "entity name in level",
        &pin.value.world.entity_name);

    const bool armed = BlueprintUI::PendingPickPin() == pin.id;
    if (armed) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.84f, 0.0f, 1.0f));
        ImGui::TextWrapped("Select an entity in the level view, then use "
                           "\"Assign selected quest reference\".");
        ImGui::PopStyleColor();
        if (ImGui::SmallButton("Cancel pick")) {
            BlueprintUI::ArmPinPick(0);
        }
    } else if (ImGui::SmallButton("Pick in level")) {
        BlueprintUI::ArmPinPick(pin.id);
    }
    if (!pin.value.world.level_id.empty()) {
        ImGui::TextDisabled("Level: %s", pin.value.world.level_id.c_str());
    }
    (void)quest;
    return changed;
}

}
