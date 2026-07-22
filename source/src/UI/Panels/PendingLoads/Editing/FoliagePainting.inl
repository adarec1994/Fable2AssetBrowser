#ifdef _WIN32

#include <cstdio>

constexpr float kFoliagePreviewCell = 32.0f;

static uint32_t s_foliage_rng = 0x1234567u;

static float foliage_rand01()
{
    s_foliage_rng = s_foliage_rng * 1664525u + 1013904223u;
    return (float)(s_foliage_rng >> 8) / 16777216.0f;
}

static std::unordered_map<std::string, CachedPropModel>&
foliage_model_cache()
{
    static std::unordered_map<std::string, CachedPropModel> cache;
    return cache;
}

static std::string foliage_mesh_prefix(const std::string& model_path)
{
    return "foliagepaint|" + model_path + "|";
}

static std::string foliage_cell_prefix(const std::string& model_path,
                                       int cx, int cy)
{
    return foliage_mesh_prefix(model_path) + "c" + std::to_string(cx) +
           "_" + std::to_string(cy) + "|";
}

static uint32_t foliage_geom_lod(const std::string& name)
{
    const size_t pos = name.rfind("|lod");
    if (pos == std::string::npos) return 0;
    const char* p = name.c_str() + pos + 4;
    if (*p < '0' || *p > '9') return 0;
    uint32_t v = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (uint32_t)(*p - '0');
        ++p;
    }
    return *p == '\0' ? v : 0;
}

static Level::PropInstance foliage_prop_instance(
    const FoliageEdit::Instance& inst)
{
    Level::PropInstance p;
    p.values[0] = inst.x;
    p.values[1] = inst.y;
    p.values[2] = inst.z;
    p.values[6] = std::sin(inst.yaw);
    p.values[7] = std::cos(inst.yaw);
    p.values[9] = p.values[10] = p.values[11] =
        inst.scale > 0.0f ? inst.scale : 1.0f;
    p.lev_rec_kind = 3;
    return p;
}

void foliage_preload_model(const std::string& model_path)
{
    if (model_path.empty()) return;
    auto& cached = foliage_model_cache()[model_path];
    if (!cached.loaded && cached.geoms.empty()) {
        load_cached_prop_model(model_path,
                               g_pending_level_model_body_bnk, cached);
        cached.loaded = true;
    }
}

static bool foliage_build_geoms_named(
    ID3D11Device* device, const std::string& model_path,
    const std::vector<FoliageEdit::Instance>& insts,
    const std::string& name_prefix)
{
    if (insts.empty()) return true;
    auto& cached = foliage_model_cache()[model_path];
    if (!cached.loaded && cached.geoms.empty()) {
        load_cached_prop_model(model_path,
                               g_pending_level_model_body_bnk, cached);
        cached.loaded = true;
    }
    if (cached.geoms.empty()) {
        OutputLog::warn("foliage paint: model load miss " + model_path);
        return false;
    }

    bool has_lod0 = false;
    for (const MDLMeshGeom& src : cached.geoms) {
        if (!src.positions.empty() && !src.indices.empty() &&
            foliage_geom_lod(src.name) == 0) {
            has_lod0 = true;
            break;
        }
    }

    static uint64_t s_batch_serial = 0;
    std::vector<MDLMeshGeom> out;
    for (size_t gi = 0; gi < cached.geoms.size(); ++gi) {
        const MDLMeshGeom& src = cached.geoms[gi];
        if (src.positions.empty() || src.indices.empty()) continue;
        if (has_lod0 && foliage_geom_lod(src.name) > 0) continue;
        MDLMeshGeom cg;
        auto reinit = [&]() {
            init_combined_prop_geom(cg, src, model_path, insts.size(),
                                    21u, gi);
            cg.name = name_prefix + std::to_string(++s_batch_serial);
        };
        reinit();
        for (const FoliageEdit::Instance& inst : insts) {
            if (would_exceed_combined_prop_limits(cg, src)) {
                out.push_back(std::move(cg));
                reinit();
            }
            const Level::PropInstance p = foliage_prop_instance(inst);
            merge_transformed_instance_into(cg, src, p, 0);
        }
        if (!cg.positions.empty() && !cg.indices.empty()) {
            out.push_back(std::move(cg));
        }
    }
    if (out.empty()) return false;
    MDLInfo dummy_info;
    MP_Build(device, out, dummy_info, g_mp, true);
    return true;
}

static void foliage_rebuild_cell(ID3D11Device* device,
                                 const std::string& model_path,
                                 int cx, int cy)
{
    const std::string prefix = foliage_cell_prefix(model_path, cx, cy);
    MP_RemoveMeshesByNamePrefix(g_mp, prefix);
    const float min_x = (float)cx * kFoliagePreviewCell;
    const float min_y = (float)cy * kFoliagePreviewCell;
    const std::vector<FoliageEdit::Instance> insts =
        FoliageEdit::SnapshotRect(model_path, min_x, min_y,
                                  min_x + kFoliagePreviewCell,
                                  min_y + kFoliagePreviewCell);
    foliage_build_geoms_named(device, model_path, insts, prefix);
}

static void foliage_build_all_cells(ID3D11Device* device,
                                    const std::string& model_path)
{
    const std::vector<FoliageEdit::Instance> all =
        FoliageEdit::Snapshot(model_path);
    std::map<std::pair<int, int>, std::vector<FoliageEdit::Instance>>
        cells;
    for (const FoliageEdit::Instance& inst : all) {
        const int cx = (int)std::floor(inst.x / kFoliagePreviewCell);
        const int cy = (int)std::floor(inst.y / kFoliagePreviewCell);
        cells[{cx, cy}].push_back(inst);
    }
    for (const auto& [cell, insts] : cells) {
        foliage_build_geoms_named(
            device, model_path, insts,
            foliage_cell_prefix(model_path, cell.first, cell.second));
    }
}

static void foliage_take_render_ownership(ID3D11Device* device,
                                          const std::string& model_path)
{
    static uint64_t s_owned_generation = ~0ull;
    static std::unordered_set<std::string> s_owned;
    const uint64_t gen = FoliageEdit::Generation();
    if (gen != s_owned_generation) {
        s_owned_generation = gen;
        s_owned.clear();
    }
    if (s_owned.count(model_path)) return;
    s_owned.insert(model_path);

    std::string leaf = model_path;
    const size_t sl = leaf.find_last_of("/\\");
    if (sl != std::string::npos) leaf = leaf.substr(sl + 1);
    MP_RemoveMeshesByNamePrefix(g_mp,
                                std::string("engine_level: ") + leaf);
    MP_RemoveMeshesByNamePrefix(g_mp, foliage_mesh_prefix(model_path));
    foliage_build_all_cells(device, model_path);
}

static std::unordered_set<std::string>& foliage_stroke_cells()
{
    static std::unordered_set<std::string> cells;
    return cells;
}

void foliage_paint_stroke_end(ID3D11Device* device)
{
    if (!device) return;
    for (const std::string& key : foliage_stroke_cells()) {
        const size_t bar = key.rfind("|c");
        if (bar == std::string::npos) continue;
        const std::string model = key.substr(0, bar);
        int cx = 0, cy = 0;
        if (std::sscanf(key.c_str() + bar + 2, "%d_%d", &cx, &cy) != 2) {
            continue;
        }
        const std::string prefix = foliage_cell_prefix(model, cx, cy);
        size_t batches = 0;
        for (const auto& m : g_mp.meshes) {
            if (m.name.rfind(prefix, 0) == 0) ++batches;
        }
        if (batches > 4) foliage_rebuild_cell(device, model, cx, cy);
    }
    foliage_stroke_cells().clear();
}

bool foliage_paint_dab(ID3D11Device* device, const float engine_hit[3],
                       int tool)
{
    if (!device || !g_mp.has_model || !g_mp.no_tilt) return false;
    const float radius = LandscapePanel::FoliageBrushRadius();

    if (tool == 2) {
        std::vector<LandscapePanel::FoliagePaintEntry> set;
        LandscapePanel::FoliageEnabledPaintSet(set);
        std::vector<std::string> touched;
        size_t removed = 0;
        if (set.empty()) {
            removed = FoliageEdit::EraseAllInRadius(
                engine_hit[0], engine_hit[1], radius, &touched);
        } else {
            std::vector<std::string> models;
            models.reserve(set.size());
            for (const auto& fe : set) models.push_back(fe.model_path);
            removed = FoliageEdit::EraseModelsInRadius(
                models, engine_hit[0], engine_hit[1], radius, &touched);
        }
        if (removed) {
            LevelEdit::NoteExternalEdit();
            const int cx0 = (int)std::floor(
                (engine_hit[0] - radius) / kFoliagePreviewCell);
            const int cx1 = (int)std::floor(
                (engine_hit[0] + radius) / kFoliagePreviewCell);
            const int cy0 = (int)std::floor(
                (engine_hit[1] - radius) / kFoliagePreviewCell);
            const int cy1 = (int)std::floor(
                (engine_hit[1] + radius) / kFoliagePreviewCell);
            for (const std::string& model : touched) {
                foliage_take_render_ownership(device, model);
                for (int cy = cy0; cy <= cy1; ++cy) {
                    for (int cx = cx0; cx <= cx1; ++cx) {
                        foliage_rebuild_cell(device, model, cx, cy);
                    }
                }
            }
        }
        return removed > 0;
    }

    if (tool == 1) {
        const LandscapePanel::FoliagePaintEntry* active =
            LandscapePanel::FoliageActiveEntry();
        if (!active) return false;
        foliage_take_render_ownership(device, active->model_path);
        FoliageEdit::Instance inst;
        inst.x = engine_hit[0];
        inst.y = engine_hit[1];
        inst.z = TerrainEdit::SampleHeightAtWorldXZ(engine_hit[0],
                                                    engine_hit[1]);
        inst.yaw = foliage_rand01() * 6.2831853f;
        inst.scale = active->scale_min +
                     (active->scale_max - active->scale_min) *
                         foliage_rand01();
        FoliageEdit::Add(active->model_path, inst);
        LevelEdit::NoteExternalEdit();
        const int cx = (int)std::floor(inst.x / kFoliagePreviewCell);
        const int cy = (int)std::floor(inst.y / kFoliagePreviewCell);
        foliage_build_geoms_named(
            device, active->model_path, {inst},
            foliage_cell_prefix(active->model_path, cx, cy));
        foliage_stroke_cells().insert(
            active->model_path + "|c" + std::to_string(cx) + "_" +
            std::to_string(cy));
        return true;
    }

    std::vector<LandscapePanel::FoliagePaintEntry> set;
    LandscapePanel::FoliageEnabledPaintSet(set);
    if (set.empty()) return false;

    const float area = 3.14159265f * radius * radius;
    bool any_added = false;
    for (const LandscapePanel::FoliagePaintEntry& fe : set) {
        const float density = std::max(0.001f, fe.density);
        const float target_f = density * area * 0.18f;
        int target = (int)target_f;
        if (foliage_rand01() < target_f - (float)target) ++target;
        target = std::min(64, target);
        if (target <= 0) continue;
        const float spacing = 0.75f / std::sqrt(density);

        foliage_take_render_ownership(device, fe.model_path);

        std::map<std::pair<int, int>, std::vector<FoliageEdit::Instance>>
            added_cells;
        for (int i = 0; i < target; ++i) {
            const float ang = foliage_rand01() * 6.2831853f;
            const float dist = radius * std::sqrt(foliage_rand01());
            const float sx = engine_hit[0] + std::cos(ang) * dist;
            const float sy = engine_hit[1] + std::sin(ang) * dist;
            if (FoliageEdit::HasInstanceWithin(fe.model_path, sx, sy,
                                               spacing)) {
                continue;
            }
            FoliageEdit::Instance inst;
            inst.x = sx;
            inst.y = sy;
            inst.z = TerrainEdit::SampleHeightAtWorldXZ(sx, sy);
            inst.yaw = foliage_rand01() * 6.2831853f;
            inst.scale = fe.scale_min +
                         (fe.scale_max - fe.scale_min) * foliage_rand01();
            FoliageEdit::Add(fe.model_path, inst);
            const int cx = (int)std::floor(sx / kFoliagePreviewCell);
            const int cy = (int)std::floor(sy / kFoliagePreviewCell);
            added_cells[{cx, cy}].push_back(inst);
        }
        for (const auto& [cell, insts] : added_cells) {
            foliage_build_geoms_named(
                device, fe.model_path, insts,
                foliage_cell_prefix(fe.model_path, cell.first,
                                    cell.second));
            foliage_stroke_cells().insert(
                fe.model_path + "|c" + std::to_string(cell.first) + "_" +
                std::to_string(cell.second));
            any_added = true;
        }
    }
    if (any_added) LevelEdit::NoteExternalEdit();
    return any_added;
}

#endif
