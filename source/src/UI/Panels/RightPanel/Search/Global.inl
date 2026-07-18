    float available_width = ImGui::GetContentRegionAvail().x;
    float field_width = (available_width - 8.0f) * 0.5f;

    ImGui::SetNextItemWidth(field_width);
    const char* filter_hint = "Filter Current BNK";
    if (S.viewing_adb) filter_hint = "Filter ADB Files";
    else if (S.viewing_lua) filter_hint = "Filter Lua Scripts";
    ImGui::InputTextWithHint("##file_filter", filter_hint, &S.file_filter);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(field_width);
    bool search_changed = ImGui::InputTextWithHint("##global_search", "Search All BNKs", &S.global_search);

    if (S.global_search != g_last_global_search) {
        g_last_global_search = S.global_search;
        g_global_hits.clear();
        g_selected_global = -1;

        if (!S.global_search.empty()) {
            S.viewing_adb = false;
            if (!g_global_busy) {
                g_global_busy = true;
                std::string search_term = S.global_search;

                std::thread([search_term]() {
                    std::vector<GlobalHit> local_hits;
                    std::string needle = search_term;
                    std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);

                    auto is_header_bnk = [](const std::string& bnk_path) -> bool {
                        std::string lower_path = bnk_path;
                        std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(), ::tolower);
                        std::string filename = std::filesystem::path(lower_path).filename().string();
                        return filename.find("header") != std::string::npos;
                    };

                    auto is_nested_bnk = [](const std::string& filename) -> bool {
                        std::string lower = filename;
                        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                        return lower.size() >= 4 && lower.substr(lower.size() - 4) == ".bnk";
                    };

                    try {
                        for (const auto& bnk_path : S.bnk_paths) {
                            if (is_header_bnk(bnk_path)) {
                                continue;
                            }

                            BNKReader reader(bnk_path);
                            const auto& files = reader.list_files();

                            for (size_t i = 0; i < files.size(); ++i) {
                                std::string fname = files[i].name;
                                std::string fname_lower = fname;
                                std::transform(fname_lower.begin(), fname_lower.end(), fname_lower.begin(), ::tolower);

                                if (fname_lower.find(needle) != std::string::npos) {
                                    local_hits.push_back({
                                        bnk_path,
                                        fname,
                                        (int)i,
                                        files[i].uncompressed_size
                                    });
                                }

                                if (is_nested_bnk(fname)) {
                                    try {
                                        auto tmpdir = std::filesystem::temp_directory_path() / "f2_global_search_nested";
                                        std::error_code ec;
                                        std::filesystem::create_directories(tmpdir, ec);

                                        std::string temp_name = "search_nested_" + std::to_string(std::hash<std::string>{}(bnk_path + fname)) + ".bnk";
                                        auto temp_bnk_path = tmpdir / temp_name;

                                        extract_one(bnk_path, (int)i, temp_bnk_path.string());

                                        BNKReader nested_reader(temp_bnk_path.string());
                                        const auto& nested_files = nested_reader.list_files();

                                        size_t fname_last_slash = fname.find_last_of('/');
                                        std::string prefix = (fname_last_slash == std::string::npos)
                                            ? std::string()
                                            : fname.substr(0, fname_last_slash + 1);

                                        for (size_t j = 0; j < nested_files.size(); ++j) {
                                            const auto& nested_file = nested_files[j];
                                            std::string nested_fname = prefix + nested_file.name;
                                            std::string nested_fname_lower = nested_fname;
                                            std::transform(nested_fname_lower.begin(), nested_fname_lower.end(), nested_fname_lower.begin(), ::tolower);

                                            if (nested_fname_lower.find(needle) != std::string::npos) {
                                                local_hits.push_back({
                                                    temp_bnk_path.string(),
                                                    nested_fname,
                                                    (int)j,
                                                    nested_files[j].uncompressed_size
                                                });
                                            }
                                        }
                                    } catch (...) {}
                                }
                            }
                        }
                    } catch (...) {}

                    g_global_hits = std::move(local_hits);
                    g_global_busy = false;
                }).detach();
            }
        }
    }

    if (!S.hide_tooltips && ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted("Type to search across all BNK files");
        ImGui::EndTooltip();
    }
