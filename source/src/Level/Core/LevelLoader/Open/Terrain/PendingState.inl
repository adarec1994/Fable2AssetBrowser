                        g_pending_terrain_ghf_payload   = hf.ghf_bytes_raw;
                        g_pending_terrain_ghf_heights =
                            std::move(hg.heights);
                        g_pending_terrain_ghf_tile_size = hg.tile_size;
                        g_pending_terrain_ghf_width     = (int)hg.width;
                        g_pending_terrain_ghf_height    = (int)hg.height;
                        {
                            const FlatAssetEntry* fe =
                                Level::FindHeightfieldByPath(res.ghf_path);
                            g_pending_terrain_ghf_entry =
                                fe ? *fe : FlatAssetEntry{};
                        }

                        {
                            std::vector<Level::PropBlock> hkx_blocks =
                                std::move(g_pending_level_prop_blocks);
                            g_pending_level_prop_blocks = info.prop_blocks;
                            g_pending_level_prop_blocks.insert(
                                g_pending_level_prop_blocks.end(),
                                std::make_move_iterator(hkx_blocks.begin()),
                                std::make_move_iterator(hkx_blocks.end()));
                        }
