        if (s_active_tab == 6) {
            struct LvlMap {
                const char* path;
                const char* name;
            };
            struct LvlGroup {
                const char* heading;
                std::initializer_list<LvlMap> entries;
            };
            static const LvlGroup kLevelGroups[] = {
                {"Bloodstone", {
                    {"worlds\\albion\\bloodstone\\defaultscenario\\defaultscenario.engine_level", "Bloodstone"},
                    {"worlds\\albion\\caves\\bloodstone\\bloodstone_assault\\defaultscenario\\defaultscenario.engine_level", "Bloodstone Assault"},
                    {"worlds\\albion\\caves\\bloodstone\\sinkhole\\defaultscenario\\defaultscenario.engine_level", "Sinkhole"},
                    {"worlds\\albion\\caves\\bloodstone\\treasureisland\\defaultscenario\\defaultscenario.engine_level", "Treasure Island"},
                    {"worlds\\albion\\reaver beach (bloodtsone)\\defaultscenario\\defaultscenario.engine_level", "Reaver Beach"},
                }},
                {"Bower Lake", {
                    {"worlds\\albion\\bowerlake\\chapter3\\chapter3.engine_level", "Chapter 3"},
                    {"worlds\\albion\\bowerlake\\defaultscenario\\defaultscenario.engine_level", "Bower Lake"},
                    {"worlds\\albion\\caves\\bowerlake\\thagscave\\defaultscenario\\defaultscenario.engine_level", "Thag's Cave"},
                    {"worlds\\albion\\tombs\\bowerlake\\rescuemybabytomb\\defaultscenario\\defaultscenario.engine_level", "\"Rescue My Baby\" Tomb"},
                }},
                {"Brightwood", {
                    {"worlds\\albion\\brightwood\\chapter3abandonedfarm\\chapter3abandonedfarm.engine_level", "Abandoned Farm"},
                    {"worlds\\albion\\brightwood\\chapter3bigfarm\\chapter3bigfarm.engine_level", "Big Farm"},
                    {"worlds\\albion\\brightwood\\defaultscenario\\defaultscenario.engine_level", "Brightwood"},
                    {"worlds\\albion\\caves\\brightwood\\bwfarmcellar\\defaultscenario\\defaultscenario.engine_level", "Brightwood Farm Cellar"},
                    {"worlds\\albion\\caves\\brightwood\\wellcave\\defaultscenario\\defaultscenario.engine_level", "Wellcave"},
                }},
                {"Bowerstone Cemetary", {
                    {"worlds\\albion\\bwscemetary\\ch3_cemetary\\ch3_cemetary.engine_level", "Chapter 3"},
                    {"worlds\\albion\\bwscemetary\\defaultscenario\\defaultscenario.engine_level", "Bowerstone Cemetary"},
                    {"worlds\\albion\\caves\\bwscemetary\\gravekeeperscave\\defaultscenario\\defaultscenario.engine_level", "Gravekeepers Cave"},
                    {"worlds\\albion\\tombs\\bwscemetery\\hallofthedead\\defaultscenario\\defaultscenario.engine_level", "Hall of the Dead"},
                    {"worlds\\albion\\tombs\\bwscemetery\\ladygreystomb\\defaultscenario\\defaultscenario.engine_level", "Lady Grey's Tomb"},
                }},
                {"Bowerstone Market", {
                    {"worlds\\albion\\bwsmarket\\bwsmarket_chapter3\\bwsmarket_chapter3.engine_level", "Chapter 3"},
                    {"worlds\\albion\\bwsmarket\\defaultscenario\\defaultscenario.engine_level", "Bowerstone Market"},
                    {"worlds\\albion\\tombs\\bwsmarket\\nightmare hollow\\defaultscenario\\defaultscenario.engine_level", "Nightmare Hollow"},
                }},
                {"Bowerstone Slums", {
                    {"worlds\\albion\\bwsslums\\chapter2posh\\chapter2posh.engine_level", "Chapter 2 - Posh"},
                    {"worlds\\albion\\bwsslums\\chapter2slums\\chapter2slums.engine_level", "Chapter 2 - Slums"},
                    {"worlds\\albion\\bwsslums\\chapter3posh\\chapter3posh.engine_level", "Chapter 3 - Posh"},
                    {"worlds\\albion\\bwsslums\\chapter3slums\\chapter3slums.engine_level", "Chapter 3 - Slums"},
                    {"worlds\\albion\\bwsslums\\defaultscenario\\defaultscenario.engine_level", "Bowerstone Slums"},
                }},
                {"Dunecrest", {
                    {"worlds\\albion\\dunecrestnew\\defaultscenario\\defaultscenario.engine_level", "Dunecrest New"},
                    {"worlds\\albion\\caves\\dunecrest\\hobbecave\\defaultscenario\\defaultscenario.engine_level", "Hobbe Cave"},
                    {"worlds\\albion\\caves\\dunecrest\\inncave\\defaultscenario\\defaultscenario.engine_level", "Inn Cave"},
                    {"worlds\\albion\\caves\\dunecrest\\waterfallcave\\defaultscenario\\defaultscenario.engine_level", "Waterfall Cave"},
                    {"worlds\\albion\\dunecrestnew\\chapter3\\chapter3.engine_level", "Chapter 3"},
                }},
                {"Deepwood", {
                    {"worlds\\albion\\caves\\deepwood\\rivercave\\defaultscenario\\defaultscenario.engine_level", "River Cave"},
                }},
                {"Wraithmarsh", {
                    {"worlds\\albion\\wraithmarsh\\defaultscenario\\defaultscenario.engine_level", "Wraithmarsh"},
                    {"worlds\\albion\\caves\\wraithmarsh\\wellcave\\defaultscenario\\defaultscenario.engine_level", "Well Cave"},
                    {"worlds\\albion\\tombs\\wraithmarsh\\autumnshrine\\defaultscenario\\defaultscenario.engine_level", "Autumn Shrine"},
                    {"worlds\\albion\\tombs\\wraithmarsh\\hotcrypt\\defaultscenario\\defaultscenario.engine_level", "Hot Crypt"},
                    {"worlds\\albion\\tombs\\wraithmarsh\\wraithmarshtobloodstonetomb\\defaultscenario\\defaultscenario.engine_level", "Wraithmarsh to Bloodstone Tomb"},
                }},
                {"Westcliffe", {
                    {"worlds\\albion\\westcliff\\chapter3\\chapter3.engine_level", "Chapter 3"},
                    {"worlds\\albion\\westcliff\\defaultscenario\\defaultscenario.engine_level", "Westcliffe"},
                    {"worlds\\albion\\caves\\westcliff\\palacecave\\defaultscenario\\defaultscenario.engine_level", "Palace Cave"},
                    {"worlds\\albion\\caves\\westcliff\\smugglerscave\\defaultscenario\\defaultscenario.engine_level", "Smuggler's Cave"},
                    {"worlds\\albion\\caves\\westcliff\\westcliffexterior\\defaultscenario\\defaultscenario.engine_level", "Westcliffe Exterior"},
                }},
                {"Ravenscar", {
                    {"worlds\\albion\\caves\\ravenscar\\hobbescavern\\defaultscenario\\defaultscenario.engine_level", "Hobbes Cavern"},
                    {"worlds\\albion\\caves\\ravenscar\\rvsritualcave\\defaultscenario\\defaultscenario.engine_level", "Ravenscar Ritual Cave"},
                    {"worlds\\albion\\ravenscar\\chapter3_evil\\chapter3_evil.engine_level", "Chapter 3 - Evil"},
                    {"worlds\\albion\\ravenscar\\chapter3_good\\chapter3_good.engine_level", "Chapter 3 - Good"},
                    {"worlds\\albion\\ravenscar\\defaultscenario\\defaultscenario.engine_level", "Ravenscar"},
                }},
                {"Castle Fairfax", {
                    {"worlds\\albion\\fairfaxcastlegardens\\defaultscenario\\defaultscenario.engine_level", "Fairfax Castle Gardens"},
                    {"worlds\\albion\\fairfaxcastlegardens\\ff_chapter1\\ff_chapter1.engine_level", "Chapter 1"},
                    {"worlds\\albion\\fairfaxcastlegardens\\ff_chapter3\\ff_chapter3.engine_level", "Chapter 3"},
                    {"worlds\\albion\\tombs\\fairfaxcastlegardens\\fairfaxtomb\\defaultscenario\\defaultscenario.engine_level", "Fairfax Tomb"},
                }},
                {"Tattered Spire", {
                    {"worlds\\albion\\tatteredspire\\chapter3\\chapter3.engine_level", "Chapter 3"},
                    {"worlds\\albion\\tatteredspire\\chapter4\\chapter4.engine_level", "Chapter 4"},
                    {"worlds\\albion\\tatteredspire\\defaultscenario\\defaultscenario.engine_level", "Tattered Spire"},
                }},
                {"Mystery Island", {
                    {"worlds\\albion\\mysteryisland\\defaultscenario\\defaultscenario.engine_level", "Mystery Island"},
                    {"worlds\\albion\\mysteryisland\\summer\\summer.engine_level", "Summer"},
                    {"worlds\\albion\\mysteryisland\\winter\\winter.engine_level", "Winter"},
                }},
                {"Shrines", {
                    {"worlds\\albion\\summershrine\\defaultscenario\\defaultscenario.engine_level", "Summer Shrine"},
                    {"worlds\\albion\\wintershrine\\defaultscenario\\defaultscenario.engine_level", "Winter Shrine"},
                }},
                {"Other", {
                    {"worlds\\albion\\templeofevil\\defaultscenario\\defaultscenario.engine_level", "Temple of Evil"},
                    {"worlds\\albion\\dreamworld\\defaultscenario\\defaultscenario.engine_level", "Dreamworld"},
                    {"worlds\\albion\\crucible\\defaultscenario\\defaultscenario.engine_level", "Crucible"},
                    {"worlds\\albion\\chamberofseasons\\defaultscenario\\defaultscenario.engine_level", "Chamber of Seasons"},
                    {"worlds\\albion\\caves\\gargoylescave\\defaultscenario\\defaultscenario.engine_level", "Gargoyle's Cave"},
                }},
                {"Demon Doors", {
                    {"worlds\\albion\\demondoors\\bloodstonedd\\defaultscenario\\defaultscenario.engine_level", "Bloodstone Demon Door"},
                    {"worlds\\albion\\demondoors\\bowerlakedd\\defaultscenario\\defaultscenario.engine_level", "Bower Lake Demon Door"},
                    {"worlds\\albion\\demondoors\\brightwooddd\\defaultscenario\\defaultscenario.engine_level", "Brightwood Demon Door"},
                    {"worlds\\albion\\demondoors\\deepwooddd\\defaultscenario\\defaultscenario.engine_level", "Deepwood Demon Door"},
                    {"worlds\\albion\\demondoors\\dunecrestdd\\defaultscenario\\defaultscenario.engine_level", "Dunecrest Demon Door"},
                    {"worlds\\albion\\demondoors\\homestead\\defaultscenario\\defaultscenario.engine_level", "Homestead Demon Door"},
                    {"worlds\\albion\\demondoors\\marcusmemorial\\defaultscenario\\defaultscenario.engine_level", "Marcus Memorial Demon Door"},
                    {"worlds\\albion\\demondoors\\ravenscardd\\defaultscenario\\defaultscenario.engine_level", "Ravenscar Demon Door"},
                    {"worlds\\albion\\demondoors\\westcliffdd\\defaultscenario\\defaultscenario.engine_level", "Westcliffe Demon Door"},
                }},
                {"DLC", {
                    {"worlds\\albion\\dlc2\\dlc2_colosseum\\defaultscenario\\defaultscenario.engine_level", "Colosseum"},
                    {"worlds\\albion\\dlc2\\dlc2_future\\defaultscenario\\defaultscenario.engine_level", "Future"},
                    {"worlds\\albion\\dlc2\\dlc2_past\\defaultscenario\\defaultscenario.engine_level", "Past"},
                    {"worlds\\albion\\dlc2\\dlc2_present\\defaultscenario\\defaultscenario.engine_level", "Present"},
                }},
            };

            auto norm = [](std::string s) -> std::string {
                std::transform(s.begin(), s.end(), s.begin(),
                               [](unsigned char c){ return std::tolower(c); });
                std::replace(s.begin(), s.end(), '/', '\\');
                return s;
            };

            std::unordered_map<std::string, std::string> path_to_name;
            for (const auto& g : kLevelGroups) {
                for (const auto& m : g.entries) {
                    path_to_name[norm(m.path)] = m.name;
                }
            }

            {
                const bool new_level_busy =
                    Level::IsAsyncLoadInProgress() ||
                    Level::IsExportInProgress() ||
                    tree_build_in_progress();
                if (new_level_busy) ImGui::BeginDisabled();
                if (ImGui::Button("+ New Level", ImVec2(-1, 0))) {
                    if (!level_edit_click_guard("Level creation")) {
                        NewLevelDialog::Open();
                    }
                }
                if (new_level_busy) ImGui::EndDisabled();
            }
            NewLevelDialog::Draw();

            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##level_filter", "Filter",
                                     &S.level_filter);
            std::string flow = S.level_filter;
            std::transform(flow.begin(), flow.end(), flow.begin(), ::tolower);

            ImGui::BeginChild("levels_list", ImVec2(0, 0), false);
            static FlatAssetEntry s_delete_level_entry{};
            static std::string s_delete_level_name;
            if (S.all_level_files.empty()) {
                ImGui::TextDisabled("No .engine_level files indexed yet.");
                ImGui::TextDisabled("Open a Fable 2 root to populate the list.");
            } else {
                auto draw_entry = [&](const FlatAssetEntry& e,
                                      const std::string& friendly)
                {
                    ImGui::PushID(&e);
                    const bool level_busy =
                        Level::IsAsyncLoadInProgress() ||
                        Level::IsExportInProgress();
                    if (level_busy) ImGui::BeginDisabled();
                    if (ImGui::Selectable(friendly.c_str(), false,
                                          ImGuiSelectableFlags_SpanAllColumns))
                    {
                        if (!level_edit_click_guard("Level loading")) {
                            S.show_item_details = false;
                            S.selected_item = -1;
                            S.show_entity_details = false;
                            S.selected_entity = -1;
                            ContentTabs::OpenLevel(e, friendly);
                        }
                    }
                    if (level_busy) ImGui::EndDisabled();
                    if (ImGui::BeginPopupContextItem("##lvl_ctx")) {
                        if (ImGui::MenuItem("View Heightmap")) {
                            std::vector<uint8_t> rgba;
                            int hw = 0, hh = 0;
                            if (Level::RenderHeightmapToRGBA(e, rgba, hw, hh)) {
                                extern std::atomic<bool>    g_pending_heightmap_view_load;
                                extern std::vector<uint8_t> g_pending_heightmap_view_rgba;
                                extern int                  g_pending_heightmap_view_w;
                                extern int                  g_pending_heightmap_view_h;
                                extern std::string          g_pending_heightmap_view_name;
                                extern std::string          g_pending_heightmap_view_kind;
                                g_pending_heightmap_view_rgba = std::move(rgba);
                                g_pending_heightmap_view_w    = hw;
                                g_pending_heightmap_view_h    = hh;
                                g_pending_heightmap_view_name = friendly;
                                g_pending_heightmap_view_kind = "Heightmap";
                                g_pending_heightmap_view_load = true;
                            }
                        }
                        if (ImGui::BeginMenu("Export")) {
                            if (ImGui::MenuItem("GLB")) {
                                Level::ExportAsync(e,
                                    Level::ExportFormat::GLB);
                            }
                            if (ImGui::MenuItem("FBX")) {
                                Level::ExportAsync(e,
                                    Level::ExportFormat::FBX);
                            }
                            ImGui::EndMenu();
                        }
                        if (Level::Creation::IsCustomLooseLevel(e)) {
                            ImGui::Separator();
                            if (ImGui::MenuItem("Delete Level")) {
                                s_delete_level_entry = e;
                                s_delete_level_name = friendly;
                            }
                        }
                        ImGui::EndPopup();
                    }
                    if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                        ImGui::BeginTooltip();
                        ImGui::TextUnformatted(e.full_path.c_str());
                        ImGui::Text("BNK: %s",
                            std::filesystem::path(e.bnk_path)
                                .filename().string().c_str());
                        ImGui::Text("Size: %u bytes", e.size);
                        ImGui::EndTooltip();
                    }
                    ImGui::PopID();
                };

                std::unordered_map<std::string, const FlatAssetEntry*> by_path;
                for (const auto& e : S.all_level_files) {
                    by_path[norm(e.full_path)] = &e;
                }

                std::unordered_set<const FlatAssetEntry*> placed;
                std::unordered_set<std::string> placed_paths;

                auto matches_filter = [&](const std::string& friendly,
                                          const std::string& full_path) {
                    if (flow.empty()) return true;
                    auto contains = [&](const std::string& s) {
                        std::string l = s;
                        std::transform(l.begin(), l.end(), l.begin(),
                            [](unsigned char c){ return std::tolower(c); });
                        return l.find(flow) != std::string::npos;
                    };
                    return contains(friendly) || contains(full_path);
                };

                for (const auto& g : kLevelGroups) {
                    std::vector<std::pair<const FlatAssetEntry*, std::string>> rows;
                    for (const auto& m : g.entries) {
                        auto it = by_path.find(norm(m.path));
                        if (it == by_path.end()) continue;
                        if (!matches_filter(m.name, it->second->full_path)) continue;
                        rows.push_back({it->second, std::string(m.name)});
                        placed.insert(it->second);
                        placed_paths.insert(norm(it->second->full_path));
                    }
                    if (rows.empty()) continue;

                    ImGui::PushStyleColor(ImGuiCol_Text,
                        ImVec4(1.0f, 0.84f, 0.0f, 1.0f));
                    ImGui::TextUnformatted(g.heading);
                    ImGui::PopStyleColor();
                    ImGui::Indent(8.0f);
                    for (const auto& [e, friendly] : rows) {
                        draw_entry(*e, friendly);
                    }
                    ImGui::Unindent(8.0f);
                    ImGui::Spacing();
                }

                auto is_loose_source = [](const FlatAssetEntry& e) {
                    std::string src = e.bnk_path;
                    std::transform(src.begin(), src.end(), src.begin(),
                        [](unsigned char c){ return (char)std::tolower(c); });
                    return src.size() < 4 ||
                           src.compare(src.size() - 4, 4, ".bnk") != 0;
                };
                std::vector<std::pair<const FlatAssetEntry*, std::string>> custom;
                for (const auto& e : S.all_level_files) {
                    if (placed.count(&e)) continue;
                    if (!is_loose_source(e)) continue;
                    if (!placed_paths.insert(norm(e.full_path)).second) continue;
                    std::filesystem::path p = e.full_path;

                    
                    auto region = p.parent_path().parent_path()
                                      .filename().string();
                    std::string label = region.empty()
                        ? e.name
                        : Level::Creation::GetCustomLevelDisplayName(
                              Level::Creation::ResolveGameDataDir(), region);
                    if (!matches_filter(label, e.full_path)) continue;
                    custom.push_back({&e, label});
                    placed.insert(&e);
                }
                if (!custom.empty()) {
                    ImGui::PushStyleColor(ImGuiCol_Text,
                        ImVec4(1.0f, 0.84f, 0.0f, 1.0f));
                    ImGui::TextUnformatted("Custom Levels");
                    ImGui::PopStyleColor();
                    ImGui::Indent(8.0f);
                    for (const auto& [e, label] : custom) {
                        draw_entry(*e, label);
                    }
                    ImGui::Unindent(8.0f);
                    ImGui::Spacing();
                }

                std::vector<std::pair<const FlatAssetEntry*, std::string>> leftover;
                for (const auto& e : S.all_level_files) {
                    if (placed.count(&e)) continue;
                    if (placed_paths.count(norm(e.full_path))) continue;
                    std::filesystem::path p = e.full_path;
                    auto parent = p.parent_path().filename().string();
                    std::string label = parent.empty()
                        ? e.name : parent + " - " + e.name;
                    if (!matches_filter(label, e.full_path)) continue;
                    leftover.push_back({&e, label});
                }
                if (!leftover.empty()) {
                    ImGui::PushStyleColor(ImGuiCol_Text,
                        ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
                    ImGui::TextUnformatted("Uncategorized");
                    ImGui::PopStyleColor();
                    ImGui::Indent(8.0f);
                    for (const auto& [e, label] : leftover) {
                        draw_entry(*e, label);
                    }
                    ImGui::Unindent(8.0f);
                }
            }

            if (!s_delete_level_name.empty() &&
                !ImGui::IsPopupOpen("Delete custom level?")) {
                ImGui::OpenPopup("Delete custom level?");
            }
            if (ImGui::BeginPopupModal("Delete custom level?", nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Delete %s? This permanently removes its "
                            "files from data\\worlds\\albion.",
                            s_delete_level_name.c_str());
                if (ImGui::Button("Delete", ImVec2(120, 0))) {
                    const FlatAssetEntry doomed = s_delete_level_entry;
                    s_delete_level_name.clear();
                    
                    std::string off_msg;
                    LevelEdit::SetEnabled(false, off_msg);
                    LevelEdit::ClearEdits();
                    ContentTabs::CloseLevelByPath(doomed.full_path);
                    std::string derr;
                    if (Level::Creation::DeleteCustomLevel(doomed,
                                                           derr)) {
                        refresh_loose_file_index();
                    } else {
                        OutputLog::error("delete level: " + derr);
                    }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                    s_delete_level_name.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            ImGui::EndChild();
        }
