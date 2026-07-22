    if (g_pending_terrain_load.exchange(false)) {
        if (S.cancel_requested.load()) {
            OutputLog::warn("terrain stage skipped: cancel requested");
            g_pending_terrain_mesh = Level::TerrainMesh{};
            g_pending_terrain_label.clear();
            g_pending_terrain_ehf_bytes.clear();
            g_pending_terrain_lightmap = {};
            g_pending_adjacent_terrain_meshes.clear();
            g_pending_terrain_ghf_payload.clear();
            g_pending_terrain_ghf_heights.clear();
            g_pending_terrain_ghf_entry = FlatAssetEntry{};
            g_pending_level_prop_blocks.clear();
            g_pending_level_model_body_bnk.clear();
            g_pending_level_water_present = false;
            g_pending_level_water_scene = Level::WaterScene{};
            g_pending_level_water_theme = Gdb::WaterTheme{};
            g_pending_level_sky_theme = Gdb::SkyTheme{};
            g_pending_level_cloud_theme = Gdb::CloudTheme{};
            g_pending_level_weather_theme = Gdb::WeatherTheme{};
            g_pending_level_environment_timeline =
                Gdb::EnvironmentThemeTimeline{};
            progress_done();
            S.cancel_requested.store(false);
            return;
        }
        progress_update(72, 100, "Uploading terrain...");
        Level::TerrainMesh& tm = g_pending_terrain_mesh;
        if (!tm.ok || tm.indices.empty()) {
            OutputLog::error("pending terrain mesh is empty - skipped");
        } else {
            TerrainTextureRegistry::Clear();
            EhfLodThumbnails::Clear();
            TerrainSplat::Clear();
            TerrainEdit::Clear();
            TerrainPaint::Clear();
            if (!g_mp_initialized) {
                MP_Init(device, g_mp, 800, 600);
                g_mp_initialized = true;
            }

            MDLMeshGeom g;
            g.positions    = tm.positions;
            g.normals      = tm.normals;
            g.uvs          = tm.uvs;
            g.indices      = tm.indices;
            g.bone_ids.assign(tm.positions.size() / 3 * 4, 0);
            g.bone_weights.assign(tm.positions.size() / 3 * 4, 0.f);
            for (size_t v = 0; v < tm.positions.size() / 3; ++v) {
                g.bone_weights[v * 4 + 0] = 1.0f;
            }
            g.name = "terrain";
            g.is_terrain = true;

            MDLInfo info;
            info.MeshCount = 1;
            MDLMeshInfo mi;
            mi.MeshName       = "terrain";
            mi.MaterialCount  = 0;
            info.Meshes.push_back(mi);
            MDLMeshBufferInfo mb;
            mb.VertexCount  = (uint32_t)(tm.positions.size() / 3);
            mb.FaceCount    = (uint32_t)tm.indices.size();
            mb.SubMeshCount = 1;
            info.MeshBuffers.push_back(mb);

            std::vector<uint8_t> picked_rgba;
            int picked_w = 0, picked_h = 0;
            std::vector<uint8_t> picked_normal_rgba;
            int picked_normal_w = 0, picked_normal_h = 0;
            std::string picked_label;
            float uv_scale = 1.0f;
            std::vector<GeneratedTerrainTexture> generated_terrain_textures;

            std::string composite_name;
            std::vector<uint8_t> splat_dbg_rgba;
            int splat_dbg_w = 0, splat_dbg_h = 0;
            progress_update(74, 100, "Baking terrain textures...");
            
            
            
            const bool custom_blank_terrain =
                Level::Creation::IsCustomLooseLevel(
                    g_pending_terrain_level_entry);
            if (custom_blank_terrain) {
                OutputLog::info(
                    "terrain: custom level - terrain textures cleared");
                TerrainPaint::InitForLevel(
                    g_pending_terrain_level_entry.full_path,
                    (int)tm.width, (int)tm.height,
                    g_pending_terrain_ghf_tile_size > 0.0f
                        ? g_pending_terrain_ghf_tile_size
                        : 0.5f);
            } else if (Level::BakeEhfTerrainCompositeAndSplat(
                    g_pending_terrain_ehf_bytes,
                    g_pending_terrain_level_entry.bnk_path,
                    picked_rgba, picked_w, picked_h,
                    composite_name,
                    splat_dbg_rgba, splat_dbg_w, splat_dbg_h))
            {
                picked_label = (composite_name == "embedded_tile_albedo")
                    ? std::string("ehf_embedded_tile_albedo")
                    : "ehf_composite[" + composite_name + "]";
                uv_scale     = 1.0f;
            } else if (Level::DecodeEhfTerrainAlbedoFromBytes(
                    g_pending_terrain_ehf_bytes,
                    g_pending_terrain_mesh.width,
                    g_pending_terrain_mesh.height,
                    picked_rgba, picked_w, picked_h))
            {
                picked_label = "ehf_baked_albedo";
                uv_scale     = 1.0f;
            } else {
                std::vector<uint8_t> pal_rgba;
                int pal_w = 0, pal_h = 0;
                float pal_tile_scale = 0.125f;
                std::string pal_name;
                if (Level::DecodeEhfPaletteFirstDiffuse(
                        g_pending_terrain_ehf_bytes,
                        pal_rgba, pal_w, pal_h,
                        pal_tile_scale, pal_name))
                {
                    picked_rgba  = std::move(pal_rgba);
                    picked_w     = pal_w;
                    picked_h     = pal_h;
                    picked_label = "ehf_palette[" + pal_name + "]";
                    uv_scale = 16.0f;
                } else {
                    std::vector<uint8_t> atlas_rgba;
                    int atlas_w = 0, atlas_h = 0;
                    if (Level::DecodeLevelTextureAtlas(
                            g_pending_terrain_level_entry,
                            atlas_rgba, atlas_w, atlas_h))
                    {
                        picked_rgba  = std::move(atlas_rgba);
                        picked_w     = atlas_w;
                        picked_h     = atlas_h;
                        picked_label = "texture_atlas_fallback";
                        uv_scale     = 32.0f;
                    }
                }
            }

            if (!picked_rgba.empty() && picked_w > 0 && picked_h > 0) {
                GeneratedTerrainTexture gt;
                gt.mesh_index = 0;
                gt.label      = picked_label;
                gt.rgba       = picked_rgba;
                gt.width      = picked_w;
                gt.height     = picked_h;
                generated_terrain_textures.push_back(std::move(gt));
            }

            const bool terrain_space_texture =
                picked_label.rfind("ehf_composite[", 0) == 0 ||
                picked_label == "ehf_embedded_tile_albedo" ||
                picked_label == "ehf_baked_albedo" ||
                custom_blank_terrain;
            if (terrain_space_texture) {
                normalize_grid_uvs(g, tm.width, tm.height);
            } else if (uv_scale != 1.0f) {
                for (float& uv : g.uvs) uv *= uv_scale;
            }

            std::vector<MDLMeshGeom> geoms;
            geoms.push_back(std::move(g));

            struct AdjGeomBind {
                const std::vector<uint8_t>* page_rgba = nullptr;
                int page_w = 0, page_h = 0;
                int fallback_adj = -1;
                std::string label;
            };
            std::vector<AdjGeomBind> geom_binds(geoms.size());

            auto make_terrain_geom = [](const Level::TerrainMesh& tm,
                                        bool preserve_uvs,
                                        const std::string& name) {
                MDLMeshGeom ag;
                ag.positions = tm.positions;
                ag.normals   = tm.normals;
                ag.uvs       = tm.uvs;
                ag.indices   = tm.indices;
                if (!preserve_uvs) {
                    normalize_grid_uvs(ag, tm.width, tm.height);
                }
                const size_t vn = tm.positions.size() / 3;
                ag.bone_ids.assign(vn * 4, 0);
                ag.bone_weights.assign(vn * 4, 0.f);
                for (size_t v = 0; v < vn; ++v) ag.bone_weights[v * 4 + 0] = 1.0f;
                ag.name = name;
                return ag;
            };

            for (size_t ai = 0; ai < g_pending_adjacent_terrain_meshes.size(); ++ai) {
                const auto& adj = g_pending_adjacent_terrain_meshes[ai];
                const std::string base = adj.label.empty()
                    ? std::string("adjacent terrain")
                    : std::string("adjacent terrain: ") + adj.label;

                if (!adj.patch_geoms.empty()) {

                    for (size_t k = 0; k < adj.patch_geoms.size(); ++k) {
                        const Level::VistaPatchGeom& vg = adj.patch_geoms[k];
                        if (!vg.mesh.ok || vg.mesh.indices.empty()) continue;
                        geoms.push_back(make_terrain_geom(
                            vg.mesh, true,
                            base + " #" + std::to_string(k)));
                        AdjGeomBind bind;
                        if (!vg.page_rgba.empty() &&
                            vg.page_w > 0 && vg.page_h > 0) {
                            bind.page_rgba = &vg.page_rgba;
                            bind.page_w = vg.page_w;
                            bind.page_h = vg.page_h;
                        }
                        bind.label = "ehf_vista_page[" + adj.label + " #" +
                                     std::to_string(k) + "]";
                        geom_binds.push_back(std::move(bind));
                    }
                    continue;
                }

                const Level::TerrainMesh& am = adj.mesh;
                if (!am.ok || am.indices.empty()) continue;
                geoms.push_back(make_terrain_geom(am, adj.preserve_mesh_uvs,
                                                  base));
                AdjGeomBind bind;
                bind.fallback_adj = int(ai);
                geom_binds.push_back(std::move(bind));
            }

            size_t water_geom_first = geoms.size();
            size_t water_geom_count = 0;
            size_t themed_water_geom_count = 0;
            float water_theme_opacity = 1.0f;
            if (g_pending_level_water_present) {

                for (size_t bi = 0; bi < g_pending_level_water_scene.bodies.size(); ++bi) {
                    const auto& body = g_pending_level_water_scene.bodies[bi];
                    for (size_t ti = 0; ti < body.tiles.size(); ++ti) {
                        const auto& t = body.tiles[ti];

                        const float y = std::isfinite(body.base_height)
                            ? body.base_height : 0.0f;

                        const float half_x = std::abs(t.ex) * 0.5f;
                        const float half_z = std::abs(t.ez) * 0.5f;
                        const float x0 = t.cx - half_x;
                        const float x1 = t.cx + half_x;
                        const float z0 = t.cz - half_z;
                        const float z1 = t.cz + half_z;

                        const int cw = std::max(t.cells_x, 1);
                        const int ch = std::max(t.cells_z, 1);
                        const int vert_w = cw + 1;
                        const int vert_h = ch + 1;
                        const size_t vert_count =
                            size_t(vert_w) * size_t(vert_h);
                        if (vert_count > 1200000) continue;

                        MDLMeshGeom wg;
                        wg.positions.reserve(vert_count * 3);
                        wg.normals.reserve(vert_count * 3);
                        wg.uvs.reserve(vert_count * 2);
                        for (int mz = 0; mz <= ch; ++mz) {
                            const float vz = float(mz) / float(ch);
                            const float pz = z0 + (z1 - z0) * vz;
                            for (int mx = 0; mx <= cw; ++mx) {
                                const float vx = float(mx) / float(cw);
                                const float px = x0 + (x1 - x0) * vx;
                                wg.positions.insert(wg.positions.end(),
                                                    { px, y, pz });
                                wg.normals.insert(wg.normals.end(),
                                                  { 0.0f, 1.0f, 0.0f });
                                wg.uvs.insert(wg.uvs.end(), { px, pz });
                            }
                        }

                        auto active_cell = [&](int mx, int mz) -> bool {
                            if (t.mask.empty()) return true;
                            const size_t mi = size_t(mz) * size_t(cw)
                                            + size_t(mx);
                            return mi < t.mask.size() && t.mask[mi] != 0;
                        };
                        for (int mz = 0; mz < ch; ++mz) {
                            for (int mx = 0; mx < cw; ++mx) {
                                if (!active_cell(mx, mz)) continue;
                                const uint32_t v00 =
                                    uint32_t(mz * vert_w + mx);
                                const uint32_t v10 = v00 + 1;
                                const uint32_t v01 =
                                    uint32_t((mz + 1) * vert_w + mx);
                                const uint32_t v11 = v01 + 1;
                                wg.indices.insert(wg.indices.end(),
                                                  { v00, v11, v10,
                                                    v00, v01, v11 });
                            }
                        }
                        if (wg.indices.empty()) {
                            continue;
                        }
                        wg.bone_ids.assign(vert_count * 4, 0);
                        wg.bone_weights.assign(vert_count * 4, 0.0f);
                        for (size_t v = 0; v < vert_count; ++v) {
                            wg.bone_weights[v * 4 + 0] = 1.0f;
                        }
                        wg.name = "water: body" + std::to_string(bi) +
                                  ":tile" + std::to_string(ti);
                        wg.diffuse_tex_name = body.normal_map_path;
                        wg.is_water = true;

                        wg.water_params[0] = y;
                        for (size_t pi = 0; pi < body.params.size() &&
                                            pi + 1 < 38; ++pi) {
                            wg.water_params[pi + 1] = body.params[pi];
                        }

                        wg.has_water_theme = true;
                        auto fin = [](float v, float d) {
                            return std::isfinite(v) ? v : d;
                        };
                        const float sr = fin(body.params[Level::WP_SURFACE_R], 0.0f);
                        const float sg = fin(body.params[Level::WP_SURFACE_G], 0.0f);
                        const float sb = fin(body.params[Level::WP_SURFACE_B], 0.0f);
                        const float dr = fin(body.params[Level::WP_DEEP_R], 0.0f);
                        const float dg = fin(body.params[Level::WP_DEEP_G], 0.0f);
                        const float db = fin(body.params[Level::WP_DEEP_B], 0.0f);
                        const bool file_colours =
                            (sr + sg + sb + dr + dg + db) > 0.0005f;
                        const Gdb::WaterTheme& wt = g_pending_level_water_theme;
                        if (file_colours) {
                            wg.water_shallow_colour[0] = sr;
                            wg.water_shallow_colour[1] = sg;
                            wg.water_shallow_colour[2] = sb;
                            wg.water_deep_colour[0] = dr;
                            wg.water_deep_colour[1] = dg;
                            wg.water_deep_colour[2] = db;
                        } else if (wt.has_shallow_colour || wt.has_deep_colour) {

                            std::copy(std::begin(wt.deep_colour),
                                      std::end(wt.deep_colour),
                                      std::begin(wg.water_shallow_colour));
                            std::copy(std::begin(wt.shallow_colour),
                                      std::end(wt.shallow_colour),
                                      std::begin(wg.water_deep_colour));
                        }
                        if (wt.has_any) {
                            wg.water_opacity = wt.opacity;
                            water_theme_opacity = wt.opacity;
                            ++themed_water_geom_count;
                        } else {
                            wg.water_opacity = 0.78f;
                        }
                        wg.water_theme_params[0] = wt.edge_blend_min;
                        wg.water_theme_params[1] = wt.edge_blend_max;
                        wg.water_theme_params[2] = wt.edge_blend_bias;
                        wg.water_theme_params[3] = wt.max_refraction_distance;
                        wg.water_theme_params[4] = wt.fresnel_bias;
                        wg.water_theme_params[5] = wt.reflection_strength;
                        wg.water_theme_params[6] = wt.refraction_scale;
                        wg.water_theme_params[7] = wt.reflection_scale;
                        wg.water_theme_params[8] = wt.reflection_bias;
                        wg.water_theme_params[9] = wt.normal_scale;
                        geoms.push_back(std::move(wg));
                        ++water_geom_count;
                    }
                }
                if (water_geom_count > 0) {
                    OutputLog::success("water: " +
                        std::to_string(water_geom_count) +
                        " exact patch grid(s) emitted from .water (engine layout)");
                    if (themed_water_geom_count > 0) {
                        std::ostringstream ss;
                        ss << "water: GDB theme opacity applied to "
                           << themed_water_geom_count
                           << " tile(s), opacity="
                           << std::fixed << std::setprecision(2)
                           << water_theme_opacity;
                        OutputLog::info(ss.str());
                    }
                }
            }

            progress_update(78, 100, "Uploading terrain mesh...");
            if (g_mp.has_model) {
                MP_Release(g_mp);
                g_mp.has_model = false;
                g_mp_initialized = false;
                MP_Init(device, g_mp, 800, 600);
                g_mp_initialized = true;
            }
            MP_Build(device, geoms, info, g_mp);
            g_mp.no_tilt = true;
            MP_BuildLevelFx(device, g_mp);
            Skybox::PreviewBinding::ApplySkyTheme(
                g_mp, g_pending_level_sky_theme);
            Skybox::PreviewBinding::ApplyCloudTheme(
                g_mp, g_pending_level_cloud_theme);
            Skybox::PreviewBinding::ApplyWeatherTheme(
                g_mp, g_pending_level_weather_theme,
                g_pending_level_sky_theme);
            Skybox::PreviewBinding::ApplyEnvironmentTimeline(
                g_mp, g_pending_level_environment_timeline);

            (void)water_geom_first;
            (void)water_geom_count;

            S.terrain_mode = true;

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
            {
                float cloud_min = 1e30f;
                if (g_pending_level_cloud_theme.has_any) {
                    for (int li = 0; li < 4; ++li) {
                        const auto& cl =
                            g_pending_level_cloud_theme.layers[li];
                        if (cl.enabled && cl.has_height &&
                            cl.height > 1.0f) {
                            cloud_min = std::min(cloud_min, cl.height);
                        }
                    }
                }
                float cap = (cloud_min < 1e29f) ? (cloud_min - 15.0f)
                                                : (maxy + 60.0f);
                if (cap < maxy + 10.0f) cap = maxy + 10.0f;
                if (g_flycam.pos[1] > cap) g_flycam.pos[1] = cap;
            }
            g_flycam.yaw    = 0.0f;
            g_flycam.pitch  = -0.6f;
            g_flycam.is_looking = false;
            g_flycam.move_speed = std::max(diag * 0.2f, 50.0f);

            g_mp.radius   = std::max(g_mp.radius, diag * 2.0f);
            g_mp.center[0] = cx_mesh;
            g_mp.center[1] = cy_mesh;
            g_mp.center[2] = cz_mesh;

            if (!g_pending_terrain_ghf_heights.empty() &&
                g_pending_terrain_ghf_width > 0 &&
                g_pending_terrain_ghf_height > 0)
            {
                uint64_t bnk_entry_offset = 0;
                uint32_t bnk_entry_size   = 0;
                bool     bnk_entry_compressed = false;
                if (!g_pending_terrain_ghf_entry.bnk_path.empty() &&
                    g_pending_terrain_ghf_entry.file_index >= 0)
                {
                    try {
                        const auto bc = BnkCache::get(
                            g_pending_terrain_ghf_entry.bnk_path);
                        const auto& files = bc.reader->list_files();
                        const int idx =
                            g_pending_terrain_ghf_entry.file_index;
                        if (idx >= 0 && idx < (int)files.size()) {
                            bnk_entry_offset =
                                bc.reader->entry_disk_offset(idx);
                            bnk_entry_size =
                                bc.reader->entry_on_disk_size(idx);
                            bnk_entry_compressed =
                                bc.reader->entry_is_compressed(idx);
                        }
                    } catch (...) {
                    }
                }

                TerrainEdit::Init(
                    g_pending_terrain_ghf_width,
                    g_pending_terrain_ghf_height,
                    g_pending_terrain_ghf_tile_size,
                     0.0f,
                     0.0f,
                    g_pending_terrain_ghf_heights,
                    geoms[0].positions,
                    g_pending_terrain_ghf_payload,
                    g_pending_terrain_ghf_entry.bnk_path,
                    g_pending_terrain_ghf_entry.file_index,
                    g_pending_terrain_ghf_entry.full_path,
                    bnk_entry_offset,
                    bnk_entry_size,
                    bnk_entry_compressed);

                std::ostringstream tos;
                tos << "  TerrainEdit ready: "
                    << g_pending_terrain_ghf_width << "x"
                    << g_pending_terrain_ghf_height
                    << " heights, BNK entry "
                    << (bnk_entry_compressed ? "chunked" : "raw")
                    << " offset=0x" << std::hex << bnk_entry_offset
                    << std::dec << " size=" << bnk_entry_size << "B";
                OutputLog::info(tos.str());
            }

            ID3D11ShaderResourceView* terrain_srv = nullptr;
            if (!picked_rgba.empty() && picked_w > 0 && picked_h > 0) {
                terrain_srv = create_srv_from_rgba(device, picked_w,
                                                   picked_h, picked_rgba);
            }
            if (terrain_srv && !g_mp.meshes.empty()) {
                MPPerMesh& m = g_mp.meshes[0];
                if (m.srv_diffuse) m.srv_diffuse->Release();
                m.srv_diffuse      = terrain_srv;
                m.diffuse_visible  = true;
                m.diffuse_tex_name = picked_label;

                TerrainTextureRegistry::Register(picked_label,
                                                 picked_rgba,
                                                 picked_w, picked_h);

                OutputLog::success("terrain texture bound: " + picked_label
                                   + " (" + std::to_string(picked_w) + "x"
                                   + std::to_string(picked_h)
                                   + ", uv_scale=" + std::to_string(uv_scale)
                                   + ")");
            } else if (!custom_blank_terrain) {
                OutputLog::warn("terrain: no albedo texture decoded "
                                "(.ehf and .texture_atlas both failed)");
            }

            if (!picked_normal_rgba.empty() && picked_normal_w > 0 &&
                picked_normal_h > 0 && !g_mp.meshes.empty()) {
                ID3D11ShaderResourceView* normal_srv =
                    create_srv_from_rgba_mipped(
                        device, picked_normal_w, picked_normal_h,
                        picked_normal_rgba);
                if (normal_srv) {
                    MPPerMesh& m = g_mp.meshes[0];
                    if (m.srv_normal && m.srv_normal != g_mp.default_srv) {
                        m.srv_normal->Release();
                    }
                    m.srv_normal = normal_srv;
                    m.normal_visible = true;
                    m.normal_tex_name = "painted_layer_normals";
                }
            }

            const std::vector<TerrainTextureRegistry::LodPaletteEntry>
                main_lod_palette = TerrainTextureRegistry::GetLodPalette();

            std::vector<std::vector<uint8_t>> adj_composite_cache(
                g_pending_adjacent_terrain_meshes.size());
            std::vector<int> adj_composite_wh(
                g_pending_adjacent_terrain_meshes.size() * 2, 0);
            std::vector<std::string> adj_composite_name(
                g_pending_adjacent_terrain_meshes.size());
            std::vector<uint8_t> adj_composite_done(
                g_pending_adjacent_terrain_meshes.size(), 0);

            for (size_t gi = 1; gi < geom_binds.size() &&
                                gi < g_mp.meshes.size(); ++gi) {
                const AdjGeomBind& b = geom_binds[gi];

                const std::vector<uint8_t>* rgba = nullptr;
                int w = 0, h = 0;
                std::string label;

                if (b.page_rgba) {
                    rgba  = b.page_rgba;
                    w     = b.page_w;
                    h     = b.page_h;
                    label = b.label;
                } else if (b.fallback_adj >= 0) {
                    const size_t ci = size_t(b.fallback_adj);
                    if (!adj_composite_done[ci]) {
                        adj_composite_done[ci] = 1;
                        const auto& adj =
                            g_pending_adjacent_terrain_meshes[ci];
                        int cw = 0, ch = 0;
                        std::string cn;
                        const bool from_pages = Level::BakeEhfVistaPageComposite(
                            adj.ehf_bytes, adj_composite_cache[ci], cw, ch, cn);
                        if (!from_pages &&
                            !Level::BakeEhfTerrainCompositeWithBnk(
                                adj.ehf_bytes, adj.preferred_bnk,
                                adj_composite_cache[ci], cw, ch, cn,
                                adj.prefer_embedded_albedo)) {
                            adj_composite_cache[ci].clear();
                        }
                        adj_composite_wh[ci * 2 + 0] = cw;
                        adj_composite_wh[ci * 2 + 1] = ch;
                        adj_composite_name[ci] =
                            from_pages
                                ? ("ehf_vista_pages[" + adj.label + "]")
                                : (cn == "embedded_tile_albedo")
                                    ? ("ehf_embedded_tile_albedo[" + adj.label + "]")
                                    : ("ehf_composite[" + cn + "]");
                    }
                    if (adj_composite_cache[ci].empty()) continue;
                    rgba  = &adj_composite_cache[ci];
                    w     = adj_composite_wh[ci * 2 + 0];
                    h     = adj_composite_wh[ci * 2 + 1];
                    label = adj_composite_name[ci];
                }

                if (!rgba || rgba->empty() || w <= 0 || h <= 0) continue;

                ID3D11ShaderResourceView* adj_srv =
                    create_srv_from_rgba_mipped(device, w, h, *rgba);
                if (!adj_srv) continue;
                MPPerMesh& m = g_mp.meshes[gi];
                if (m.srv_diffuse) m.srv_diffuse->Release();
                m.srv_diffuse = adj_srv;
                m.diffuse_visible = true;
                m.diffuse_tex_name = label;

                GeneratedTerrainTexture gt;
                gt.mesh_index = gi;
                gt.label      = label;
                gt.rgba       = *rgba;
                gt.width      = w;
                gt.height     = h;
                gt.mipped     = true;
                generated_terrain_textures.push_back(std::move(gt));

                OutputLog::success("adjacent terrain texture bound: " +
                    label + " (" + std::to_string(w) + "x" +
                    std::to_string(h) + ")");
            }
            TerrainTextureRegistry::SetLodPalette(main_lod_palette);

            bool static_lightmap_thumbnail_bound = false;
            if (!g_mp.meshes.empty() &&
                g_pending_terrain_lightmap.ok &&
                !g_pending_terrain_lightmap.rgba.empty()) {
                MPPerMesh& m = g_mp.meshes[0];
                ID3D11ShaderResourceView* lm_srv = create_srv_from_rgba(
                    device,
                    static_cast<int>(g_pending_terrain_lightmap.texture_width),
                    static_cast<int>(g_pending_terrain_lightmap.texture_height),
                    g_pending_terrain_lightmap.rgba);
                if (lm_srv) {
                    if (m.srv_extra) m.srv_extra->Release();
                    m.srv_extra = lm_srv;
                    m.extra_visible = false;
                    m.extra_tex_name = "lmp_static_lightmap";
                    TerrainTextureRegistry::Register(
                        "lmp_static_lightmap",
                        g_pending_terrain_lightmap.rgba,
                        static_cast<int>(g_pending_terrain_lightmap.texture_width),
                        static_cast<int>(g_pending_terrain_lightmap.texture_height));
                    static_lightmap_thumbnail_bound = true;
                    std::ostringstream lm_log;
                    lm_log << "  bound LMP static lightmap: 0x" << std::hex
                           << g_pending_terrain_lightmap.key << std::dec << "  "
                           << g_pending_terrain_lightmap.texture_width << "x"
                           << g_pending_terrain_lightmap.texture_height;
                    OutputLog::info(lm_log.str());
                }
            }

            if (!g_mp.meshes.empty() && !g_pending_terrain_ehf_bytes.empty()) {
                const auto& ehf = g_pending_terrain_ehf_bytes;
                static constexpr char   kMagic[]   = "HeightFieldGraphicsFile";
                static constexpr size_t kMagicLen  = sizeof(kMagic) - 1;
                static constexpr size_t kHeaderLen = 63;
                bool header_ok = (ehf.size() >= kHeaderLen) &&
                    (std::memcmp(ehf.data(), kMagic, kMagicLen) == 0);
                uint32_t body_off = 0, body_size = 0;
                if (header_ok) {
                    auto rd = [&](size_t off) -> uint32_t {
                        return (uint32_t(ehf[off]) << 24)
                             | (uint32_t(ehf[off+1]) << 16)
                             | (uint32_t(ehf[off+2]) << 8)
                             |  uint32_t(ehf[off+3]);
                    };
                    body_off  = rd(55);
                    body_size = rd(59);
                    header_ok = (uint64_t(body_off) + body_size <= ehf.size());
                }

                if (header_ok && body_size > 0) {
                    std::vector<uint8_t> body_slice(
                        ehf.data() + body_off,
                        ehf.data() + body_off + body_size);

                    auto lm = TextureAtlas::DecodeAtlas(body_slice);
                    if (lm.ok && lm.pixel_format == 24u && !lm.rgba.empty()) {
                        TerrainTextureRegistry::Register(
                            "ehf_height", lm.rgba, lm.width, lm.height);
                        if (!static_lightmap_thumbnail_bound) {
                            MPPerMesh& m = g_mp.meshes[0];
                            ID3D11ShaderResourceView* height_srv =
                                create_srv_from_rgba(device, lm.width,
                                                     lm.height, lm.rgba);
                            if (height_srv) {
                                if (m.srv_extra) m.srv_extra->Release();
                                m.srv_extra = height_srv;
                                m.extra_visible = false;
                                m.extra_tex_name = "ehf_height";
                            }
                        }
                        OutputLog::info("  decoded EHF height thumbnail: "
                            + std::to_string(lm.width) + "x"
                            + std::to_string(lm.height) + " (PF=24)");
                    }

                    const size_t bn = body_slice.size();
                    for (size_t i = 4; i + 0x60 < bn; ++i) {
                        if (body_slice[i]   != 0xFF ||
                            body_slice[i+1] != 0xFF ||
                            body_slice[i+2] != 0xFF ||
                            body_slice[i+3] != 0xFE) continue;
                        const uint32_t pf =
                            (uint32_t(body_slice[i+0x18]) << 24) |
                            (uint32_t(body_slice[i+0x19]) << 16) |
                            (uint32_t(body_slice[i+0x1A]) << 8) |
                             uint32_t(body_slice[i+0x1B]);
                        if (pf != 40u) continue;
                        std::vector<uint8_t> nm_slice(
                            body_slice.begin() + i, body_slice.end());
                        auto nm = TextureAtlas::DecodeAtlas(nm_slice);
                        if (nm.ok && !nm.rgba.empty()) {
                            MPPerMesh& m = g_mp.meshes[0];
                            ID3D11ShaderResourceView* nm_srv =
                                create_srv_from_rgba(device, nm.width,
                                                     nm.height, nm.rgba);
                            if (nm_srv) {
                                if (m.srv_normal) m.srv_normal->Release();
                                m.srv_normal      = nm_srv;
                                m.normal_visible  = false;
                                m.normal_tex_name = "ehf_normal";
                                TerrainTextureRegistry::Register(
                                    "ehf_normal", nm.rgba, nm.width, nm.height);
                                OutputLog::info("  bound normal thumbnail: "
                                    + std::to_string(nm.width) + "x"
                                    + std::to_string(nm.height) + " (PF=40 BC5)");
                            }
                        }
                        break;
                    }
                }
            }

            (void)splat_dbg_rgba; (void)splat_dbg_w; (void)splat_dbg_h;

            {
                progress_update(82, 100, "Preparing terrain materials...");
                const std::string& preferred_bnk =
                    g_pending_terrain_level_entry.bnk_path;
                const auto& palette =
                    TerrainTextureRegistry::GetLodPalette();
                std::vector<EhfLodThumbnails::Entry> thumbs;
                thumbs.reserve(palette.size());

                auto decode_one = [&](const std::string& path,
                                      ID3D11ShaderResourceView*& out_srv,
                                      int& out_w, int& out_h,
                                      std::vector<uint8_t>& out_rgba)
                {
                    out_srv = nullptr; out_w = out_h = 0;
                    out_rgba.clear();
                    if (path.empty()) return;

                    std::string basename =
                        std::filesystem::path(path).filename().string();
                    if (basename.empty()) return;
                    std::transform(basename.begin(), basename.end(),
                                   basename.begin(),
                                   [](unsigned char c){ return std::tolower(c); });

                    const std::string cache_key =
                        normalized_asset_path(preferred_bnk) + "|" + basename;
                    auto& cache = terrain_lod_rgba_cache();
                    auto cached = cache.find(cache_key);
                    if (cached != cache.end()) {
                        out_rgba = cached->second.rgba;
                        out_w = cached->second.width;
                        out_h = cached->second.height;
                        out_srv = create_srv_from_rgba(device, out_w, out_h,
                                                       out_rgba);
                        return;
                    }

                    std::vector<unsigned char> blob_uc;
                    bool stitched = false;
                    try {
                        stitched = build_any_tex_buffer_for_name(
                            basename, blob_uc, preferred_bnk);
                    } catch (...) { stitched = false; }
                    if (!stitched || blob_uc.empty()) return;

                    std::vector<uint8_t> rgba;
                    bool has_alpha = false;
                    int w = 0, h = 0;
                    if (!decode_tex_to_rgba(blob_uc, rgba, w, h,
                                            &has_alpha, -1)) return;
                    if (rgba.empty() || w <= 0 || h <= 0) return;

                    ID3D11ShaderResourceView* srv =
                        create_srv_from_rgba(device, w, h, rgba);
                    if (!srv) return;
                    out_srv = srv;
                    out_w   = w;
                    out_h   = h;
                    out_rgba = rgba;
                    remember_terrain_lod_rgba(cache_key, std::move(rgba),
                                              w, h);
                };

                for (const auto& pe : palette) {
                    if (S.cancel_requested.load()) {
                        OutputLog::warn("LOD palette decode aborted: cancel requested");
                        break;
                    }
                    EhfLodThumbnails::Entry e;
                    e.base_diffuse_path   = pe.base_diffuse;
                    e.base_normal_path    = pe.base_normal;
                    e.detail_diffuse_path = pe.detail_diffuse;
                    e.detail_normal_path  = pe.detail_normal;
                    e.base_tile_scale     = pe.base_tile_scale;
                    e.detail_tile_scale   = pe.detail_tile_scale;
                    e.base_intensity      = pe.base_intensity;
                    e.detail_intensity    = pe.detail_intensity;
                    decode_one(pe.base_diffuse, e.srv_base_diffuse,
                               e.base_diffuse_w, e.base_diffuse_h,
                               e.base_diffuse_rgba);
                    decode_one(pe.base_normal, e.srv_base_normal,
                               e.base_normal_w, e.base_normal_h,
                               e.base_normal_rgba);
                    decode_one(pe.detail_diffuse, e.srv_detail_diffuse,
                               e.detail_diffuse_w, e.detail_diffuse_h,
                               e.detail_diffuse_rgba);
                    decode_one(pe.detail_normal, e.srv_detail_normal,
                               e.detail_normal_w, e.detail_normal_h,
                               e.detail_normal_rgba);
                    thumbs.push_back(std::move(e));
                }

                int decoded_count = 0;
                for (const auto& e : thumbs) {
                    if (e.srv_base_diffuse)   ++decoded_count;
                    if (e.srv_base_normal)    ++decoded_count;
                    if (e.srv_detail_diffuse) ++decoded_count;
                    if (e.srv_detail_normal)  ++decoded_count;
                }
                EhfLodThumbnails::Set(std::move(thumbs));
                OutputLog::info("  decoded LOD palette: "
                    + std::to_string(palette.size()) + " materials, "
                    + std::to_string(decoded_count) + "/"
                    + std::to_string(palette.size() * 4) + " maps OK");

                Level::EhfParsedBody splat_parsed;
                if (Level::ParseEhfBody(g_pending_terrain_ehf_bytes,
                                        splat_parsed)) {
                    progress_update(88, 100, "Building terrain splat shader...");
                    const auto& fresh_thumbs = EhfLodThumbnails::Get();
                    bool splat_ok = TerrainSplat::Build(
                        device,
                        splat_parsed,
                        fresh_thumbs,
                        g_pending_terrain_lightmap.rgba,
                        static_cast<int>(
                            g_pending_terrain_lightmap.texture_width),
                        static_cast<int>(
                            g_pending_terrain_lightmap.texture_height),
                        0.0f,
                        0.0f,
                        true);
                    if (splat_ok)
                    {
                        if (!g_mp.meshes.empty()) {
                            g_mp.meshes[0].is_terrain = true;
                            g_mp.meshes[0].diffuse_tex_name =
                                "ehf_splat_terrain";
                        }
                        OutputLog::success(
                            "terrain SPLAT shader bound: global EHF material weights");
                    } else {
                        OutputLog::warn(
                            "terrain SPLAT shader unavailable; using EHF composite texture");
                    }
                } else {
                    OutputLog::warn(
                        "terrain SPLAT parse failed; using EHF composite texture");
                }
            }

            const size_t terrain_vert_count =
                geoms.empty() ? 0 : geoms[0].positions.size() / 3;
            OutputLog::success("terrain '" + g_pending_terrain_label +
                               "' built (" +
                               std::to_string(terrain_vert_count) +
                               " verts)");
            if (S.cancel_requested.load()) {
                OutputLog::warn("level load cancelled before prop stream");
                progress_done();
                S.cancel_requested.store(false);
            } else {
                const bool props_started =
                    start_level_prop_stream(std::move(geoms),
                                            std::move(info));
                if (props_started) {
                    g_level_prop_stream.terrain_textures =
                        std::move(generated_terrain_textures);
                } else {
                    progress_done();
                }
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

        g_pending_level_water_present = false;
        g_pending_level_water_scene = Level::WaterScene{};
        g_pending_level_water_theme = Gdb::WaterTheme{};
        g_pending_level_sky_theme = Gdb::SkyTheme{};
        g_pending_level_cloud_theme = Gdb::CloudTheme{};
        g_pending_level_weather_theme = Gdb::WeatherTheme{};
        g_pending_level_environment_timeline =
            Gdb::EnvironmentThemeTimeline{};
    }

    stream_level_prop_batch(device);
