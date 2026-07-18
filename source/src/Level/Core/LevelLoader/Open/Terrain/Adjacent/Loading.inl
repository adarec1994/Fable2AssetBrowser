                        const std::string main_ehf_norm = norm_path(res.ehf_path);
                        for (const auto& adj_ehf_path : all_ehf_refs) {
                            if (norm_path(adj_ehf_path) == main_ehf_norm) continue;

                            HeightfieldFiles adj_hf;
                            if (!LoadHeightfieldFiles(adj_ehf_path, {},
                                                      {}, {}, adj_hf)) {
                                OutputLog::warn("adjacent terrain load failed: " +
                                                adj_ehf_path + " (" + adj_hf.error + ")");
                                continue;
                            }

                            if (S.cancel_requested.load()) {
                                OutputLog::warn("level load cancelled during adjacent terrain loop");
                                return false;
                            }

                            TerrainMesh adj_mesh;
                            std::vector<Level::VistaPatchGeom> adj_patch_geoms;
                            bool used_vista_mesh = false;
                            bool used_ehf_render_mesh = false;
                            {
                                std::string vista_stats;
                                if (BuildEhfVistaPatchMesh(
                                        adj_hf.ehf_bytes, adj_mesh,
                                        &vista_stats)) {
                                    used_vista_mesh = true;
                                    OutputLog::info("adjacent terrain using "
                                                    ".ehf bg patches: " +
                                                    adj_ehf_path + " (" +
                                                    vista_stats + ")");

                                    std::string geom_stats;
                                    if (BuildEhfVistaPatchGeoms(
                                            adj_hf.ehf_bytes, adj_patch_geoms,
                                            &geom_stats)) {
                                        OutputLog::info(
                                            "adjacent terrain per-patch geoms: " +
                                            geom_stats);
                                    }
                                }
                            }

                            if (!adj_mesh.ok) {
                                const std::string adj_ghf_path =
                                    resolve_adjacent_ghf(adj_ehf_path);
                                HeightfieldFiles adj_ghf;
                                if (!adj_ghf_path.empty() &&
                                    LoadHeightfieldFiles({}, adj_ghf_path,
                                                         {}, {}, adj_ghf) &&
                                    !adj_ghf.ghf_bytes_raw.empty()) {
                                    GhfHeights adj_hg;
                                    if (!DecodeGhfHeights(adj_ghf.ghf_bytes_raw,
                                                          adj_hg)) {
                                        OutputLog::warn("adjacent terrain .ghf decode failed: " +
                                                        adj_ghf_path + " (" + adj_hg.error + ")");
                                    } else {
                                        if (adj_hg.tile_size <= 0.0f) {
                                            const float ehf_tile = adj_hf.ehf_header.ok
                                                ? adj_hf.ehf_header.f2 : 0.0f;
                                            adj_hg.tile_size =
                                                (ehf_tile > 0.0f &&
                                                 std::isfinite(ehf_tile))
                                                    ? ehf_tile : hg.tile_size;
                                        }
#ifdef _WIN32
                                        const bool adjacent_built =
                                            BuildTerrainMesh(adj_hg, adj_mesh);
#else
                                        constexpr size_t
                                            kLinuxAdjacentPreviewVertices =
                                                65536;
                                        const size_t adjacent_vertex_count =
                                            size_t(adj_hg.width) *
                                            size_t(adj_hg.height);
                                        Level::GhfHeights preview_adj_hg;
                                        const Level::GhfHeights* render_adj_hg =
                                            &adj_hg;
                                        if (adjacent_vertex_count >
                                            kLinuxAdjacentPreviewVertices) {
                                            preview_adj_hg =
                                                make_linux_preview_heightfield(
                                                    adj_hg,
                                                    kLinuxAdjacentPreviewVertices);
                                            render_adj_hg = &preview_adj_hg;
                                        }
                                        const bool adjacent_built =
                                            BuildTerrainMesh(*render_adj_hg,
                                                             adj_mesh);
#endif
                                        if (!adjacent_built) {
                                            OutputLog::warn("adjacent terrain mesh build failed: " +
                                                            adj_ehf_path);
                                        }
                                    }
                                }
                                if (adj_mesh.ok) {
                                    EhfParsedBody adj_body;
                                    if (ParseEhfBody(adj_hf.ehf_bytes, adj_body) &&
                                        !adj_body.chunks.empty()) {
                                        float min_x = 1e30f, min_z = 1e30f;
                                        for (const auto& c : adj_body.chunks) {
                                            min_x = std::min(min_x, c.origin[0]);
                                            min_z = std::min(min_z, c.origin[1]);
                                        }
                                        if (std::isfinite(min_x) &&
                                            std::isfinite(min_z) &&
                                            (std::fabs(min_x) > 1e-4f ||
                                             std::fabs(min_z) > 1e-4f)) {
                                            for (size_t pi = 0;
                                                 pi + 2 < adj_mesh.positions.size();
                                                 pi += 3) {
                                                adj_mesh.positions[pi + 0] += min_x;
                                                adj_mesh.positions[pi + 2] += min_z;
                                            }
                                        }
                                    }
                                    OutputLog::info("adjacent terrain using .ghf "
                                                    "grid (no bg patches): " +
                                                    adj_ehf_path);
                                }
                            }
                            if (!adj_mesh.ok) {
                                std::string render_stats;
                                if (BuildEhfRenderStripMesh(
                                        adj_hf.ehf_bytes, adj_mesh,
                                        &render_stats)) {
                                    used_ehf_render_mesh = true;
                                    OutputLog::info("adjacent terrain using "
                                                    ".ehf render mesh: " +
                                                    adj_ehf_path + " (" +
                                                    render_stats + ")");
                                } else if (build_ehf_proxy_mesh(adj_hf,
                                                                 hg.min_height,
                                                                 adj_mesh)) {
                                    OutputLog::info("adjacent terrain using .ehf "
                                                    "chunk proxy (last resort): " +
                                                    adj_ehf_path);
                                } else {
                                    OutputLog::info("adjacent terrain skipped "
                                                    "(no usable .ghf/.ehf mesh): " +
                                                    adj_ehf_path);
                                    continue;
                                }
                            }

                            Level::PendingAdjacentTerrain adj;
                            adj.label = std::filesystem::path(adj_ehf_path)
                                            .filename().string();
                            adj.preferred_bnk = g_pending_terrain_level_entry.bnk_path;
                            adj.ehf_bytes = std::move(adj_hf.ehf_bytes);
                            adj.mesh = std::move(adj_mesh);
                            adj.patch_geoms = std::move(adj_patch_geoms);
                            adj.preserve_mesh_uvs =
                                used_vista_mesh || used_ehf_render_mesh;
                            adj.prefer_embedded_albedo = true;
                            g_pending_adjacent_terrain_meshes.push_back(std::move(adj));
                        }
                        if (!g_pending_adjacent_terrain_meshes.empty()) {
                            OutputLog::success("adjacent terrain meshes loaded: " +
                                std::to_string(g_pending_adjacent_terrain_meshes.size()));
                        }
