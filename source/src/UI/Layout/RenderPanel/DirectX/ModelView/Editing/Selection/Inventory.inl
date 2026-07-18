            if (sel_contents) {
                auto pretty_tag = [](std::string tag, int money) {

                    for (const char* pfx : { "INV_ITEM_", "OBJECT_",
                                             "TEXT_" }) {
                        const size_t n = std::strlen(pfx);
                        if (tag.size() > n && tag.compare(0, n, pfx) == 0) {
                            tag = tag.substr(n);
                            break;
                        }
                    }
                    constexpr const char* kNameSuffix = "_NAME";
                    constexpr size_t kNameSuffixLen = 5;
                    if (tag.size() > kNameSuffixLen &&
                        tag.compare(tag.size() - kNameSuffixLen,
                                    kNameSuffixLen, kNameSuffix) == 0) {
                        tag.resize(tag.size() - kNameSuffixLen);
                    }
                    if (tag.find('_') != std::string::npos ||
                        std::none_of(tag.begin(), tag.end(),
                                     [](unsigned char c) {
                                         return std::islower(c);
                                     })) {
                        bool word_start = true;
                        for (auto& c : tag) {
                            if (c == '_') {
                                c = ' ';
                                word_start = true;
                            } else {
                                c = word_start
                                    ? char(std::toupper((unsigned char)c))
                                    : char(std::tolower((unsigned char)c));
                                word_start = false;
                            }
                        }
                    }
                    if (money >= 0) {
                        tag += " (" + std::to_string(money) + " gold)";
                    }
                    return tag;
                };
                auto catalog_label = [&](uint32_t record_hash) {
                    for (const auto& c : g_item_details) {
                        if (c.record_hash == record_hash &&
                            !c.display_name.empty()) {
                            return c.display_name;
                        }
                    }
                    for (const auto& c : g_level_item_catalog) {
                        if (c.record_hash == record_hash) {
                            return pretty_tag(c.label, c.money);
                        }
                    }
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), "0x%08X", record_hash);
                    return std::string(buf);
                };
                auto item_label = [&](const Gdb::EntityContentsItem& it) {
                    if (!it.display_name.empty()) return it.display_name;
                    std::string tag = !it.name_tag.empty() ? it.name_tag
                                                           : it.entry_label;
                    if (tag.empty()) return catalog_label(it.record_hash);
                    return pretty_tag(std::move(tag), it.money);
                };
                ImGui::Spacing();
                ImGui::Separator();
                if (!sel_gameplay && !sel_contents->entity_name.empty()) {
                    ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f),
                                       "%s",
                                       sel_contents->entity_name.c_str());
                }
                draw_container_authored_rules(*sel_contents);
                const uint32_t sel_entity =
                    sel_gdb_entity_hash != 0
                        ? sel_gdb_entity_hash
                        : uint32_t(::g_selected_level_hash);
                std::vector<uint32_t> shown_items;
                bool staged = LevelEdit::GetChestContents(sel_entity,
                                                          shown_items);
                if (!staged) {
                    for (const auto& it : sel_contents->initial_items) {
                        shown_items.push_back(it.record_hash);
                    }
                }
                const bool contents_editable =
                    LevelEdit::Enabled() && !LevelEdit::Saving();

                static int s_picker_slot = -1;
                static uint32_t s_picker_entity = 0;
                static char s_picker_filter[64] = {};

                if (staged || !shown_items.empty() ||
                    sel_contents->has_inventory_component ||
                    contents_editable) {
                    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                                       staged ? "Initial items (edited):"
                                              : "Initial items:");
                    int remove_idx = -1;
                    bool open_picker = false;
                    for (size_t ii = 0; ii < shown_items.size(); ++ii) {
                        ImGui::PushID(int(ii));
                        if (contents_editable) {
                            if (ImGui::SmallButton("x")) remove_idx = int(ii);
                            ImGui::SameLine();
                            std::string label;
                            if (!staged &&
                                ii < sel_contents->initial_items.size()) {
                                label = item_label(
                                    sel_contents->initial_items[ii]);
                            } else {
                                label = catalog_label(shown_items[ii]);
                            }
                            if (ImGui::Selectable(label.c_str(), false,
                                    ImGuiSelectableFlags_DontClosePopups)) {
                                s_picker_slot = int(ii);
                                s_picker_entity = sel_entity;
                                s_picker_filter[0] = 0;
                                open_picker = true;
                            }
                        } else {
                            std::string label;
                            if (!staged &&
                                ii < sel_contents->initial_items.size()) {
                                label = item_label(
                                    sel_contents->initial_items[ii]);
                            } else {
                                label = catalog_label(shown_items[ii]);
                            }
                            ImGui::BulletText("%s", label.c_str());
                        }
                        ImGui::PopID();
                    }
                    if (shown_items.empty()) {
                        ImGui::TextDisabled(staged ? "  (emptied)"
                                                   : "  (empty)");
                    }
                    if (contents_editable) {
                        if (ImGui::SmallButton("+ Add item")) {
                            s_picker_slot = int(shown_items.size());
                            s_picker_entity = sel_entity;
                            s_picker_filter[0] = 0;
                            open_picker = true;
                        }
                        if (staged) {
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Revert")) {
                                LevelEdit::ClearChestContents(sel_entity);
                            }
                        }
                        if (staged) {
                            ImGui::TextDisabled("staged - Save to bake");
                        }
                    }
                    if (remove_idx >= 0 &&
                        size_t(remove_idx) < shown_items.size()) {
                        std::vector<uint32_t> next = shown_items;
                        next.erase(next.begin() + remove_idx);
                        LevelEdit::SetChestContents(sel_entity, next);
                    }
                    if (open_picker) {
                        ImGui::OpenPopup("##chest_item_picker");
                    }
                    if (ImGui::BeginPopup("##chest_item_picker")) {
                        if (s_picker_entity != sel_entity) {
                            ImGui::CloseCurrentPopup();
                        } else {
                            ImGui::SetNextItemWidth(260.0f);
                            ImGui::InputTextWithHint("##item_filter",
                                                     "search items...",
                                                     s_picker_filter,
                                                     sizeof(s_picker_filter));
                            std::string filter = s_picker_filter;
                            std::transform(filter.begin(), filter.end(),
                                           filter.begin(), ::tolower);
                            ImGui::BeginChild("##item_list",
                                              ImVec2(320.0f, 300.0f), true);
                            std::vector<int> rows;
                            rows.reserve(g_item_details.size());
                            for (int ci = 0;
                                 ci < int(g_item_details.size());
                                 ++ci) {
                                if (filter.empty()) {
                                    rows.push_back(ci);
                                    continue;
                                }
                                std::string low =
                                    g_item_details[size_t(ci)]
                                        .display_name;
                                std::transform(low.begin(), low.end(),
                                               low.begin(), ::tolower);
                                if (low.find(filter) !=
                                    std::string::npos) {
                                    rows.push_back(ci);
                                }
                            }
                            ImGuiListClipper clipper;
                            clipper.Begin(int(rows.size()));
                            while (clipper.Step()) {
                                for (int ri = clipper.DisplayStart;
                                     ri < clipper.DisplayEnd; ++ri) {
                                    const auto& c = g_item_details
                                        [size_t(rows[size_t(ri)])];
                                    const std::string& pl =
                                        c.display_name.empty()
                                            ? c.label
                                            : c.display_name;
                                    ImGui::PushID(int(c.record_hash));
                                    if (ImGui::Selectable(pl.c_str())) {
                                        std::vector<uint32_t> next =
                                            shown_items;
                                        if (size_t(s_picker_slot) <
                                            next.size()) {
                                            next[size_t(s_picker_slot)] =
                                                c.record_hash;
                                        } else {
                                            next.push_back(c.record_hash);
                                        }
                                        LevelEdit::SetChestContents(
                                            sel_entity, next);
                                        ImGui::CloseCurrentPopup();
                                    }
                                    ImGui::PopID();
                                }
                            }
                            ImGui::EndChild();
                        }
                        ImGui::EndPopup();
                    }
                }

                draw_container_loot_table_editor(sel_entity,
                                                 *sel_contents);
            } else if (sel_chest_addition >= 0) {
                auto pretty_tag2 = [](std::string tag, int money) {
                    for (const char* pfx : { "INV_ITEM_", "OBJECT_",
                                             "TEXT_" }) {
                        const size_t n = std::strlen(pfx);
                        if (tag.size() > n && tag.compare(0, n, pfx) == 0) {
                            tag = tag.substr(n);
                            break;
                        }
                    }
                    if (tag.size() > 5 &&
                        tag.compare(tag.size() - 5, 5, "_NAME") == 0) {
                        tag.resize(tag.size() - 5);
                    }
                    if (tag.find('_') != std::string::npos ||
                        std::none_of(tag.begin(), tag.end(),
                                     [](unsigned char c) {
                                         return std::islower(c);
                                     })) {
                        bool ws = true;
                        for (auto& c : tag) {
                            if (c == '_') {
                                c = ' ';
                                ws = true;
                            } else {
                                c = ws ? char(std::toupper((unsigned char)c))
                                       : char(std::tolower((unsigned char)c));
                                ws = false;
                            }
                        }
                    }
                    if (money >= 0) {
                        tag += " (" + std::to_string(money) + " gold)";
                    }
                    return tag;
                };
                auto catalog_label2 = [&](uint32_t record_hash) {
                    for (const auto& c : g_item_details) {
                        if (c.record_hash == record_hash &&
                            !c.display_name.empty()) {
                            return c.display_name;
                        }
                    }
                    for (const auto& c : g_level_item_catalog) {
                        if (c.record_hash == record_hash) {
                            return pretty_tag2(c.label, c.money);
                        }
                    }
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), "0x%08X", record_hash);
                    return std::string(buf);
                };
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f),
                                   LevelEdit::AdditionIsDigSpot(
                                       sel_chest_addition)
                                       ? "New dig spot (unsaved)"
                                       : "New container (unsaved)");
                std::vector<uint32_t> add_items;
                LevelEdit::GetAdditionChestItems(sel_chest_addition,
                                                 add_items);
                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                                   "Contents:");
                const bool add_editable =
                    LevelEdit::Enabled() && !LevelEdit::Saving();
                static char s_add_filter[64] = {};
                bool open_add_picker = false;
                static int s_add_slot = -1;
                int remove_idx = -1;
                for (size_t ii = 0; ii < add_items.size(); ++ii) {
                    ImGui::PushID(int(ii) + 0x1000);
                    if (add_editable) {
                        if (ImGui::SmallButton("x")) remove_idx = int(ii);
                        ImGui::SameLine();
                        if (ImGui::Selectable(
                                catalog_label2(add_items[ii]).c_str(),
                                false,
                                ImGuiSelectableFlags_DontClosePopups)) {
                            s_add_slot = int(ii);
                            s_add_filter[0] = 0;
                            open_add_picker = true;
                        }
                    } else {
                        ImGui::BulletText(
                            "%s", catalog_label2(add_items[ii]).c_str());
                    }
                    ImGui::PopID();
                }
                if (add_editable) {
                    if (ImGui::SmallButton("+ Add item")) {
                        s_add_slot = int(add_items.size());
                        s_add_filter[0] = 0;
                        open_add_picker = true;
                    }

                    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                                       "Random loot:");
                    const uint32_t cur_loot =
                        LevelEdit::GetAdditionLootTable(
                            sel_chest_addition);
                    std::string cur_label = "None";
                    if (cur_loot) {
                        char lb[32];
                        std::snprintf(lb, sizeof(lb), "table 0x%08X",
                                      cur_loot);
                        cur_label = lb;
                        for (const auto& kv : g_level_entity_contents) {
                            if (kv.second.potential_items_record ==
                                    cur_loot &&
                                !kv.second.entity_name.empty()) {
                                cur_label =
                                    "like " + kv.second.entity_name;
                                break;
                            }
                        }
                    }
                    ImGui::SetNextItemWidth(240.0f);
                    if (ImGui::BeginCombo("##rand_loot",
                                          cur_label.c_str())) {
                        if (ImGui::Selectable("None", cur_loot == 0)) {
                            LevelEdit::SetAdditionLootTable(
                                sel_chest_addition, 0);
                        }
                        std::unordered_set<uint32_t> seen_tables;
                        for (const auto& kv : g_level_entity_contents) {
                            const auto& ec = kv.second;
                            if (!ec.potential_items_record ||
                                ec.potential_items.empty()) {
                                continue;
                            }
                            if (!seen_tables
                                     .insert(ec.potential_items_record)
                                     .second) {
                                continue;
                            }
                            char lbl[128];
                            std::snprintf(
                                lbl, sizeof(lbl),
                                "%s (%zu entr%s)##%08X",
                                ec.entity_name.empty()
                                    ? "<unnamed>"
                                    : ec.entity_name.c_str(),
                                ec.potential_items.size(),
                                ec.potential_items.size() == 1 ? "y"
                                                               : "ies",
                                ec.potential_items_record);
                            if (ImGui::Selectable(
                                    lbl,
                                    cur_loot ==
                                        ec.potential_items_record)) {
                                LevelEdit::SetAdditionLootTable(
                                    sel_chest_addition,
                                    ec.potential_items_record);
                            }
                        }
                        ImGui::EndCombo();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "Give this container a weighted random loot "
                            "table borrowed from an existing container "
                            "in this level.");
                    }
                }
                if (remove_idx >= 0 &&
                    size_t(remove_idx) < add_items.size()) {
                    add_items.erase(add_items.begin() + remove_idx);
                    LevelEdit::SetAdditionChestItems(sel_chest_addition,
                                                     add_items);
                }
                if (open_add_picker) {
                    ImGui::OpenPopup("##add_chest_item_picker");
                }
                if (ImGui::BeginPopup("##add_chest_item_picker")) {
                    ImGui::SetNextItemWidth(260.0f);
                    ImGui::InputTextWithHint("##add_item_filter",
                                             "search items...",
                                             s_add_filter,
                                             sizeof(s_add_filter));
                    std::string filter = s_add_filter;
                    std::transform(filter.begin(), filter.end(),
                                   filter.begin(), ::tolower);
                    ImGui::BeginChild("##add_item_list",
                                      ImVec2(320.0f, 300.0f), true);
                    std::vector<int> rows;
                    rows.reserve(g_item_details.size());
                    for (int ci = 0;
                         ci < int(g_item_details.size()); ++ci) {
                        if (filter.empty()) {
                            rows.push_back(ci);
                            continue;
                        }
                        std::string low =
                            g_item_details[size_t(ci)].display_name;
                        std::transform(low.begin(), low.end(),
                                       low.begin(), ::tolower);
                        if (low.find(filter) != std::string::npos) {
                            rows.push_back(ci);
                        }
                    }
                    ImGuiListClipper clipper;
                    clipper.Begin(int(rows.size()));
                    while (clipper.Step()) {
                        for (int ri = clipper.DisplayStart;
                             ri < clipper.DisplayEnd; ++ri) {
                            const auto& c = g_item_details
                                [size_t(rows[size_t(ri)])];
                            const std::string& pl =
                                c.display_name.empty() ? c.label
                                                       : c.display_name;
                            ImGui::PushID(int(c.record_hash));
                            if (ImGui::Selectable(pl.c_str())) {
                                if (size_t(s_add_slot) <
                                    add_items.size()) {
                                    add_items[size_t(s_add_slot)] =
                                        c.record_hash;
                                } else {
                                    add_items.push_back(c.record_hash);
                                }
                                LevelEdit::SetAdditionChestItems(
                                    sel_chest_addition, add_items);
                                ImGui::CloseCurrentPopup();
                            }
                            ImGui::PopID();
                        }
                    }
                    ImGui::EndChild();
                    ImGui::EndPopup();
                }
            }
