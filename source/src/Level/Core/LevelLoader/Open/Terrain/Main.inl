    if (bail_if_cancelled("pre-heightfield")) return false;

    if (!res.ehf_path.empty() || !res.ghf_path.empty()) {
        HeightfieldFiles hf;
        loader_progress_update(32, 100, "Loading heightfield files...");
        if (!LoadHeightfieldFiles(res.ehf_path, res.ghf_path,
                                  res.hdb_path, res.genv_path, hf)) {
            OutputLog::error("heightfield load failed: " + hf.error);
        } else if (S.cancel_requested.load()) {
            OutputLog::warn("level load cancelled during heightfield load");
            return false;
        } else {
            std::ostringstream hos;
            hos << "heightfield loaded:"
                << "  ehf=" << hf.ehf_bytes.size() << "B"
                << "  ghf=" << hf.ghf_bytes_compressed.size() << "B (gz)"
                << " -> " << hf.ghf_bytes_raw.size() << "B (raw)";
            OutputLog::success(hos.str());

            if (!hf.ghf_bytes_raw.empty()) {
                GhfHeights hg;
                loader_progress_update(45, 100, "Decoding height grid...");
                if (!DecodeGhfHeights(hf.ghf_bytes_raw, hg)) {
                    OutputLog::error("  .ghf decode failed: " + hg.error);
                } else {
                    if (hg.tile_size <= 0.0f) {
                        const float ehf_tile = hf.ehf_header.ok
                                             ? hf.ehf_header.f2 : 0.0f;
                        const float fallback =
                            (ehf_tile > 0.0f && std::isfinite(ehf_tile))
                                ? ehf_tile : 0.5f;
                        std::ostringstream tos;
                        tos << "  .ghf tile_size was 0 - using .ehf f2 = "
                            << fallback << " (world = "
                            << (hg.width  - 1) * fallback << " x "
                            << (hg.height - 1) * fallback << ")";
                        OutputLog::info(tos.str());
                        hg.tile_size = fallback;
                    }

                    std::ostringstream gos;
                    gos << "  .ghf heightmap: " << hg.width << "x" << hg.height
                        << "  tile=" << hg.tile_size
                        << "  h=[" << hg.min_height << ".." << hg.max_height << "]";
                    OutputLog::success(gos.str());

                    TerrainMesh mesh;
                    loader_progress_update(58, 100, "Building terrain mesh...");
                    if (S.cancel_requested.load()) {
                        OutputLog::warn("level load cancelled before terrain mesh build");
                        return false;
                    }
#ifdef _WIN32
                    const bool terrain_built = BuildTerrainMesh(hg, mesh);
#else
                    constexpr size_t kLinuxTerrainPreviewVertices = 262144;
                    const size_t full_vertex_count =
                        size_t(hg.width) * size_t(hg.height);
                    Level::GhfHeights preview_hg;
                    const Level::GhfHeights* render_hg = &hg;
                    if (full_vertex_count > kLinuxTerrainPreviewVertices) {
                        preview_hg = make_linux_preview_heightfield(
                            hg, kLinuxTerrainPreviewVertices);
                        render_hg = &preview_hg;
                    }
                    const bool terrain_built =
                        BuildTerrainMesh(*render_hg, mesh);
#endif
                    if (!terrain_built) {
                        OutputLog::error("  terrain mesh build failed");
                    } else {
                        const size_t tri_count = mesh.indices.size() / 3;
                        std::ostringstream mos;
                        mos << "  terrain mesh: verts=" << (mesh.positions.size() / 3)
                            << "  tris=" << tri_count;
                        OutputLog::success(mos.str());

                        g_pending_terrain_mesh        = std::move(mesh);
                        g_pending_terrain_label       = entry.name;
                        g_pending_terrain_level_entry = entry;
                        g_pending_terrain_ehf_bytes   = hf.ehf_bytes;
                        g_pending_adjacent_terrain_meshes.clear();
