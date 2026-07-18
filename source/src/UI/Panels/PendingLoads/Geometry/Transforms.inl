struct GeneratedTerrainTexture {
    size_t               mesh_index = 0;
    std::string          label;
    std::vector<uint8_t> rgba;
    int                  width = 0;
    int                  height = 0;
    bool                 mipped = false;
};

static void append_transformed_prop_geom(std::vector<MDLMeshGeom>& out,
                                         const MDLMeshGeom& src,
                                         const Level::PropInstance& inst,
                                         float terrain_cx,
                                         float terrain_cz);

static void transform_instance_point(const Level::PropInstance& inst,
                                     float lx,
                                     float ly,
                                     float lz,
                                     float& x,
                                     float& y,
                                     float& z)
{
    const float px = inst.values[0];
    const float py = inst.values[2];
    const float pz = inst.values[1];
    if (inst.has_full_transform) {
        float scale = inst.values[12];
        if (!std::isfinite(scale) || scale == 0.0f) scale = 1.0f;
        lx *= scale;
        ly *= scale;
        lz *= scale;
        const float* m = &inst.values[3];
        x = px + m[0] * lx + m[1] * ly + m[2] * lz;
        y = py + m[3] * lx + m[4] * ly + m[5] * lz;
        z = pz + m[6] * lx + m[7] * ly + m[8] * lz;
        return;
    }

    const float s  = inst.values[6];
    const float c  = inst.values[7];
    const float sx = inst.values[9]  == 0.0f ? 1.0f : inst.values[9];
    const float sy = inst.values[10] == 0.0f ? sx   : inst.values[10];
    const float sz = inst.values[11] == 0.0f ? sx   : inst.values[11];
    lx *= sx;
    ly *= sz;
    lz *= sy;
    x = px + lx * c + lz * s;
    y = py + ly;
    z = pz - lx * s + lz * c;
}

static void transform_instance_normal(const Level::PropInstance& inst,
                                      float lx,
                                      float ly,
                                      float lz,
                                      float& x,
                                      float& y,
                                      float& z)
{
    if (inst.has_full_transform) {
        const float* m = &inst.values[3];
        x = m[0] * lx + m[1] * ly + m[2] * lz;
        y = m[3] * lx + m[4] * ly + m[5] * lz;
        z = m[6] * lx + m[7] * ly + m[8] * lz;
        const float len = std::sqrt(x * x + y * y + z * z);
        if (std::isfinite(len) && len > 1e-6f) {
            x /= len;
            y /= len;
            z /= len;
        }
        return;
    }

    const float s = inst.values[6];
    const float c = inst.values[7];
    x = lx * c + lz * s;
    y = ly;
    z = -lx * s + lz * c;
}

static void instance_display_transform(const Level::PropInstance& inst,
                                       float out_pos[3],
                                       float out_rot_deg[3])
{
    out_pos[0] = inst.values[0];
    out_pos[1] = inst.values[1];
    out_pos[2] = inst.values[2];
    out_rot_deg[0] = 0.0f;
    out_rot_deg[1] = 0.0f;
    out_rot_deg[2] = 0.0f;
    constexpr float kRadToDeg = 57.29577951308232f;

    if (!inst.has_full_transform) {
        out_rot_deg[2] =
            std::atan2(inst.values[6], inst.values[7]) * kRadToDeg;
        return;
    }

    const float* pv = &inst.values[3];
    static const int axis_map[3] = {0, 2, 1};
    float g[9];
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            g[r * 3 + c] = pv[axis_map[r] * 3 + axis_map[c]];
        }
    }
    for (int r = 0; r < 3; ++r) {
        const float len = std::sqrt(g[r*3+0] * g[r*3+0] +
                                    g[r*3+1] * g[r*3+1] +
                                    g[r*3+2] * g[r*3+2]);
        if (std::isfinite(len) && len > 1e-6f) {
            g[r*3+0] /= len;
            g[r*3+1] /= len;
            g[r*3+2] /= len;
        }
    }
    const float sp = std::max(-1.0f, std::min(1.0f, -g[2]));
    out_rot_deg[0] = std::atan2(g[5], g[8]) * kRadToDeg;
    out_rot_deg[1] = std::asin(sp) * kRadToDeg;
    out_rot_deg[2] = std::atan2(g[1], g[0]) * kRadToDeg;
}

static void merge_transformed_instance_into(MDLMeshGeom& dst,
                                            const MDLMeshGeom& src,
                                            const Level::PropInstance& inst,
                                            uint32_t selection_id = 0)
{
    if (src.positions.empty() || src.indices.empty()) return;

    const size_t src_vcount = src.positions.size() / 3;
    const size_t dst_vcount = dst.positions.size() / 3;
    if (src_vcount == 0 ||
        dst_vcount > size_t(std::numeric_limits<uint32_t>::max())) {
        return;
    }
    for (uint32_t idx : src.indices) {
        if (idx >= src_vcount) return;
    }

    const uint32_t base_vertex = uint32_t(dst_vcount);
    const uint32_t index_start = uint32_t(dst.indices.size());

    const size_t pos_off = dst.positions.size();
    dst.positions.resize(pos_off + src.positions.size());
    float mnx =  std::numeric_limits<float>::infinity();
    float mny =  std::numeric_limits<float>::infinity();
    float mnz =  std::numeric_limits<float>::infinity();
    float mxx = -std::numeric_limits<float>::infinity();
    float mxy = -std::numeric_limits<float>::infinity();
    float mxz = -std::numeric_limits<float>::infinity();
    for (size_t i = 0; i < src_vcount; ++i) {
        const float lx = src.positions[i*3+0];
        const float ly = src.positions[i*3+2];
        const float lz = src.positions[i*3+1];
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        transform_instance_point(inst, lx, ly, lz,
                                 x, y, z);
        dst.positions[pos_off + i*3 + 0] = x;
        dst.positions[pos_off + i*3 + 1] = y;
        dst.positions[pos_off + i*3 + 2] = z;
        mnx = std::min(mnx, x);
        mny = std::min(mny, y);
        mnz = std::min(mnz, z);
        mxx = std::max(mxx, x);
        mxy = std::max(mxy, y);
        mxz = std::max(mxz, z);
    }

    if (!src.normals.empty()) {
        const size_t n_off = dst.normals.size();
        dst.normals.resize(n_off + src.normals.size());
        const size_t src_ncount = src.normals.size() / 3;
        for (size_t i = 0; i < src_ncount; ++i) {
            const float lx = src.normals[i*3+0];
            const float ly = src.normals[i*3+2];
            const float lz = src.normals[i*3+1];
            transform_instance_normal(inst, lx, ly, lz,
                                      dst.normals[n_off + i*3 + 0],
                                      dst.normals[n_off + i*3 + 1],
                                      dst.normals[n_off + i*3 + 2]);
        }
    }

    if (!src.uvs.empty()) {
        dst.uvs.insert(dst.uvs.end(), src.uvs.begin(), src.uvs.end());
    }
    if (!src.bone_ids.empty()) {
        dst.bone_ids.insert(dst.bone_ids.end(),
                            src.bone_ids.begin(), src.bone_ids.end());
    }
    if (!src.bone_weights.empty()) {
        dst.bone_weights.insert(dst.bone_weights.end(),
                                src.bone_weights.begin(),
                                src.bone_weights.end());
    }

    const size_t i_off = dst.indices.size();
    dst.indices.resize(i_off + src.indices.size());
    for (size_t k = 0; k < src.indices.size(); ++k) {
        dst.indices[i_off + k] = src.indices[k] + base_vertex;
    }

    if (selection_id != 0 && std::isfinite(mnx) && std::isfinite(mxx)) {
        MDLMeshGeom::PickRange pr;
        pr.selection_id = selection_id;
        pr.index_start = index_start;
        pr.index_count = uint32_t(src.indices.size());
        pr.center[0] = (mnx + mxx) * 0.5f;
        pr.center[1] = (mny + mxy) * 0.5f;
        pr.center[2] = (mnz + mxz) * 0.5f;
        float r2 = 0.0f;
        for (size_t i = 0; i < src_vcount; ++i) {
            const float x = dst.positions[pos_off + i*3 + 0] - pr.center[0];
            const float y = dst.positions[pos_off + i*3 + 1] - pr.center[1];
            const float z = dst.positions[pos_off + i*3 + 2] - pr.center[2];
            r2 = std::max(r2, x * x + y * y + z * z);
        }
        pr.radius = std::sqrt(r2);
        if (pr.radius < 0.0001f) pr.radius = 0.25f;
        instance_display_transform(inst, pr.inst_pos, pr.inst_rot_deg);
        pr.has_transform = true;
        pr.inst_hash = inst.hash;
        pr.pos_file_offset = inst.pos_file_offset;
        pr.gdb_pos_off[0] = inst.gdb_pos_off[0];
        pr.gdb_pos_off[1] = inst.gdb_pos_off[1];
        pr.gdb_pos_off[2] = inst.gdb_pos_off[2];
        pr.gdb_rot_off[0] = inst.gdb_rot_off[0];
        pr.gdb_rot_off[1] = inst.gdb_rot_off[1];
        pr.gdb_rot_off[2] = inst.gdb_rot_off[2];
        pr.gdb_entity_hash = inst.gdb_entity_hash;
        pr.lev_rec_kind = inst.lev_rec_kind;
        dst.pick_ranges.push_back(pr);
    }
}
