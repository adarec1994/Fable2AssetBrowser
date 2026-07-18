bool Open(const FlatAssetEntry& entry)
{
    DebugLog::Scope debug_scope("Load level", entry.name + " | " +
                                entry.full_path);
    if (!g_level_export_only_load.load()) {
        OutputLog::info("loading level '" + entry.name + "' ...");
    }
    loader_progress_update(8, 100, "Extracting " + entry.name);

    auto bail_if_cancelled = [&](const char* where) -> bool {
        if (!S.cancel_requested.load()) return false;
        OutputLog::warn(std::string("level load cancelled at ") + where);
        return true;
    };

    g_pending_level_prop_blocks.clear();
    g_pending_adjacent_terrain_meshes.clear();
    g_pending_level_water_theme = Gdb::WaterTheme{};
    g_pending_level_sky_theme = Gdb::SkyTheme{};
    g_pending_level_cloud_theme = Gdb::CloudTheme{};
    g_pending_level_weather_theme = Gdb::WeatherTheme{};
    g_pending_level_environment_timeline =
        Gdb::EnvironmentThemeTimeline{};

    if (bail_if_cancelled("entry")) return false;

    std::vector<uint8_t> bytes;
    try {
        bytes = BnkCache::extract_bytes(entry.bnk_path, entry.file_index);
    } catch (const std::exception& ex) {
        OutputLog::error("level extract failed: " + std::string(ex.what()));
        return false;
    } catch (...) {
        OutputLog::error("level extract failed (unknown exception)");
        return false;
    }
    if (bytes.empty()) {
        OutputLog::error("level extract produced 0 bytes");
        return false;
    }
    if (bail_if_cancelled("after-extract")) return false;

    loader_progress_update(18, 100, "Parsing level entries...");
    EngineLevelInfo info;
    if (!ParseEngineLevel(bytes, info)) {
        OutputLog::error("parse failed for '" + entry.name + "': "
                         + info.error);
        return false;
    }
    if (bail_if_cancelled("after-parse")) return false;

    info.source_path = entry.full_path;
    LevelEdit::OnLevelLoaded(entry);
    int n_t2 = 0, n_t4 = 0, n_t5 = 0, n_t21 = 0, n_t32 = 0, n_other = 0;
    for (const auto& e : info.entries) {
        switch (e.type) {
            case 2:  ++n_t2;  break;
            case 4:  ++n_t4;  break;
            case 5:  ++n_t5;  break;
            case 21: ++n_t21; break;
            case 32: ++n_t32; break;
            default: ++n_other; break;
        }
    }

    std::ostringstream os;
    os << "level OK  ver=" << info.version
       << "  entries=" << info.entries.size()
       << "/" << info.entry_count
       << "  (t2=" << n_t2
       << " t4=" << n_t4
       << " t5=" << n_t5
       << " t21=" << n_t21
       << " t32=" << n_t32
       << " other=" << n_other << ")";
    OutputLog::success(os.str());

    size_t t2_prop_blocks = 0, t2_prop_instances = 0;
    size_t t21_prop_blocks = 0, t21_prop_instances = 0;
    size_t other_prop_blocks = 0, other_prop_instances = 0;
    for (const auto& b : info.prop_blocks) {
        if (b.type == 2) {
            ++t2_prop_blocks;
            t2_prop_instances += b.instances.size();
        } else if (b.type == 21) {
            ++t21_prop_blocks;
            t21_prop_instances += b.instances.size();
        } else {
            ++other_prop_blocks;
            other_prop_instances += b.instances.size();
        }
    }
    std::ostringstream ps;
    ps << "level prop placements: "
       << "t2=" << t2_prop_blocks << " blocks / " << t2_prop_instances << " instances, "
       << "t21=" << t21_prop_blocks << " blocks / " << t21_prop_instances << " instances";
    if (other_prop_blocks > 0) {
        ps << ", other=" << other_prop_blocks << " blocks / "
           << other_prop_instances << " instances";
    }
    OutputLog::info(ps.str());

    std::unordered_set<std::string> authored_level_model_paths;
    authored_level_model_paths.reserve(info.prop_blocks.size() * 2);
    for (const auto& block : info.prop_blocks) {
        if (!block.model_path.empty()) {
            authored_level_model_paths.insert(lower_slash(block.model_path));
        }
        if (!block.lod_model_path.empty()) {
            authored_level_model_paths.insert(
                lower_slash(block.lod_model_path));
        }
    }

    {
        const std::vector<std::string> wanted = {
            "bridge", "lamp", "lantern", "fence", "bench", "post",
            "archway", "gate", "stall", "shop", "wall"
        };
        std::map<std::string, std::pair<size_t, size_t>> match_counts;
        std::map<std::string, std::vector<std::string>> match_paths;
        for (const auto& pb : info.prop_blocks) {
            std::string p = pb.model_path;
            std::transform(p.begin(), p.end(), p.begin(), ::tolower);
            for (const auto& kw : wanted) {
                if (p.find(kw) != std::string::npos) {
                    match_counts[kw].first += 1;
                    match_counts[kw].second += pb.instances.size();
                    if (match_paths[kw].size() < 3) {
                        match_paths[kw].push_back(pb.model_path);
                    }
                    break;
                }
            }
        }
        OutputLog::info("engine_level keyword scan (bridge/lamp/fence/...):");
        for (const auto& kw : wanted) {
            auto it = match_counts.find(kw);
            if (it == match_counts.end()) {
                OutputLog::warn("  " + kw + ":  NONE in engine_level");
            } else {
                std::ostringstream os;
                os << "  " << kw << ":  " << it->second.first
                   << " blocks / " << it->second.second << " instances";
                OutputLog::success(os.str());
                for (const auto& path : match_paths[kw]) {
                    OutputLog::info("    " + path);
                }
            }
        }
    }

    auto ends_with_ci = [](const std::string& s, const char* suffix) {
        size_t n = std::strlen(suffix);
        if (s.size() < n) return false;
        for (size_t i = 0; i < n; ++i) {
            char a = s[s.size() - n + i];
            char b = suffix[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) return false;
        }
        return true;
    };
