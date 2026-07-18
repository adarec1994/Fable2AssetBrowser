    int n_heightfield_refs = 0;
    int n_logged           = 0;
    const int kMaxLog      = 16;

    std::vector<std::string> all_ehf_refs;
    std::vector<std::string> all_water_refs;

    for (const auto& e : info.entries) {
        if (e.str_a.empty()) continue;

        const bool is_heightfield_like =
            ends_with_ci(e.str_a, ".ehf") ||
            ends_with_ci(e.str_a, ".ghf") ||
            ends_with_ci(e.str_a, ".hdb") ||
            ends_with_ci(e.str_a, ".genv") ||
            ends_with_ci(e.str_a, ".ama")  ||
            ends_with_ci(e.str_a, ".amm")  ||
            ends_with_ci(e.str_a, ".amr")  ||
            ends_with_ci(e.str_a, ".water") ||
            (e.str_a.find("heightfield") != std::string::npos) ||
            (e.str_a.find("Heightfield") != std::string::npos);

        if (is_heightfield_like) {
            ++n_heightfield_refs;
            OutputLog::info("  heightfield ref: t" + std::to_string(e.type)
                            + "  " + e.str_a);
            if (ends_with_ci(e.str_a, ".ehf")) {
                all_ehf_refs.push_back(e.str_a);
            } else if (ends_with_ci(e.str_a, ".water")) {
                all_water_refs.push_back(e.str_a);
            }
        } else if (n_logged < kMaxLog) {
            ++n_logged;
            OutputLog::info("  ref: t" + std::to_string(e.type)
                            + "  " + e.str_a
                            + (e.str_b.empty() ? std::string()
                                               : "  | " + e.str_b));
        }
    }

    if (n_heightfield_refs == 0) {
        OutputLog::warn("level references no .ehf/.ghf/heightfield* strings - "
                        "checking sibling .list file for the heightfield "
                        "names instead.");
    }

    auto sibling_with_ext = [&](const std::string& new_ext) {
        std::filesystem::path p = entry.full_path;
        p.replace_extension(new_ext);
        return p.string();
    };

    auto load_text_sibling = [&](const std::string& sibling_full_path,
                                 std::vector<uint8_t>& out_bytes,
                                 std::string* out_src_bnk = nullptr,
                                 int* out_src_idx = nullptr,
                                 std::string* out_src_file = nullptr) -> bool
    {
        out_bytes.clear();
        if (out_src_bnk) out_src_bnk->clear();
        if (out_src_idx) *out_src_idx = -1;
        if (out_src_file) out_src_file->clear();
        auto normalize_asset_key = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c){ return std::tolower(c); });
            std::replace(s.begin(), s.end(), '\\', '/');
            return s;
        };
        auto filename_of_key = [](const std::string& s) {
            const size_t p = s.find_last_of("/\\");
            return (p == std::string::npos) ? s : s.substr(p + 1);
        };
        auto try_extract = [&](const std::string& bnk_path,
                               int idx) -> bool
        {
            if (bnk_path.empty() || idx < 0) return false;
            try {
                auto v = BnkCache::extract_bytes(bnk_path, idx);
                if (v.empty()) return false;
                out_bytes.assign(v.begin(), v.end());
                if (out_src_bnk) *out_src_bnk = bnk_path;
                if (out_src_idx) *out_src_idx = idx;
                return true;
            } catch (...) {
                return false;
            }
        };
        auto try_bnk_path = [&](const std::string& bnk_path,
                                const std::string& key,
                                const std::string& leaf) -> bool
        {
            int idx = BnkCache::find_index(bnk_path, key);
            if (idx < 0 && !leaf.empty()) {
                idx = BnkCache::find_index(bnk_path, leaf);
            }
            return try_extract(bnk_path, idx);
        };
        auto try_file = [&](const std::filesystem::path& p) -> bool
        {
            std::error_code ec;
            if (!std::filesystem::is_regular_file(p, ec)) return false;
            std::ifstream f(p, std::ios::binary);
            if (!f) return false;
            f.seekg(0, std::ios::end);
            const std::streamoff size = f.tellg();
            if (size <= 0) return false;
            f.seekg(0, std::ios::beg);
            out_bytes.resize(static_cast<size_t>(size));
            f.read(reinterpret_cast<char*>(out_bytes.data()), size);
            if (!f) {
                out_bytes.clear();
                return false;
            }
            if (out_src_file) *out_src_file = p.string();
            return true;
        };

        const std::string key = normalize_asset_key(sibling_full_path);
        const std::string leaf = filename_of_key(key);

        if (try_bnk_path(entry.bnk_path, key, leaf)) {
            return true;
        }

        for (const auto& fe : S.all_heightfield_files) {
            const std::string fe_full =
                normalize_asset_key(fe.full_path.empty()
                    ? fe.name : fe.full_path);
            const std::string fe_name = normalize_asset_key(fe.name);
            const bool match =
                fe_full == key ||
                fe_name == key ||
                (!leaf.empty() &&
                 (filename_of_key(fe_full) == leaf ||
                  filename_of_key(fe_name) == leaf));
            if (!match) continue;
            if (try_extract(fe.bnk_path, fe.file_index)) {
                return true;
            }
        }

        for (const auto& bnk_path : S.bnk_paths) {
            if (bnk_path == entry.bnk_path) continue;
            if (try_bnk_path(bnk_path, key, leaf)) {
                return true;
            }
        }

        if (key.compare(0, 5, "data/") == 0) {
            auto try_iso_file = [&](const std::string& virtual_path) -> bool {
                if (!ISO::IsoMount::instance().is_mounted()) return false;
                std::string vp = virtual_path;
                std::replace(vp.begin(), vp.end(), '\\', '/');
                auto bytes = ISO::IsoMount::instance().read_file(vp);
                if (bytes.empty()) return false;
                out_bytes = std::move(bytes);
                return true;
            };
            if (try_iso_file(key)) {
                return true;
            }

            std::vector<std::filesystem::path> game_roots;
            auto add_game_root = [&](const std::filesystem::path& root) {
                if (root.empty()) return;
                if (ISO::IsoMount::is_iso_path(root.string())) return;
                std::error_code ec;
                const auto abs = std::filesystem::absolute(root, ec);
                const auto candidate = ec ? root : abs;
                for (const auto& existing : game_roots) {
                    if (existing == candidate) return;
                }
                game_roots.push_back(candidate);
            };
            auto add_root_from_path = [&](const std::string& p) {
                if (p.empty()) return;
                if (ISO::IsoMount::is_iso_path(p)) return;
                std::string norm = p;
                std::replace(norm.begin(), norm.end(), '\\', '/');
                std::string low = norm;
                std::transform(low.begin(), low.end(), low.begin(),
                               [](unsigned char c){ return std::tolower(c); });
                const size_t data_pos = low.find("/data/");
                if (data_pos != std::string::npos) {
                    add_game_root(norm.substr(0, data_pos));
                }
            };
            add_game_root(S.root_dir);
            add_root_from_path(entry.bnk_path);
            for (const auto& bnk_path : S.bnk_paths) {
                add_root_from_path(bnk_path);
            }
            for (const auto& bnk_path : S.nested_bnk_paths) {
                add_root_from_path(bnk_path);
            }

            const std::string without_data = key.substr(5);
            for (const auto& root : game_roots) {
                if (try_file(root / std::filesystem::path(key))) {
                    return true;
                }
                if (try_file(root / "data" /
                             std::filesystem::path(without_data))) {
                    return true;
                }
                if (lower_slash(root.string()).size() >= 4 &&
                    lower_slash(root.string()).compare(
                        lower_slash(root.string()).size() - 4, 4,
                        "data") == 0 &&
                    try_file(root / std::filesystem::path(without_data))) {
                    return true;
                }
            }
        }

        if (!leaf.empty()) {
            std::error_code ec;
            const auto cwd = std::filesystem::current_path(ec);
            if (!ec) {
                if (try_file(cwd / "extracted" / leaf)) return true;
                if (try_file(cwd / "cmake-build-debug" / "extracted" / leaf))
                    return true;
                if (try_file(cwd.parent_path() / "cmake-build-debug" /
                             "extracted" / leaf))
                    return true;
            }
        }

        return false;
    };

    LevelResources res;
    {
        std::vector<uint8_t> list_bytes;
        const std::string list_path = sibling_with_ext(".list");
        if (load_text_sibling(list_path, list_bytes)) {
            std::string list_str(reinterpret_cast<const char*>(list_bytes.data()),
                                 list_bytes.size());
            std::ostringstream ls; ls << "list (" << list_bytes.size() << " bytes):";
            OutputLog::info(ls.str());

            size_t pos = 0;
            while (pos < list_str.size()) {
                size_t eol = list_str.find_first_of("\r\n", pos);
                std::string line = (eol == std::string::npos)
                                       ? list_str.substr(pos)
                                       : list_str.substr(pos, eol - pos);
                pos = (eol == std::string::npos)
                          ? list_str.size()
                          : list_str.find_first_not_of("\r\n", eol);
                if (pos == std::string::npos) pos = list_str.size();
                if (line.empty()) continue;

                std::string low = line;
                std::transform(low.begin(), low.end(), low.begin(),
                               [](unsigned char c){ return std::tolower(c); });
                auto matches = [&](const char* ext) {
                    size_t n = std::strlen(ext);
                    return low.size() >= n &&
                           low.compare(low.size() - n, n, ext) == 0;
                };
                if      (matches(".ehf"))  res.ehf_path  = line;
                else if (matches(".ghf"))  res.ghf_path  = line;
                else if (matches(".hdb"))  res.hdb_path  = line;
                else if (matches(".genv")) res.genv_path = line;
                else if (matches(".ama"))  res.ama_path  = line;
                else if (matches(".amm"))  res.amm_path  = line;
                else if (matches(".amr"))  res.amr_path  = line;
                else if (matches("_models.bnk")) res.model_body_bnk = line;

                OutputLog::info("  " + line);
            }
        } else {
            OutputLog::warn("no companion .list (" + list_path + ") in BNK");
        }
    }

    auto basename_no_ext = [](const std::string& p) -> std::string {
        size_t slash = p.find_last_of("/\\");
        std::string s = (slash == std::string::npos)
            ? p
            : p.substr(slash + 1);
        auto dot = s.find_last_of('.');
        if (dot != std::string::npos) s.resize(dot);
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        return s;
    };
    if (res.ehf_path.empty() && !all_ehf_refs.empty()) {
        const std::string ghf_base = basename_no_ext(res.ghf_path);
        for (const auto& candidate : all_ehf_refs) {
            if (!ghf_base.empty() &&
                basename_no_ext(candidate) == ghf_base) {
                res.ehf_path = candidate;
                break;
            }
        }
        if (res.ehf_path.empty()) res.ehf_path = all_ehf_refs.front();
    }

    auto report_slot = [](const char* label, const std::string& v) {
        if (v.empty()) {
            OutputLog::warn(std::string("  ") + label + ": (missing)");
        } else {
            OutputLog::success(std::string("  ") + label + ": " + v);
        }
    };
    OutputLog::info("heightfield resources for this level:");
    report_slot(".ehf  (graphics desc)", res.ehf_path);
    report_slot(".ghf  (raw heightmap)", res.ghf_path);
    report_slot(".hdb  (height database)", res.hdb_path);
    report_slot(".genv (env table)",     res.genv_path);
    report_slot(".ama  (ambient)",       res.ama_path);
    report_slot(".amm  (ambient meta)",  res.amm_path);
    report_slot(".amr  (ambient refs)",  res.amr_path);
    report_slot("models",                res.model_body_bnk);

