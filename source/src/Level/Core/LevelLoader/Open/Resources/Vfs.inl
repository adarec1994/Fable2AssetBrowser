    {
        struct SiblingSlot { const char* label; const std::string& path; };
        const SiblingSlot slots[] = {
            { ".hdb  (height database)", res.hdb_path  },
            { ".genv (env table)",       res.genv_path },
            { ".ama  (ambient)",         res.ama_path  },
            { ".amm  (ambient meta)",    res.amm_path  },
            { ".amr  (ambient refs)",    res.amr_path  },
        };
        OutputLog::info("loading .list terrain siblings:");
        for (const auto& s : slots) {
            if (s.path.empty()) continue;
            std::vector<uint8_t> bytes;
            if (load_text_sibling(s.path, bytes)) {
                std::ostringstream os;
                os << "  " << s.label << " loaded (" << bytes.size() << " bytes)";
                OutputLog::success(os.str());
            } else {
                OutputLog::warn(std::string("  ") + s.label + " load FAILED: " + s.path);
            }
        }
    }

    g_level_havok_collision.clear();
    OutputLog::info("havok_scenario loading disabled");
    if (bail_if_cancelled("after terrain siblings")) return false;

    g_level_vfs_texture_body_bnks.clear();
    g_level_vfs_model_bnks.clear();
    g_level_vfs_streaming_bnks.clear();
    {
        std::vector<uint8_t> vfs_bytes;
        std::filesystem::path vfs_path = entry.full_path;
        vfs_path.replace_filename("level.vfsconfig");
        if (load_text_sibling(vfs_path.string(), vfs_bytes)) {
            auto vfs = Level::ParseVfsConfig(vfs_bytes);
            g_level_vfs_texture_body_bnks = std::move(vfs.texture_body_bnks);
            g_level_vfs_model_bnks        = std::move(vfs.model_bnks);
            g_level_vfs_streaming_bnks    = std::move(vfs.streaming_bnks);
            std::ostringstream os;
            os << "vfsconfig: "
               << g_level_vfs_texture_body_bnks.size() << " texture body BNKs, "
               << g_level_vfs_model_bnks.size() << " model BNKs, "
               << g_level_vfs_streaming_bnks.size() << " streaming BNKs";
            OutputLog::info(os.str());
            for (const auto& p : g_level_vfs_texture_body_bnks) {
                OutputLog::info("  tex-body: " + p);
            }
            for (const auto& p : g_level_vfs_model_bnks) {
                OutputLog::info("  model:    " + p);
            }
            for (const auto& p : g_level_vfs_streaming_bnks) {
                OutputLog::info("  stream:   " + p);
            }
        } else {
            OutputLog::warn("no level.vfsconfig sibling in BNK");
        }
    }
    if (bail_if_cancelled("after vfsconfig")) return false;
    const std::vector<StreamingModelCandidate> streaming_model_candidates =
        collect_streaming_model_candidates(g_level_vfs_streaming_bnks);
    if (!streaming_model_candidates.empty()) {
        size_t indexed = 0;
        for (const auto& c : streaming_model_candidates) {
            if (c.entry) ++indexed;
        }
        OutputLog::info("streaming model candidates: " +
                        std::to_string(streaming_model_candidates.size()) +
                        " streaming hint path(s), " + std::to_string(indexed) +
                        " resolved through global .mdl index");
    }
