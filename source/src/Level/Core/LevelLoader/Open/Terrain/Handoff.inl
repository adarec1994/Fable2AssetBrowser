                        g_pending_level_model_body_bnk.clear();
                        if (!res.model_body_bnk.empty()) {
                            auto found_model_bnk =
                                find_bnk_by_virtual_path(res.model_body_bnk);
                            if (!found_model_bnk) {
                                size_t slash =
                                    res.model_body_bnk.find_last_of("/\\");
                                std::string model_leaf =
                                    (slash == std::string::npos)
                                        ? res.model_body_bnk
                                        : res.model_body_bnk.substr(slash + 1);
                                std::transform(model_leaf.begin(),
                                               model_leaf.end(),
                                               model_leaf.begin(), ::tolower);
                                found_model_bnk = find_bnk_by_filename(model_leaf);
                            }
                            if (found_model_bnk) {
                                g_pending_level_model_body_bnk = *found_model_bnk;
                                OutputLog::info("level props: resolved model BNK " +
                                                res.model_body_bnk + " -> " +
                                                std::filesystem::path(*found_model_bnk)
                                                    .filename().string());
                            } else {
                                OutputLog::warn("level props: model BNK not mounted: " +
                                                res.model_body_bnk);
                            }
                        }

                        if (S.cancel_requested.load()) {
                            OutputLog::warn("level load cancelled before handoff to terrain stage");
                            return false;
                        }
                        g_pending_terrain_load =
                            !g_level_export_only_load.load();

                        {
                            auto pal = EhfPalette::Parse(hf.ehf_bytes);
                            if (pal.ok) {
                                std::ostringstream pos;
                                pos << "ehf palette: " << pal.entries.size()
                                    << " ground-texture entr"
                                    << (pal.entries.size() == 1 ? "y" : "ies")
                                    << " @ 0x" << std::hex << pal.palette_offset;
                                OutputLog::info(pos.str());
                                const size_t n_show = std::min<size_t>(pal.entries.size(), 6);
                                for (size_t pi = 0; pi < n_show; ++pi) {
                                    const auto& e = pal.entries[pi];
                                    std::filesystem::path d_p = e.diffuse_path;
                                    std::filesystem::path n_p = e.normal_path;
                                    std::ostringstream l;
                                    l << "  [" << pi << "] tile=" << e.tile_scale
                                      << " int=" << e.intensity
                                      << "  diff=" << d_p.filename().string()
                                      << "  norm=" << n_p.filename().string();
                                    OutputLog::info(l.str());
                                }
                                if (pal.entries.size() > n_show) {
                                    OutputLog::info("  ... (+ "
                                        + std::to_string(pal.entries.size() - n_show)
                                        + " more)");
                                }
                            }
                        }

                    }
                }
            }
        }
    } else {
        OutputLog::warn("no .ehf or .ghf path in level - can't load terrain");
    }

    return true;
}
