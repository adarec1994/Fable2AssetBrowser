static void draw_entity_gameplay_details(
    const Gdb::EntityGameplayDetails& details,
    bool show_entity_name = true);
static void draw_property_details(const Gdb::PropertyDetails& details);

#ifndef _WIN32
static void draw_entity_gameplay_details(
    const Gdb::EntityGameplayDetails& details,
    bool show_entity_name)
{
    ImGui::Spacing();
    ImGui::Separator();
    if (show_entity_name && !details.entity_name.empty()) {
        ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f), "%s",
                           details.entity_name.c_str());
    }
    ImGui::TextColored(ImVec4(0.55f, 0.9f, 1.0f, 1.0f),
                       "Entity gameplay");
    if (!details.faction_name.empty()) {
        ImGui::Text("Faction / allegiance: %s",
                    details.faction_name.c_str());
    }
    if (!details.combat_profile_name.empty()) {
        ImGui::TextWrapped("Combat profile: %s",
                           details.combat_profile_name.c_str());
    }
    if (!details.core_fields.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                           "Core stats:");
        for (const auto& field : details.core_fields) {
            ImGui::Text("%s: %s", field.label.c_str(),
                        field.value.c_str());
        }
    }
    if (!details.combat_fields.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                           "Combat:");
        for (const auto& field : details.combat_fields) {
            ImGui::Text("%s: %s", field.label.c_str(),
                        field.value.c_str());
        }
    }
}
#endif

void draw_placeholder() {

    ImVec2 region = ImGui::GetContentRegionAvail();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin,
                      ImVec2(origin.x + region.x, origin.y + region.y),
                      IM_COL32(20, 22, 28, 255));
    ImGui::Dummy(region);
}

void draw_item_tab_content() {
    ImVec2 region = ImGui::GetContentRegionAvail();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(
        origin, ImVec2(origin.x + region.x, origin.y + region.y),
        IM_COL32(20, 22, 28, 255));

    ImGui::BeginChild("##item_tab_content", region, false);
    if (S.selected_item < 0 ||
        S.selected_item >= static_cast<int>(g_item_details.size())) {
        ImGui::TextDisabled("Item data is no longer available.");
        ImGui::EndChild();
        return;
    }
    const Gdb::ItemDetail& item =
        g_item_details[static_cast<std::size_t>(S.selected_item)];
    std::string name;
    if (item.name_tag) TextBank::Lookup(item.name_tag, name);
    if (name.empty()) {
        name = item.display_name.empty() ? item.label : item.display_name;
    }
    ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f), "%s",
                       name.c_str());
    if (item.money >= 0) ImGui::Text("Value: %d gold", item.money);

    std::string description;
    if (item.desc_tag) TextBank::Lookup(item.desc_tag, description);
    if (!description.empty()) {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(description.c_str());
        ImGui::PopTextWrapPos();
    }
    if (!item.stats.empty()) {
        ImGui::Spacing();
        ImGui::SeparatorText("Stats");
        for (const auto& stat : item.stats) {
            ImGui::Text("%s: %s", stat.first.c_str(), stat.second.c_str());
        }
    }
    ImGui::EndChild();
}

void draw_entity_tab_content() {
    ImVec2 region = ImGui::GetContentRegionAvail();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(
        origin, ImVec2(origin.x + region.x, origin.y + region.y),
        IM_COL32(20, 22, 28, 255));

    ImGui::BeginChild("##entity_tab_content", region, false);
    if (S.selected_entity < 0 ||
        S.selected_entity >=
            static_cast<int>(g_global_entity_catalog.size())) {
        ImGui::TextDisabled("Entity data is no longer available.");
        ImGui::EndChild();
        return;
    }
    const auto& entity =
        g_global_entity_catalog[static_cast<std::size_t>(S.selected_entity)];
    ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f), "%s",
                       (entity.display_name.empty() ? entity.name
                                                    : entity.display_name)
                           .c_str());
    ImGui::TextDisabled("No renderable model was found for this entity.");
    const auto gameplay = g_global_entity_gameplay.find(entity.entity_hash);
    if (gameplay != g_global_entity_gameplay.end()) {
        draw_entity_gameplay_details(gameplay->second);
    }
    if (AnimTreeUI::Available(entity.entity_hash)) {
        ImGui::Spacing();
        if (ImGui::Button("Show Animation Tree")) {
            AnimTreeUI::Open(entity.entity_hash,
                             entity.display_name.empty()
                                 ? entity.name : entity.display_name);
        }
    }
    ImGui::EndChild();
}

bool is_adjacent_terrain_mesh_name(const std::string& name)
{
    return name.rfind("adjacent terrain", 0) == 0;
}

std::string clean_level_model_name(std::string name)
{
    const char* prefixes[] = { "prop: ", "engine_level: " };
    for (const char* prefix : prefixes) {
        const size_t n = std::strlen(prefix);
        if (name.rfind(prefix, 0) == 0) {
            name.erase(0, n);
            break;
        }
    }

    size_t hash = name.find('#');
    if (hash != std::string::npos) name.resize(hash);
    size_t inst = name.find(" (");
    if (inst != std::string::npos) name.resize(inst);

    std::replace(name.begin(), name.end(), '\\', '/');
    size_t slash = name.find_last_of('/');
    if (slash != std::string::npos) name = name.substr(slash + 1);

    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".mdl") {
        name.resize(name.size() - 4);
    }
    return name.empty() ? std::string("(unnamed)") : name;
}

std::string level_model_key_from_mesh_name(const std::string& name)
{
    std::string key = clean_level_model_name(name);
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return key;
}

std::string clean_level_material_name(std::string name)
{
    const size_t hash = name.find('#');
    if (hash != std::string::npos) {
        name.erase(0, hash + 1);
    } else {
        return clean_level_model_name(std::move(name));
    }

    const size_t inst = name.find(" (");
    if (inst != std::string::npos) name.resize(inst);

    return name.empty() ? std::string("(unnamed)") : name;
}

void draw_lua_source() {
    if (S.lua_preview_loading) {
        ImGui::TextDisabled("Decompiling...");
        return;
    }
    if (S.lua_preview_content.empty()) {
        ImGui::TextDisabled("(empty)");
        return;
    }

    ImVec2 sz = ImGui::GetContentRegionAvail();
    ImGui::PushStyleColor(ImGuiCol_FrameBg,        ImVec4(0.08f, 0.08f, 0.10f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.10f, 0.10f, 0.12f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  ImVec4(0.10f, 0.10f, 0.12f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text,           ImVec4(0.85f, 0.92f, 0.82f, 1.0f));
    ImGui::InputTextMultiline(
        "##lua_text",
        const_cast<char*>(S.lua_preview_content.c_str()),
        S.lua_preview_content.size() + 1,
        sz,
        ImGuiInputTextFlags_ReadOnly);
    ImGui::PopStyleColor(4);
}

void draw_lua_in_panel() {
    ImVec2 region = ImGui::GetContentRegionAvail();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin,
                      ImVec2(origin.x + region.x, origin.y + region.y),
                      IM_COL32(18, 18, 22, 255));

    if (!S.lua_preview_is_quest) {
        draw_lua_source();
        return;
    }

    if (ImGui::BeginTabBar("##quest_preview_tabs")) {
        const bool authored = QuestUI::IsAuthoredQuestActive();
        const ImGuiTabItemFlags node_flags =
            S.quest_preview_select_nodes
                ? ImGuiTabItemFlags_SetSelected
                : ImGuiTabItemFlags_None;
        if (ImGui::BeginTabItem(authored ? "Quest Flow" : "Node View",
                                nullptr, node_flags)) {
            if (S.lua_preview_loading) {
                ImGui::TextDisabled("Decompiling and building quest graph...");
            } else {
                QuestUI::DrawNodeView();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Lua Script")) {
            draw_lua_source();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    S.quest_preview_select_nodes = false;
}

void draw_gdb_in_panel() {
    ImVec2 region = ImGui::GetContentRegionAvail();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin,
                      ImVec2(origin.x + region.x, origin.y + region.y),
                      IM_COL32(18, 20, 23, 255));

    auto hex32 = [](uint32_t v) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0x%08X", v);
        return std::string(buf);
    };
    auto hex4 = [](size_t v) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%04X", (unsigned)(v & 0xFFFFu));
        return std::string(buf);
    };
    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        return s;
    };
    auto row_label = [](const GdbViewerRow& r) -> std::string {
        if (!r.name.empty()) return r.name;
        if (!r.hash_name.empty()) return r.hash_name;
        return "(unnamed)";
    };
    auto record_kind = [](const GdbViewerRow& r) -> const char* {
        (void)r;
        return "RecordData";
    };
    auto detail = [](const char* name, const std::string& value) {
        ImGui::TreeNodeEx(name,
                          ImGuiTreeNodeFlags_Leaf |
                          ImGuiTreeNodeFlags_NoTreePushOnOpen |
                          ImGuiTreeNodeFlags_Bullet,
                          "%s | %s", name, value.c_str());
    };
    auto hash_detail_value = [&](uint32_t hash, const std::string& name) {
        std::string v = hex32(hash);
        if (!name.empty()) v += "  " + name;
        return v;
    };

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.82f, 0.93f, 1.0f, 1.0f));
    ImGui::TextUnformatted(S.gdb_view_title.empty()
                               ? "GDB"
                               : S.gdb_view_title.c_str());
    ImGui::PopStyleColor();

    const float btn_w = ImGui::CalcTextSize("Close").x +
                        ImGui::GetStyle().FramePadding.x * 2.0f + 8.0f;
    ImGui::SameLine(ImGui::GetContentRegionAvail().x +
                    ImGui::GetCursorPosX() - btn_w);
    if (ImGui::SmallButton("Close##gdb_render")) {
        S.show_gdb_render = false;
    }
    ImGui::Separator();

    ImGui::SetNextItemWidth(340.0f);
    ImGui::InputTextWithHint("##gdb_filter", "Filter name/parent/hash",
                             &S.gdb_view_filter);
    ImGui::SameLine();
    ImGui::TextDisabled("%zu row(s)", S.gdb_view_rows.size());

    const std::string filter = lower(S.gdb_view_filter);
    std::vector<int> visible;
    visible.reserve(S.gdb_view_rows.size());
    for (size_t i = 0; i < S.gdb_view_rows.size(); ++i) {
        const auto& r = S.gdb_view_rows[i];
        if (filter.empty()) {
            visible.push_back((int)i);
            continue;
        }
        std::string hay = lower(row_label(r));
        hay += " " + lower(r.hash_name);
        hay += " " + lower(r.parent_name);
        hay += " " + lower(r.skeleton_file_name);
        hay += " " + lower(r.retarget_skeleton_file_name);
        hay += " " + lower(hex4(size_t(r.record_index) + 1));
        hay += " " + lower(r.model_path_name);
        for (const std::string& model_name : r.model_path_names) {
            hay += " " + lower(model_name);
        }
        hay += " " + lower(hex32(r.hash));
        hay += " " + lower(hex32(r.parent_hash));
        hay += " " + lower(hex32(r.model_path_hash));
        hay += " " + lower(hex32(r.skeleton_file_hash));
        hay += " " + lower(hex32(r.retarget_skeleton_file_hash));
        for (uint32_t model_hash : r.model_path_hashes) {
            hay += " " + lower(hex32(model_hash));
        }
        if (hay.find(filter) != std::string::npos) {
            visible.push_back((int)i);
        }
    }

    ImGui::Separator();
    ImGui::BeginChild("##gdb_tree_body", ImVec2(0, 0), false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    for (int row_i : visible) {
        const auto& r = S.gdb_view_rows[(size_t)row_i];
        const std::string id = hex4(size_t(r.record_index) + 1);
        const std::string label = row_label(r);

        ImGui::PushID(row_i);
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
        if (row_i == 0) flags |= ImGuiTreeNodeFlags_DefaultOpen;
        const bool open = ImGui::TreeNodeEx(
            "##gdb_record", flags, "%s  %s", id.c_str(), label.c_str());
        if (open) {
            detail(record_kind(r), label);
            detail("Index", hex4(size_t(r.record_index) + 1));
            detail("Hash", hash_detail_value(r.hash, r.hash_name));
            if (r.parent_hash != 0 || !r.parent_name.empty()) {
                detail("Parent",
                       hash_detail_value(r.parent_hash, r.parent_name));
            }
            if (r.model_path_hash != 0) {
                detail("ModelPathHash",
                       hash_detail_value(r.model_path_hash,
                                         r.model_path_name));
            }
            if (r.skeleton_file_hash != 0) {
                detail("SkeletonFile",
                       hash_detail_value(r.skeleton_file_hash,
                                         r.skeleton_file_name));
            }
            if (r.retarget_skeleton_file_hash != 0) {
                detail("RetargetSkeletonFile",
                       hash_detail_value(r.retarget_skeleton_file_hash,
                                         r.retarget_skeleton_file_name));
            }
            if (r.model_path_hashes.size() > 1) {
                const bool models_open = ImGui::TreeNodeEx(
                    "##gdb_model_hashes",
                    ImGuiTreeNodeFlags_SpanAvailWidth,
                    "ModelPathHashes | %zu", r.model_path_hashes.size());
                if (models_open) {
                    for (size_t mi = 0; mi < r.model_path_hashes.size(); ++mi) {
                        const std::string name =
                            mi < r.model_path_names.size()
                                ? r.model_path_names[mi]
                                : std::string();
                        detail("ModelPathHash",
                               hash_detail_value(r.model_path_hashes[mi],
                                                 name));
                    }
                    ImGui::TreePop();
                }
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}
