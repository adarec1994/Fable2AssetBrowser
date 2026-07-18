bool MP_Build(ID3D11Device* dev, const std::vector<MDLMeshGeom>& geoms, const MDLInfo& info, ModelPreview& mp, bool append){
    if (!append) {
        for(auto& m : mp.meshes){
            if(m.vb){m.vb->Release();}
            if(m.ib){m.ib->Release();}
            if(m.srv_diffuse && m.srv_diffuse != mp.default_srv){m.srv_diffuse->Release();}
            if(m.srv_normal && m.srv_normal != mp.default_srv){m.srv_normal->Release();}
            if(m.srv_specular && m.srv_specular != mp.default_srv){m.srv_specular->Release();}
            if(m.srv_metallic && m.srv_metallic != mp.default_srv){m.srv_metallic->Release();}
            if(m.srv_extra && m.srv_extra != mp.default_srv){m.srv_extra->Release();}
        }
        mp.meshes.clear();
        mp.lod_count    = 1;
        mp.selected_lod = -1;
        Skybox::ResetForBuild(mp);
    }

    auto extract_lod = [](std::string& name) -> uint32_t {
        size_t pos = name.rfind("|lod");
        if (pos == std::string::npos) return 0;
        const char* p = name.c_str() + pos + 4;
        if (*p == '\0' || *p < '0' || *p > '9') return 0;
        uint32_t v = 0;
        const char* q = p;
        while (*q >= '0' && *q <= '9') {
            v = v * 10 + uint32_t(*q - '0');
            ++q;
        }
        if (*q != '\0') return 0;
        name.erase(pos);
        return v;
    };

    if (!append) {
        float minx=1e9f,miny=1e9f,minz=1e9f,maxx=-1e9f,maxy=-1e9f,maxz=-1e9f;
        for(const auto& g: geoms){
            for(size_t i=0;i+2<g.positions.size();i+=3){
                float x=g.positions[i],y=g.positions[i+1],z=g.positions[i+2];
                if(x<minx)minx=x; if(y<miny)miny=y; if(z<minz)minz=z;
                if(x>maxx)maxx=x; if(y>maxy)maxy=y; if(z>maxz)maxz=z;
            }
        }
        if(!(minx<maxx)){ minx=-1;maxx=1;miny=-1;maxy=1;minz=-1;maxz=1; }
        mp.center[0]=(minx+maxx)*0.5f; mp.center[1]=(miny+maxy)*0.5f; mp.center[2]=(minz+maxz)*0.5f;
        mp.radius = std::max(std::max(maxx-minx,maxy-miny),maxz-minz)*0.5f; if(mp.radius<0.0001f) mp.radius=1.0f;
        FlyCam_Reset(g_flycam, mp.center[0], mp.center[1], mp.center[2], mp.radius);
    }
    for(size_t i=0;i<geoms.size();++i){
        const auto& g = geoms[i];
        size_t vcount = g.positions.size()/3;
        if(vcount==0 || g.indices.empty()) continue;
        std::vector<MPVertex> vtx(vcount);
        bool hasN = (g.normals.size()==vcount*3);
        bool hasT = (g.uvs.size()==vcount*2);

        bool hasBI = (g.bone_ids.size()     == vcount * 4);
        bool hasBW = (g.bone_weights.size() == vcount * 4);
        for(size_t v=0; v<vcount; ++v){
            vtx[v].px = g.positions[v*3+0];
            vtx[v].py = g.positions[v*3+1];
            vtx[v].pz = g.positions[v*3+2];
            vtx[v].nx = hasN ? g.normals[v*3+0] : 0.0f;
            vtx[v].ny = hasN ? g.normals[v*3+1] : 1.0f;
            vtx[v].nz = hasN ? g.normals[v*3+2] : 0.0f;
            vtx[v].u  = hasT ? g.uvs[v*2+0] : 0.0f;
            vtx[v].v  = hasT ? g.uvs[v*2+1] : 0.0f;

            auto cap = [](uint16_t id) -> uint8_t {
                uint32_t x = (uint32_t)id;
                if (x >= MP_MAX_BONES) x = 0;
                return (uint8_t)x;
            };
            if (hasBI) {
                vtx[v].b0 = cap(g.bone_ids[v*4+0]);
                vtx[v].b1 = cap(g.bone_ids[v*4+1]);
                vtx[v].b2 = cap(g.bone_ids[v*4+2]);
                vtx[v].b3 = cap(g.bone_ids[v*4+3]);
            } else {
                vtx[v].b0 = vtx[v].b1 = vtx[v].b2 = vtx[v].b3 = 0;
            }
            if (hasBW) {
                vtx[v].w0 = g.bone_weights[v*4+0];
                vtx[v].w1 = g.bone_weights[v*4+1];
                vtx[v].w2 = g.bone_weights[v*4+2];
                vtx[v].w3 = g.bone_weights[v*4+3];
            } else {
                vtx[v].w0 = 1.0f;
                vtx[v].w1 = vtx[v].w2 = vtx[v].w3 = 0.0f;
            }
        }
        MPPerMesh m;

        {
            float mnx =  1e30f, mny =  1e30f, mnz =  1e30f;
            float mxx = -1e30f, mxy = -1e30f, mxz = -1e30f;
            for (size_t v = 0; v + 2 < g.positions.size(); v += 3) {
                const float x = g.positions[v + 0];
                const float y = g.positions[v + 1];
                const float z = g.positions[v + 2];
                if (x < mnx) mnx = x; if (y < mny) mny = y; if (z < mnz) mnz = z;
                if (x > mxx) mxx = x; if (y > mxy) mxy = y; if (z > mxz) mxz = z;
            }
            if (mnx < mxx) {
                m.center[0] = (mnx + mxx) * 0.5f;
                m.center[1] = (mny + mxy) * 0.5f;
                m.center[2] = (mnz + mxz) * 0.5f;
                float r2 = 0.0f;
                for (size_t v = 0; v + 2 < g.positions.size(); v += 3) {
                    const float x = g.positions[v + 0] - m.center[0];
                    const float y = g.positions[v + 1] - m.center[1];
                    const float z = g.positions[v + 2] - m.center[2];
                    r2 = std::max(r2, x * x + y * y + z * z);
                }
                m.radius = std::sqrt(r2);
                if (m.radius < 0.0001f) m.radius = 0.25f;
            } else {
                m.center[0] = m.center[1] = m.center[2] = 0.0f;
                m.radius = 0.25f;
            }
        }
        const uint64_t vb_bytes =
            uint64_t(vtx.size()) * uint64_t(sizeof(MPVertex));
        const uint64_t ib_bytes =
            uint64_t(g.indices.size()) * uint64_t(sizeof(uint32_t));
        const std::string mesh_log_name =
            g.name.empty() ? std::to_string(i) : g.name;
        if (vb_bytes == 0 || ib_bytes == 0 ||
            vb_bytes > std::numeric_limits<UINT>::max() ||
            ib_bytes > std::numeric_limits<UINT>::max()) {
            OutputLog::warn("MP_Build: skipped oversized mesh '" +
                            mesh_log_name +
                            "' vb=" + std::to_string(vb_bytes) +
                            " ib=" + std::to_string(ib_bytes));
            continue;
        }

        D3D11_BUFFER_DESC vb{}; vb.BindFlags=D3D11_BIND_VERTEX_BUFFER; vb.ByteWidth=(UINT)vb_bytes;
        if(g.cloth_sim){ vb.Usage=D3D11_USAGE_DYNAMIC; vb.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE; }
        else           { vb.Usage=D3D11_USAGE_IMMUTABLE; }
        D3D11_SUBRESOURCE_DATA vsd{}; vsd.pSysMem=vtx.data();
        if(FAILED(dev->CreateBuffer(&vb,&vsd,&m.vb))) {
            OutputLog::warn("MP_Build: vertex buffer create failed for '" +
                            mesh_log_name +
                            "' bytes=" + std::to_string(vb_bytes));
            continue;
        }
        if(g.cloth_sim){ m.cloth = mp_build_cloth(g, std::move(vtx)); }
        D3D11_BUFFER_DESC ib{}; ib.BindFlags=D3D11_BIND_INDEX_BUFFER; ib.ByteWidth=(UINT)ib_bytes; ib.Usage=D3D11_USAGE_IMMUTABLE;
        D3D11_SUBRESOURCE_DATA isd{}; isd.pSysMem=g.indices.data();
        if(FAILED(dev->CreateBuffer(&ib,&isd,&m.ib))) {
            OutputLog::warn("MP_Build: index buffer create failed for '" +
                            mesh_log_name +
                            "' bytes=" + std::to_string(ib_bytes));
            m.vb->Release();
            continue;
        }
        m.index_count = (UINT)g.indices.size();
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
        m.lod_index = extract_lod(m.name);
        if (m.lod_index + 1 > mp.lod_count) mp.lod_count = m.lod_index + 1;

        m.highlight = false;
        m.isolated  = false;
        m.is_terrain = g.is_terrain;
        m.is_water   = g.is_water;
        m.is_cloth   = g.is_cloth;
        m.is_entity_model = g.is_entity_model;
        m.alpha_test = g.alpha_test;
        m.cloth_sim  = g.cloth_sim && (bool)m.cloth;
        std::memcpy(m.water_params, g.water_params,
                    sizeof(m.water_params));
        m.has_water_theme = g.has_water_theme;
        m.water_opacity = g.water_opacity;
        std::memcpy(m.water_shallow_colour, g.water_shallow_colour,
                    sizeof(m.water_shallow_colour));
        std::memcpy(m.water_deep_colour, g.water_deep_colour,
                    sizeof(m.water_deep_colour));
        std::memcpy(m.water_theme_params, g.water_theme_params,
                    sizeof(m.water_theme_params));

        m.source_mesh_idx = (uint32_t)i;
        m.pick_ranges.clear();
        m.pick_ranges.reserve(g.pick_ranges.size());
        for (const auto& pr : g.pick_ranges) {
            MPPerMesh::PickRange mr;
            mr.selection_id = pr.selection_id;
            mr.index_start = pr.index_start;
            mr.index_count = pr.index_count;
            mr.center[0] = pr.center[0];
            mr.center[1] = pr.center[1];
            mr.center[2] = pr.center[2];
            mr.radius = pr.radius;
            mr.inst_pos[0] = pr.inst_pos[0];
            mr.inst_pos[1] = pr.inst_pos[1];
            mr.inst_pos[2] = pr.inst_pos[2];
            mr.inst_rot_deg[0] = pr.inst_rot_deg[0];
            mr.inst_rot_deg[1] = pr.inst_rot_deg[1];
            mr.inst_rot_deg[2] = pr.inst_rot_deg[2];
            mr.has_transform = pr.has_transform;
            mr.inst_hash = pr.inst_hash;
            mr.pos_file_offset = pr.pos_file_offset;
            mr.gdb_pos_off[0] = pr.gdb_pos_off[0];
            mr.gdb_pos_off[1] = pr.gdb_pos_off[1];
            mr.gdb_pos_off[2] = pr.gdb_pos_off[2];
            mr.gdb_rot_off[0] = pr.gdb_rot_off[0];
            mr.gdb_rot_off[1] = pr.gdb_rot_off[1];
            mr.gdb_rot_off[2] = pr.gdb_rot_off[2];
            mr.gdb_entity_hash = pr.gdb_entity_hash;
            mr.lev_rec_kind = pr.lev_rec_kind;
            m.pick_ranges.push_back(mr);
        }
        if (!m.pick_ranges.empty()) {
            m.pick_positions = g.positions;
            m.pick_indices = g.indices;
        }

        std::string preferred_for_tex =
            (S.selected_nested_index != -1 && !S.selected_nested_temp_path.empty())
                ? S.selected_nested_temp_path
                : S.selected_bnk;
        auto load_named_srv = [&](const std::string& tex_name,
                                  ID3D11ShaderResourceView** out_srv,
                                  bool* out_has_alpha) {
            if (tex_name.empty()) return;

            std::string key_name = tex_name;
            std::transform(key_name.begin(), key_name.end(), key_name.begin(),
                           [](unsigned char c){ return std::tolower(c); });
            TexCacheKey ck{ key_name, preferred_for_tex };
            {
                std::lock_guard<std::mutex> lk(tex_cache_mutex());
                auto it = tex_cache_table().find(ck);
                if (it != tex_cache_table().end()) {
                    if (it->second.srv) {
                        *out_srv = it->second.srv;
                        (*out_srv)->AddRef();
                        if (out_has_alpha) *out_has_alpha = it->second.has_alpha;
                    }
                    if (it->second.tried) return;
                }
            }

            std::vector<unsigned char> tex_buf;
            const bool found = build_any_tex_buffer_for_name(tex_name, tex_buf, preferred_for_tex);
            if (!found) {
                OutputLog::warn("texture '" + tex_name +
                                "' not found in any loaded BNK");
            }

            bool dummyA = false;
            bool* alpha_ptr = out_has_alpha ? out_has_alpha : &dummyA;
            if (found) {
                srv_from_tex_blob_auto(dev, tex_buf, out_srv, alpha_ptr);
                if (!*out_srv) {
                    std::string reason = mp_last_decode_fail_reason();
                    std::string info   = mp_last_decode_info();
                    std::string msg = "texture '" + tex_name + "' failed to decode";
                    if (!reason.empty()) msg += " (" + reason + ")";
                    if (!info.empty())   msg += " [" + info + "]";
                    msg += " - bytes=" + std::to_string(tex_buf.size());
                    OutputLog::error(msg);
                }
            }

            {
                std::lock_guard<std::mutex> lk(tex_cache_mutex());
                auto& slot = tex_cache_table()[ck];
                slot.tried = true;
                if (*out_srv && slot.srv == nullptr) {
                    (*out_srv)->AddRef();
                    slot.srv       = *out_srv;
                    slot.has_alpha = *alpha_ptr;
                } else if (*out_srv && slot.srv != nullptr) {
                }
            }
        };
        load_named_srv(g.diffuse_tex_name,  &m.srv_diffuse,  &hasA);
        load_named_srv(g.normal_tex_name,   &m.srv_normal,   nullptr);
        load_named_srv(g.specular_tex_name, &m.srv_specular, nullptr);
        load_named_srv(g.metallic_tex_name, &m.srv_metallic, nullptr);
        load_named_srv(g.extra_tex_name,    &m.srv_extra,    nullptr);
        if (!m.srv_diffuse && mp.default_srv) { m.srv_diffuse = mp.default_srv; m.srv_diffuse->AddRef(); }
        if (!m.srv_normal  && mp.default_srv) { m.srv_normal  = mp.default_srv; m.srv_normal->AddRef(); }
        if (!m.srv_specular&& mp.default_srv) { m.srv_specular= mp.default_srv; m.srv_specular->AddRef(); }
        if (!m.srv_metallic     && mp.default_srv) { m.srv_metallic     = mp.default_srv; m.srv_metallic->AddRef(); }
        if (!m.srv_extra    && mp.default_srv) { m.srv_extra    = mp.default_srv; m.srv_extra->AddRef(); }
        m.has_alpha = m.is_water ? m.has_water_theme : hasA;

        if (m.is_water && !g.diffuse_tex_name.empty()) {
            std::vector<unsigned char> wtex;
            if (build_any_tex_buffer_for_name(g.diffuse_tex_name, wtex,
                                              preferred_for_tex)) {
                std::vector<uint8_t> wrgba;
                int ww = 0, wh = 0;
                bool wha = false;
                if (decode_tex_to_rgba(wtex, wrgba, ww, wh, &wha)) {
                    ID3D11ShaderResourceView* mip_srv =
                        create_srv_from_rgba_mipped(dev, ww, wh, wrgba);
                    if (mip_srv) {
                        if (m.srv_diffuse) m.srv_diffuse->Release();
                        m.srv_diffuse = mip_srv;
                    }
                }
            }
        }
        mp.meshes.push_back(m);
    }

    if (append) return true;

    if (mp.lod_count > 1) {
        mp.selected_lod = 0;
    }
    mp.selected_pick_id = 0;
    mp.selected_pick_hash = 0;
    mp.range_edit_xforms.clear();

    mp.bone_count = 0;
    mp.bone_parents.clear();
    mp.local_rest.clear();
    mp.inv_bind.clear();

    if (info.HasBoneTransforms && info.BoneCount > 0 &&
        info.Bones.size() == info.BoneTransforms.size()) {
        const uint32_t n = std::min<uint32_t>(info.BoneCount, MP_MAX_BONES);
        mp.bone_count = n;
        mp.bone_parents.resize(n);
        mp.local_rest.resize((size_t)n * 11);
        mp.inv_bind.resize((size_t)n * 16);

        for (uint32_t i = 0; i < n; ++i) {
            int pid = info.Bones[i].ParentID;
            if (pid >= (int)n) pid = -1;
            mp.bone_parents[i] = pid;

            const auto& tf = info.BoneTransforms[i];
            for (int k = 0; k < 11; ++k) {
                mp.local_rest[(size_t)i * 11 + k] =
                    (k < (int)tf.size()) ? tf[k] : 0.0f;
            }
        }

        std::vector<XMFLOAT4X4> rest_world;
        compute_rest_world(info, n, rest_world);
        for (uint32_t i = 0; i < n && i < rest_world.size(); ++i) {
            XMMATRIX W = XMLoadFloat4x4(&rest_world[i]);
            XMMATRIX inv = XMMatrixInverse(nullptr, W);
            XMFLOAT4X4 m;
            XMStoreFloat4x4(&m, inv);
            std::memcpy(&mp.inv_bind[(size_t)i * 16], &m,
                        sizeof(float) * 16);
        }
    }

    S.bone_rot_deltas.assign((size_t)mp.bone_count * 4, 0.0f);
    for (uint32_t i = 0; i < mp.bone_count; ++i) {
        S.bone_rot_deltas[(size_t)i * 4 + 3] = 1.0f;
    }
    S.bone_anim_rot_absolute.clear();
    S.bone_anim_rot_present.clear();
    S.bone_anim_trans_delta.clear();
    S.bone_anim_trans_present.clear();
    S.bone_anim_pose_active = false;
    S.selected_bone = -1;
    S.bone_rotate_mode = false;

    mp.has_model = !mp.meshes.empty();
    return true;
}
