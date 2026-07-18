bool MP_Build(const std::vector<MDLMeshGeom>& geoms, const MDLInfo& info, ModelPreview& mp, bool append) {
    if (!append) {
        for (auto& m : mp.meshes) mp_release_mesh_gl(m);
        mp.meshes.clear();
        mp.lod_count = 1;
        mp.selected_lod = -1;
    }
    float minx = 1e9f, miny = 1e9f, minz = 1e9f, maxx = -1e9f, maxy = -1e9f, maxz = -1e9f;
    if (!append) for (const auto& g : geoms) {
        for (size_t i = 0; i + 2 < g.positions.size(); i += 3) {
            float x = g.positions[i], y = g.positions[i + 1], z = g.positions[i + 2];
            if (x < minx) minx = x; if (y < miny) miny = y; if (z < minz) minz = z;
            if (x > maxx) maxx = x; if (y > maxy) maxy = y; if (z > maxz) maxz = z;
        }
    }
    if (!append) {
    if (!(minx < maxx)) { minx = -1; maxx = 1; miny = -1; maxy = 1; minz = -1; maxz = 1; }
    mp.center[0] = (minx + maxx) * 0.5f; mp.center[1] = (miny + maxy) * 0.5f; mp.center[2] = (minz + maxz) * 0.5f;
    mp.radius = std::max(std::max(maxx - minx, maxy - miny), maxz - minz) * 0.5f;
    if (mp.radius < 0.0001f) mp.radius = 1.0f;
    FlyCam_Reset(g_flycam, mp.center[0], mp.center[1], mp.center[2], mp.radius);
    }
    for (size_t i = 0; i < geoms.size(); ++i) {
        const auto& g = geoms[i];
        size_t vcount = g.positions.size() / 3;
        if (vcount == 0 || g.indices.empty()) continue;
        std::vector<MPVertex> vtx(vcount);
        bool hasN = (g.normals.size() == vcount * 3);
        bool hasT = (g.uvs.size() == vcount * 2);
        for (size_t v = 0; v < vcount; ++v) {
            vtx[v].px = g.positions[v * 3 + 0];
            vtx[v].py = g.positions[v * 3 + 1];
            vtx[v].pz = g.positions[v * 3 + 2];
            vtx[v].nx = hasN ? g.normals[v * 3 + 0] : 0.0f;
            vtx[v].ny = hasN ? g.normals[v * 3 + 1] : 1.0f;
            vtx[v].nz = hasN ? g.normals[v * 3 + 2] : 0.0f;
            vtx[v].u = hasT ? g.uvs[v * 2 + 0] : 0.0f;
            vtx[v].v = hasT ? g.uvs[v * 2 + 1] : 0.0f;
        }
        MPPerMesh m;
        glGenVertexArrays(1, &m.vao);
        glGenBuffers(1, &m.vbo);
        glGenBuffers(1, &m.ibo);
        glBindVertexArray(m.vao);
        glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
        glBufferData(GL_ARRAY_BUFFER, vtx.size() * sizeof(MPVertex), vtx.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, g.indices.size() * sizeof(uint32_t), g.indices.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MPVertex), (void*)offsetof(MPVertex, px));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(MPVertex), (void*)offsetof(MPVertex, nx));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(MPVertex), (void*)offsetof(MPVertex, u));
        glEnableVertexAttribArray(2);
        glBindVertexArray(0);
        m.index_count = (unsigned int)g.indices.size();
        bool hasA = false;
        m.diffuse_tex_name  = g.diffuse_tex_name;
        m.normal_tex_name   = g.normal_tex_name;
        m.specular_tex_name = g.specular_tex_name;
        m.metallic_tex_name = g.metallic_tex_name;
        m.extra_tex_name    = g.extra_tex_name;
        m.diffuse_visible   = true;
        m.normal_visible    = true;
        m.specular_visible  = true;
        m.metallic_visible  = true;
        m.extra_visible     = true;

        if (!g.name.empty()) {
            m.name = g.name;
        } else {
            m.name = "mesh_" + std::to_string(g.MeshIndex)
                   + "_sub_" + std::to_string(g.SubMeshIndex);
        }
        m.highlight = false;
        m.isolated  = false;
        m.is_cloth  = g.is_cloth;
        m.is_entity_model = g.is_entity_model;
        m.is_terrain = g.is_terrain;
        m.is_water = g.is_water;
        m.alpha_test = g.alpha_test;
        m.source_mesh_idx = (uint32_t)i;
        {
            const size_t lod = m.name.rfind("|lod");
            if (lod != std::string::npos) {
                const char* p = m.name.c_str() + lod + 4;
                if (*p >= '0' && *p <= '9') {
                    m.lod_index = (uint32_t)std::strtoul(p, nullptr, 10);
                    m.name.resize(lod);
                    mp.lod_count = std::max(mp.lod_count, m.lod_index + 1);
                }
            }
        }
        m.pick_ranges.reserve(g.pick_ranges.size());
        for (const auto& pr : g.pick_ranges) {
            MPPerMesh::PickRange out;
            out.selection_id = pr.selection_id;
            out.index_start = pr.index_start; out.index_count = pr.index_count;
            std::copy(pr.center, pr.center + 3, out.center);
            out.radius = pr.radius;
            std::copy(pr.inst_pos, pr.inst_pos + 3, out.inst_pos);
            std::copy(pr.inst_rot_deg, pr.inst_rot_deg + 3, out.inst_rot_deg);
            out.has_transform = pr.has_transform; out.inst_hash = pr.inst_hash;
            out.pos_file_offset = pr.pos_file_offset;
            std::copy(pr.gdb_pos_off, pr.gdb_pos_off + 3, out.gdb_pos_off);
            std::copy(pr.gdb_rot_off, pr.gdb_rot_off + 3, out.gdb_rot_off);
            out.gdb_entity_hash = pr.gdb_entity_hash;
            out.lev_rec_kind = pr.lev_rec_kind;
            m.pick_ranges.push_back(out);
        }
        if (!m.pick_ranges.empty()) {
            m.pick_positions = g.positions; m.pick_indices = g.indices;
        }
        if (!g.diffuse_tex_name.empty())  { m.tex_diffuse  = load_tex_from_name(g.diffuse_tex_name,  &hasA); }
        if (!g.normal_tex_name.empty())   { m.tex_normal   = load_tex_from_name(g.normal_tex_name,   nullptr); }
        if (!g.specular_tex_name.empty()) { m.tex_specular = load_tex_from_name(g.specular_tex_name, nullptr); }
        if (!g.metallic_tex_name.empty()) { m.tex_metallic = load_tex_from_name(g.metallic_tex_name, nullptr); }
        if (!g.extra_tex_name.empty())    { m.tex_extra    = load_tex_from_name(g.extra_tex_name,    nullptr); }
        if (!m.tex_diffuse) m.tex_diffuse = mp.default_tex;
        if (!m.tex_normal) m.tex_normal = mp.default_tex;
        if (!m.tex_specular) m.tex_specular = mp.default_tex;
        if (!m.tex_metallic) m.tex_metallic = mp.default_tex;
        if (!m.tex_extra) m.tex_extra = mp.default_tex;
        m.has_alpha = hasA;
        mp.meshes.push_back(m);
    }
    mp.has_model = !mp.meshes.empty();
    return true;
}
