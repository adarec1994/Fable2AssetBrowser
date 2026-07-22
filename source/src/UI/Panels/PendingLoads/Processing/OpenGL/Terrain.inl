    if (g_pending_terrain_load.exchange(false)) {
        const Level::TerrainMesh& tm = g_pending_terrain_mesh;
        if (!tm.ok || tm.indices.empty()) {
            OutputLog::error("pending terrain mesh is empty - skipped");
        } else {
            TerrainTextureRegistry::Clear();
            EhfLodThumbnails::Clear();
            TerrainSplat::Clear();
            TerrainEdit::Clear();

            const uint32_t terrain_width = tm.width;
            const uint32_t terrain_height = tm.height;
            const size_t terrain_vertex_count = tm.positions.size() / 3;
            const size_t terrain_index_count = tm.indices.size();
            MDLMeshGeom g;
            g.positions = std::move(tm.positions);
            g.normals = std::move(tm.normals);
            g.uvs = std::move(tm.uvs);
            g.indices = std::move(tm.indices);
            g.name = "terrain";
            g.is_terrain = true;

            MDLInfo info;
            info.MeshCount = 1;
            MDLMeshInfo mi;
            mi.MeshName = "terrain";
            mi.MaterialCount = 0;
            info.Meshes.push_back(mi);
            MDLMeshBufferInfo mb;
            mb.VertexCount = (uint32_t)terrain_vertex_count;
            mb.FaceCount = (uint32_t)terrain_index_count;
            mb.SubMeshCount = 1;
            info.MeshBuffers.push_back(mb);

            std::vector<uint8_t> picked_rgba;
            int picked_w = 0;
            int picked_h = 0;
            std::string picked_label;
            float uv_scale = 1.0f;
            std::string composite_name;
            std::vector<uint8_t> splat_dbg_rgba;
            int splat_dbg_w = 0;
            int splat_dbg_h = 0;

            if (Level::BakeEhfTerrainCompositeAndSplat(
                    g_pending_terrain_ehf_bytes,
                    g_pending_terrain_level_entry.bnk_path,
                    picked_rgba, picked_w, picked_h,
                    composite_name,
                    splat_dbg_rgba, splat_dbg_w, splat_dbg_h)) {
                picked_label = (composite_name == "embedded_tile_albedo")
                    ? std::string("ehf_embedded_tile_albedo")
                    : "ehf_composite[" + composite_name + "]";
                uv_scale = 1.0f;
            } else if (Level::DecodeEhfTerrainAlbedoFromBytes(
                    g_pending_terrain_ehf_bytes,
                    g_pending_terrain_ghf_width > 0
                        ? (uint32_t)g_pending_terrain_ghf_width
                        : g_pending_terrain_mesh.width,
                    g_pending_terrain_ghf_height > 0
                        ? (uint32_t)g_pending_terrain_ghf_height
                        : g_pending_terrain_mesh.height,
                    picked_rgba, picked_w, picked_h)) {
                picked_label = "ehf_baked_albedo";
                uv_scale = 1.0f;
            } else {
                std::vector<uint8_t> pal_rgba;
                int pal_w = 0;
                int pal_h = 0;
                float pal_tile_scale = 0.125f;
                std::string pal_name;
                if (Level::DecodeEhfPaletteFirstDiffuse(
                        g_pending_terrain_ehf_bytes,
                        pal_rgba, pal_w, pal_h,
                        pal_tile_scale, pal_name)) {
                    picked_rgba = std::move(pal_rgba);
                    picked_w = pal_w;
                    picked_h = pal_h;
                    picked_label = "ehf_palette[" + pal_name + "]";
                    uv_scale = 16.0f;
                } else {
                    std::vector<uint8_t> atlas_rgba;
                    int atlas_w = 0;
                    int atlas_h = 0;
                    if (Level::DecodeLevelTextureAtlas(
                            g_pending_terrain_level_entry,
                            atlas_rgba, atlas_w, atlas_h)) {
                        picked_rgba = std::move(atlas_rgba);
                        picked_w = atlas_w;
                        picked_h = atlas_h;
                        picked_label = "texture_atlas_fallback";
                        uv_scale = 32.0f;
                    }
                }
            }

            const bool terrain_space_texture =
                picked_label.rfind("ehf_composite[", 0) == 0 ||
                picked_label == "ehf_embedded_tile_albedo" ||
                picked_label == "ehf_baked_albedo";
            if (terrain_space_texture) {
                normalize_grid_uvs(g, terrain_width, terrain_height);
            } else if (uv_scale != 1.0f) {
                for (float& uv : g.uvs) uv *= uv_scale;
            }

            std::vector<MDLMeshGeom> geoms;
            geoms.push_back(std::move(g));
            for (const auto& adj : g_pending_adjacent_terrain_meshes) {
                const Level::TerrainMesh& am = adj.mesh;
                if (!am.ok || am.indices.empty()) continue;
                MDLMeshGeom ag;
                ag.positions = am.positions;
                ag.normals = am.normals;
                ag.uvs = am.uvs;
                ag.indices = am.indices;
                if (!adj.preserve_mesh_uvs) {
                    normalize_grid_uvs(ag, am.width, am.height);
                }
                ag.name = adj.label.empty()
                    ? std::string("adjacent terrain")
                    : std::string("adjacent terrain: ") + adj.label;
                geoms.push_back(std::move(ag));
            }
            append_level_props_to_geoms(geoms);

            MP_Release(g_mp);
            g_mp_initialized = false;
            g_mp_initialized = MP_Init(g_mp, 800, 600);
            if (g_mp_initialized) {
                MP_Build(geoms, info, g_mp);
                g_mp.no_tilt = true;
                Skybox::PreviewBinding::ApplySkyTheme(
                    g_mp, g_pending_level_sky_theme);
                Skybox::PreviewBinding::ApplyCloudTheme(
                    g_mp, g_pending_level_cloud_theme);
                Skybox::PreviewBinding::ApplyWeatherTheme(
                    g_mp, g_pending_level_weather_theme,
                    g_pending_level_sky_theme);
                Skybox::PreviewBinding::ApplyEnvironmentTimeline(
                    g_mp, g_pending_level_environment_timeline);
                S.terrain_mode = true;
                S.show_model_preview = false;
                S.model_preview_open = false;
                S.model_materials_open = false;

                float minx = 1e30f, miny = 1e30f, minz = 1e30f;
                float maxx = -1e30f, maxy = -1e30f, maxz = -1e30f;
                for (size_t v = 0; v + 2 < geoms[0].positions.size(); v += 3) {
                    const float x = geoms[0].positions[v];
                    const float y = geoms[0].positions[v + 1];
                    const float z = geoms[0].positions[v + 2];
                    if (x < minx) minx = x;  if (x > maxx) maxx = x;
                    if (y < miny) miny = y;  if (y > maxy) maxy = y;
                    if (z < minz) minz = z;  if (z > maxz) maxz = z;
                }
                const float cx_mesh = 0.5f * (minx + maxx);
                const float cy_mesh = 0.5f * (miny + maxy);
                const float cz_mesh = 0.5f * (minz + maxz);
                const float ext_x   = (maxx - minx);
                const float ext_z   = (maxz - minz);
                const float diag    = std::sqrt(ext_x * ext_x + ext_z * ext_z) * 0.5f;
                g_flycam.pos[0] = cx_mesh;
                g_flycam.pos[1] = maxy + diag * 0.7f;
                g_flycam.pos[2] = cz_mesh - diag * 1.0f;
                g_flycam.yaw = 0.0f;
                g_flycam.pitch = -0.6f;
                g_flycam.is_looking = false;
                g_flycam.move_speed = std::max(diag * 0.2f, 50.0f);
                g_mp.radius = std::max(g_mp.radius, diag * 2.0f);
                g_mp.center[0] = cx_mesh;
                g_mp.center[1] = cy_mesh;
                g_mp.center[2] = cz_mesh;

            if (!picked_rgba.empty() && picked_w > 0 && picked_h > 0 &&
                !g_mp.meshes.empty()) {
                    unsigned int terrain_tex =
                        create_gl_texture_from_rgba(picked_w, picked_h,
                                                    picked_rgba.data());
                    if (terrain_tex) {
                        MPPerMesh& m = g_mp.meshes[0];
                        if (m.tex_diffuse && m.tex_diffuse != g_mp.default_tex) {
                            glDeleteTextures(1, &m.tex_diffuse);
                        }
                        m.tex_diffuse = terrain_tex;
                        m.diffuse_visible = true;
                        m.diffuse_tex_name = picked_label;
                        TerrainTextureRegistry::Register(picked_label,
                                                         picked_rgba,
                                                         picked_w,
                                                         picked_h);
                        OutputLog::success("terrain texture bound: " + picked_label +
                                           " (" + std::to_string(picked_w) + "x" +
                                           std::to_string(picked_h) + ")");
                    }
                } else {
                    OutputLog::warn("terrain: no albedo texture decoded");
                }

                OutputLog::success("terrain '" + g_pending_terrain_label +
                                   "' built (" +
                                   std::to_string(geoms[0].positions.size() / 3) +
                                   " verts)");
            }
        }

        g_pending_terrain_mesh = Level::TerrainMesh{};
        g_pending_terrain_label.clear();
        g_pending_terrain_ehf_bytes.clear();
        g_pending_terrain_ehf_bytes.shrink_to_fit();
        g_pending_terrain_lightmap = {};
        g_pending_adjacent_terrain_meshes.clear();
        g_pending_adjacent_terrain_meshes.shrink_to_fit();
        g_pending_terrain_ghf_payload.clear();
        g_pending_terrain_ghf_payload.shrink_to_fit();
        g_pending_terrain_ghf_heights.clear();
        g_pending_terrain_ghf_heights.shrink_to_fit();
        g_pending_terrain_ghf_entry = FlatAssetEntry{};
        g_pending_level_prop_blocks.clear();
        g_pending_level_prop_blocks.shrink_to_fit();
        g_pending_level_model_body_bnk.clear();
    }
