bool parse_mdl_geometry(const std::vector<unsigned char>& data, const MDLInfo& info, std::vector<MDLMeshGeom>& out){
    out.clear();
    if(info.MeshBuffers.size()!=info.Meshes.size()){
        return true;
    }
    R r{data.data(), data.size(), 0};
    for(size_t mi=0; mi<info.MeshBuffers.size(); ++mi){
        const auto& mb=info.MeshBuffers[mi];

        if(mb.IsFoliagePath){

            if(mb.FoliageVertexStride == 20){
                if(mb.VertexCount==0 || mb.FaceCount==0 ||
                   mb.VertexOffset+(size_t)mb.VertexCount*20>r.n ||
                   mb.FaceOffset+(size_t)mb.FaceCount*2>r.n){
                    MDLMeshGeom g;
                    if(mi<info.Meshes.size() && !info.Meshes[mi].Materials.empty())
                        { g.diffuse_tex_name=info.Meshes[mi].Materials[0].DiffuseTexName; g.normal_tex_name=info.Meshes[mi].Materials[0].NormalTexName; g.specular_tex_name=info.Meshes[mi].Materials[0].SpecularTexName; g.metallic_tex_name=info.Meshes[mi].Materials[0].MetallicTexName; g.extra_tex_name=info.Meshes[mi].Materials[0].ExtraTexName; }
                    if(mi<info.Meshes.size() && !info.Meshes[mi].MeshName.empty())
                        g.name=info.Meshes[mi].MeshName;
                    g.MeshIndex=(uint32_t)mi;
                    out.push_back(std::move(g));
                    continue;
                }
                MDLMeshGeom g;
                g.positions.resize((size_t)mb.VertexCount*3);
                g.uvs.resize((size_t)mb.VertexCount*2);
                const uint8_t* vp=r.p+mb.VertexOffset;
                for(uint32_t v=0;v<mb.VertexCount;++v){
                    const uint8_t* p=vp+v*20;

                    g.positions[v*3+0]=half_to_float((uint16_t(p[0])<<8)|p[1]);
                    g.positions[v*3+1]=half_to_float((uint16_t(p[2])<<8)|p[3]);
                    g.positions[v*3+2]=half_to_float((uint16_t(p[4])<<8)|p[5]);

                    g.uvs[v*2+0]=half_to_float((uint16_t(p[12])<<8)|p[13]);
                    g.uvs[v*2+1]=half_to_float((uint16_t(p[14])<<8)|p[15]);

                }
                std::vector<uint16_t> strip(mb.FaceCount);
                const uint8_t* fp=r.p+mb.FaceOffset;
                bool hasFFFF=false;
                for(uint32_t i=0;i<mb.FaceCount;i++){
                    uint16_t w=(uint16_t(fp[i*2+0])<<8)|fp[i*2+1];
                    strip[i]=w; if(w==0xFFFF) hasFFFF=true;
                }
                if(hasFFFF){ build_triangles_from_strip(strip,g.indices); }
                else{
                    size_t tc=strip.size()/3; g.indices.resize(tc*3);
                    for(size_t t=0;t<tc;t++){
                        g.indices[t*3+0]=strip[t*3+0]; g.indices[t*3+1]=strip[t*3+1]; g.indices[t*3+2]=strip[t*3+2];
                    }
                }

                compute_smooth_normals(mb.VertexCount, g.indices, g.positions, g.normals);

                if(mi < info.Meshes.size() && !info.Meshes[mi].Materials.empty()){
                    uint8_t pick = 0;
                    if(!mb.SubMeshes.empty()) pick = mb.SubMeshes[0].MaterialIndex;
                    if(pick >= info.Meshes[mi].Materials.size()) pick = 0;
                    g.diffuse_tex_name = info.Meshes[mi].Materials[pick].DiffuseTexName;
                }
                g.MeshIndex=(uint32_t)mi; g.SubMeshIndex=0;
                if(mi<info.Meshes.size() && !info.Meshes[mi].MeshName.empty())
                    g.name=info.Meshes[mi].MeshName;
                else
                    g.name="mesh_"+std::to_string(mi);
                out.push_back(std::move(g));
                continue;
            }

            if(mb.FoliageVertexStride == 48 || mb.FoliageVertexStride == 0){
                if(mb.VertexCount==0 || mb.FaceCount==0 ||
                   mb.VertexOffset+(size_t)mb.VertexCount*48>r.n ||
                   mb.FaceOffset+(size_t)mb.FaceCount*2>r.n){
                    MDLMeshGeom g;
                    if(mi<info.Meshes.size() && !info.Meshes[mi].Materials.empty())
                        { g.diffuse_tex_name=info.Meshes[mi].Materials[0].DiffuseTexName; g.normal_tex_name=info.Meshes[mi].Materials[0].NormalTexName; g.specular_tex_name=info.Meshes[mi].Materials[0].SpecularTexName; g.metallic_tex_name=info.Meshes[mi].Materials[0].MetallicTexName; g.extra_tex_name=info.Meshes[mi].Materials[0].ExtraTexName; }
                    if(mi<info.Meshes.size() && !info.Meshes[mi].MeshName.empty())
                        g.name=info.Meshes[mi].MeshName;
                    g.MeshIndex=(uint32_t)mi;
                    out.push_back(std::move(g));
                    continue;
                }
                MDLMeshGeom g;
                g.positions.resize((size_t)mb.VertexCount*3);
                g.normals.resize((size_t)mb.VertexCount*3);
                g.uvs.resize((size_t)mb.VertexCount*2);
                const uint8_t* vp=r.p+mb.VertexOffset;
                auto bswap=[](float f){ uint32_t u; std::memcpy(&u,&f,4);
                    u=(u>>24)|((u>>8)&0xFF00)|((u<<8)&0xFF0000)|(u<<24);
                    std::memcpy(&f,&u,4); return f; };
                for(uint32_t v=0;v<mb.VertexCount;++v){
                    const uint8_t* p=vp+v*48;
                    float px,py,pz,nx,ny,nz,uu,vv;
                    std::memcpy(&px,p+0, 4); std::memcpy(&py,p+4, 4); std::memcpy(&pz,p+8, 4);
                    std::memcpy(&nx,p+12,4); std::memcpy(&ny,p+16,4); std::memcpy(&nz,p+20,4);
                    std::memcpy(&uu,p+24,4); std::memcpy(&vv,p+28,4);
                    g.positions[v*3+0]=bswap(px); g.positions[v*3+1]=bswap(py); g.positions[v*3+2]=bswap(pz);
                    g.normals[v*3+0]=bswap(nx);   g.normals[v*3+1]=bswap(ny);   g.normals[v*3+2]=bswap(nz);
                    g.uvs[v*2+0]=bswap(uu);       g.uvs[v*2+1]=bswap(vv);
                }
                std::vector<uint16_t> strip(mb.FaceCount);
                const uint8_t* fp=r.p+mb.FaceOffset;
                bool hasFFFF=false;
                for(uint32_t i=0;i<mb.FaceCount;i++){
                    uint16_t w=(uint16_t(fp[i*2+0])<<8)|fp[i*2+1];
                    strip[i]=w; if(w==0xFFFF) hasFFFF=true;
                }
                if(hasFFFF){ build_triangles_from_strip(strip,g.indices); }
                else{
                    size_t tc=strip.size()/3; g.indices.resize(tc*3);
                    for(size_t t=0;t<tc;t++){
                        g.indices[t*3+0]=strip[t*3+0]; g.indices[t*3+1]=strip[t*3+1]; g.indices[t*3+2]=strip[t*3+2];
                    }
                }
                if(mi<info.Meshes.size() && !info.Meshes[mi].Materials.empty())
                    { g.diffuse_tex_name=info.Meshes[mi].Materials[0].DiffuseTexName; g.normal_tex_name=info.Meshes[mi].Materials[0].NormalTexName; g.specular_tex_name=info.Meshes[mi].Materials[0].SpecularTexName; g.metallic_tex_name=info.Meshes[mi].Materials[0].MetallicTexName; g.extra_tex_name=info.Meshes[mi].Materials[0].ExtraTexName; }
                g.MeshIndex=(uint32_t)mi; g.SubMeshIndex=0;
                if(mi<info.Meshes.size() && !info.Meshes[mi].MeshName.empty())
                    g.name=info.Meshes[mi].MeshName;
                else
                    g.name="mesh_"+std::to_string(mi);
                out.push_back(std::move(g));
                continue;
            }

            if(mb.FoliageVertexStride == 36){
                if(mb.VertexCount==0 || mb.FaceCount==0 ||
                   mb.VertexOffset+(size_t)mb.VertexCount*36>r.n ||
                   mb.FaceOffset+(size_t)mb.FaceCount*2>r.n){
                    MDLMeshGeom g;
                    if(mi<info.Meshes.size() && !info.Meshes[mi].Materials.empty())
                        { g.diffuse_tex_name=info.Meshes[mi].Materials[0].DiffuseTexName; g.normal_tex_name=info.Meshes[mi].Materials[0].NormalTexName; g.specular_tex_name=info.Meshes[mi].Materials[0].SpecularTexName; g.metallic_tex_name=info.Meshes[mi].Materials[0].MetallicTexName; g.extra_tex_name=info.Meshes[mi].Materials[0].ExtraTexName; }
                    out.push_back(std::move(g));
                    continue;
                }
                MDLMeshGeom g;
                g.positions.resize((size_t)mb.VertexCount*3);
                g.uvs.resize((size_t)mb.VertexCount*2);
                const uint8_t* vp=r.p+mb.VertexOffset;
                for(uint32_t v=0;v<mb.VertexCount;++v){
                    const uint8_t* p=vp+v*36;
                    g.positions[v*3+0]=half_to_float((uint16_t(p[0])<<8)|p[1]);
                    g.positions[v*3+1]=half_to_float((uint16_t(p[2])<<8)|p[3]);
                    g.positions[v*3+2]=half_to_float((uint16_t(p[4])<<8)|p[5]);
                    g.uvs[v*2+0]=half_to_float((uint16_t(p[12])<<8)|p[13]);
                    g.uvs[v*2+1]=half_to_float((uint16_t(p[14])<<8)|p[15]);
                }
                std::vector<uint16_t> strip(mb.FaceCount);
                const uint8_t* fp=r.p+mb.FaceOffset;
                bool hasFFFF=false;
                for(uint32_t i=0;i<mb.FaceCount;i++){
                    uint16_t w=(uint16_t(fp[i*2+0])<<8)|fp[i*2+1];
                    strip[i]=w; if(w==0xFFFF) hasFFFF=true;
                }
                if(hasFFFF){ build_triangles_from_strip(strip,g.indices); }
                else{
                    size_t tc=strip.size()/3; g.indices.resize(tc*3);
                    for(size_t t=0;t<tc;t++){
                        g.indices[t*3+0]=strip[t*3+0]; g.indices[t*3+1]=strip[t*3+1]; g.indices[t*3+2]=strip[t*3+2];
                    }
                }
                g.normals.resize((size_t)mb.VertexCount*3);
                compute_smooth_normals(mb.VertexCount, g.indices, g.positions, g.normals);
                if(mi<info.Meshes.size() && !info.Meshes[mi].Materials.empty())
                    { g.diffuse_tex_name=info.Meshes[mi].Materials[0].DiffuseTexName; g.normal_tex_name=info.Meshes[mi].Materials[0].NormalTexName; g.specular_tex_name=info.Meshes[mi].Materials[0].SpecularTexName; g.metallic_tex_name=info.Meshes[mi].Materials[0].MetallicTexName; g.extra_tex_name=info.Meshes[mi].Materials[0].ExtraTexName; }
                g.MeshIndex=(uint32_t)mi; g.SubMeshIndex=0;
                if(mi<info.Meshes.size() && !info.Meshes[mi].MeshName.empty())
                    g.name=info.Meshes[mi].MeshName;
                else
                    g.name="mesh_"+std::to_string(mi);
                out.push_back(std::move(g));
                continue;
            }

            {
                MDLMeshGeom g;
                if(mi<info.Meshes.size() && !info.Meshes[mi].Materials.empty())
                    { g.diffuse_tex_name=info.Meshes[mi].Materials[0].DiffuseTexName; g.normal_tex_name=info.Meshes[mi].Materials[0].NormalTexName; g.specular_tex_name=info.Meshes[mi].Materials[0].SpecularTexName; g.metallic_tex_name=info.Meshes[mi].Materials[0].MetallicTexName; g.extra_tex_name=info.Meshes[mi].Materials[0].ExtraTexName; }
                out.push_back(std::move(g));
                continue;
            }
        }

        size_t vertex_stride = mb.IsAltPath ? 20 : 28;

        if(mb.VertexCount==0 || mb.FaceCount==0 || mb.VertexOffset+(size_t)mb.VertexCount*vertex_stride>r.n || mb.FaceOffset+(size_t)mb.FaceCount*2>r.n){
            MDLMeshGeom g;
            if(mi<info.Meshes.size() && !info.Meshes[mi].Materials.empty())
                { g.diffuse_tex_name=info.Meshes[mi].Materials[0].DiffuseTexName; g.normal_tex_name=info.Meshes[mi].Materials[0].NormalTexName; g.specular_tex_name=info.Meshes[mi].Materials[0].SpecularTexName; g.metallic_tex_name=info.Meshes[mi].Materials[0].MetallicTexName; g.extra_tex_name=info.Meshes[mi].Materials[0].ExtraTexName; }
            out.push_back(std::move(g));
            continue;
        }

        std::vector<float> all_positions((size_t)mb.VertexCount*3);
        std::vector<float> all_uvs((size_t)mb.VertexCount*2);
        std::vector<uint16_t> all_bone_ids((size_t)mb.VertexCount*4);
        std::vector<float> all_bone_weights((size_t)mb.VertexCount*4);

        const uint8_t* vp=r.p+mb.VertexOffset;
        for(uint32_t v=0; v<mb.VertexCount; ++v){
            const uint8_t* p=vp+v*vertex_stride;
            uint16_t hx=(uint16_t(p[0])<<8)|p[1];
            uint16_t hy=(uint16_t(p[2])<<8)|p[3];
            uint16_t hz=(uint16_t(p[4])<<8)|p[5];
            all_positions[v*3+0]=half_to_float(hx);
            all_positions[v*3+1]=half_to_float(hy);
            all_positions[v*3+2]=half_to_float(hz);

            if(!mb.IsAltPath && vertex_stride >= 20){
                const uint32_t bone_count =
                    info.BoneCount ? info.BoneCount
                                   : (uint32_t)info.Bones.size();
                uint16_t ids[4] = {
                    (uint16_t)p[12], (uint16_t)p[13],
                    (uint16_t)p[14], (uint16_t)p[15]
                };
                uint8_t weights_u8[4] = { p[16], p[17], p[18], p[19] };

                int out_influence = 0;
                float weight_sum = 0.0f;
                for(int k=0; k<4; ++k){
                    if(ids[k] >= 255) continue;
                    if(bone_count > 0 && ids[k] >= bone_count) continue;
                    if(weights_u8[k] == 0) continue;

                    const size_t o = (size_t)v * 4 + (size_t)out_influence;
                    all_bone_ids[o] = ids[k];
                    all_bone_weights[o] = weights_u8[k] / 255.0f;
                    weight_sum += all_bone_weights[o];
                    ++out_influence;
                }

                if(out_influence > 0 && weight_sum > 1e-6f){
                    for(int k=0; k<out_influence; ++k){
                        all_bone_weights[(size_t)v * 4 + (size_t)k] /=
                            weight_sum;
                    }
                } else {
                    for(int k=0; k<4; ++k){
                        if(ids[k] >= 255) continue;
                        if(bone_count > 0 && ids[k] >= bone_count) continue;
                        all_bone_ids[(size_t)v * 4 + 0] = ids[k];
                        all_bone_weights[(size_t)v * 4 + 0] = 1.0f;
                        out_influence = 1;
                        break;
                    }
                    if(out_influence == 0){
                        all_bone_ids[(size_t)v * 4 + 0] = 0;
                        all_bone_weights[(size_t)v * 4 + 0] = 1.0f;
                    }
                }
            } else {
                all_bone_ids[v*4+0] = 0;
                all_bone_ids[v*4+1] = 0;
                all_bone_ids[v*4+2] = 0;
                all_bone_ids[v*4+3] = 0;
                all_bone_weights[v*4+0] = 1.0f;
                all_bone_weights[v*4+1] = 0.0f;
                all_bone_weights[v*4+2] = 0.0f;
                all_bone_weights[v*4+3] = 0.0f;
            }

            size_t uv_offset = mb.IsAltPath ? 12 : 20;
            uint16_t uu=(uint16_t(p[uv_offset+0])<<8)|p[uv_offset+1];
            uint16_t vv=(uint16_t(p[uv_offset+2])<<8)|p[uv_offset+3];
            all_uvs[v*2+0]=half_to_float(uu);
            all_uvs[v*2+1]=half_to_float(vv);
        }

        std::vector<uint16_t> strip(mb.FaceCount);
        const uint8_t* fp=r.p+mb.FaceOffset;
        bool hasFFFF=false;
        for(uint32_t i=0;i<mb.FaceCount;i++){
            uint16_t w=(uint16_t(fp[i*2+0])<<8)|fp[i*2+1];
            strip[i]=w;
            if(w==0xFFFF) hasFFFF=true;
        }

        if(mb.SubMeshes.empty() || mb.SubMeshCount <= 1){
            MDLMeshGeom g;
            g.positions = all_positions;
            g.uvs = all_uvs;
            g.bone_ids = all_bone_ids;
            g.bone_weights = all_bone_weights;

            if(hasFFFF){ build_triangles_from_strip(strip, g.indices); }
            else{
                size_t triCount=strip.size()/3;
                g.indices.resize(triCount*3);
                for(size_t t=0;t<triCount;t++){
                    g.indices[t*3+0]=strip[t*3+0];
                    g.indices[t*3+1]=strip[t*3+1];
                    g.indices[t*3+2]=strip[t*3+2];
                }
            }

            g.normals.resize(mb.VertexCount * 3);
            compute_smooth_normals(mb.VertexCount, g.indices, g.positions, g.normals);

            if(mi<info.Meshes.size() && !info.Meshes[mi].Materials.empty())
                { g.diffuse_tex_name=info.Meshes[mi].Materials[0].DiffuseTexName; g.normal_tex_name=info.Meshes[mi].Materials[0].NormalTexName; g.specular_tex_name=info.Meshes[mi].Materials[0].SpecularTexName; g.metallic_tex_name=info.Meshes[mi].Materials[0].MetallicTexName; g.extra_tex_name=info.Meshes[mi].Materials[0].ExtraTexName; }

            g.MeshIndex = (uint32_t)mi;
            g.SubMeshIndex = 0;
            if(!mb.SubMeshes.empty() &&
               mb.SubMeshes[0].RegionIndex < info.HideRegions.size())
                g.hide_region = info.HideRegions[mb.SubMeshes[0].RegionIndex];
            if(mi<info.Meshes.size() && !info.Meshes[mi].MeshName.empty())
                g.name = info.Meshes[mi].MeshName;
            else
                g.name = "mesh_" + std::to_string(mi);

            out.push_back(std::move(g));
        } else {
            for(size_t si=0; si<mb.SubMeshes.size(); ++si){
                const auto& sub = mb.SubMeshes[si];

                MDLMeshGeom g;

                if(mi<info.Meshes.size() && sub.MaterialIndex < info.Meshes[mi].Materials.size()) { const auto& mat = info.Meshes[mi].Materials[sub.MaterialIndex]; g.diffuse_tex_name=mat.DiffuseTexName; g.normal_tex_name=mat.NormalTexName; g.specular_tex_name=mat.SpecularTexName; g.metallic_tex_name=mat.MetallicTexName; g.extra_tex_name=mat.ExtraTexName; }
                else if(mi<info.Meshes.size() && !info.Meshes[mi].Materials.empty()) { const auto& mat = info.Meshes[mi].Materials[0]; g.diffuse_tex_name=mat.DiffuseTexName; g.normal_tex_name=mat.NormalTexName; g.specular_tex_name=mat.SpecularTexName; g.metallic_tex_name=mat.MetallicTexName; g.extra_tex_name=mat.ExtraTexName; }

                uint32_t start_idx = sub.StartIndex;
                uint32_t end_idx;
                if(si + 1 < mb.SubMeshes.size())
                    end_idx = mb.SubMeshes[si + 1].StartIndex;
                else
                    end_idx = (uint32_t)strip.size();

                if(start_idx >= strip.size() || start_idx >= end_idx){
                    out.push_back(std::move(g));
                    continue;
                }

                if(end_idx > strip.size()) end_idx = (uint32_t)strip.size();

                std::vector<uint16_t> sub_strip(strip.begin() + start_idx, strip.begin() + end_idx);

                bool sub_has_ffff = false;
                for (uint16_t v : sub_strip) {
                    if (v == 0xFFFF) { sub_has_ffff = true; break; }
                }
                const bool looks_like_triangle_list =
                    !sub_has_ffff && sub.FaceCount > 0 &&
                    sub_strip.size() == (size_t)sub.FaceCount * 3;

                std::vector<uint32_t> sub_indices;
                if (looks_like_triangle_list) {
                    sub_indices.resize(sub_strip.size());
                    for (size_t k = 0; k < sub_strip.size(); ++k) {
                        sub_indices[k] = sub_strip[k];
                    }
                } else {
                    build_triangles_from_strip(sub_strip, sub_indices);
                    if (sub.FaceCount > 0) {
                        const size_t face_cap_idx = (size_t)sub.FaceCount * 3;
                        if (sub_indices.size() > face_cap_idx) {
                            sub_indices.resize(face_cap_idx);
                        }
                    }
                }

                if(sub_indices.empty()){
                    out.push_back(std::move(g));
                    continue;
                }

                std::vector<bool> used_verts(mb.VertexCount, false);
                for(uint32_t idx : sub_indices){
                    if(idx < mb.VertexCount) used_verts[idx] = true;
                }

                std::vector<uint32_t> old_to_new(mb.VertexCount, 0xFFFFFFFF);
                uint32_t new_vert_count = 0;
                for(uint32_t v=0; v<mb.VertexCount; ++v){
                    if(used_verts[v]){
                        old_to_new[v] = new_vert_count++;
                    }
                }

                g.positions.resize(new_vert_count * 3);
                g.uvs.resize(new_vert_count * 2);
                g.bone_ids.resize(new_vert_count * 4);
                g.bone_weights.resize(new_vert_count * 4);

                for(uint32_t v=0; v<mb.VertexCount; ++v){
                    if(used_verts[v]){
                        uint32_t nv = old_to_new[v];
                        g.positions[nv*3+0] = all_positions[v*3+0];
                        g.positions[nv*3+1] = all_positions[v*3+1];
                        g.positions[nv*3+2] = all_positions[v*3+2];
                        g.uvs[nv*2+0] = all_uvs[v*2+0];
                        g.uvs[nv*2+1] = all_uvs[v*2+1];
                        g.bone_ids[nv*4+0] = all_bone_ids[v*4+0];
                        g.bone_ids[nv*4+1] = all_bone_ids[v*4+1];
                        g.bone_ids[nv*4+2] = all_bone_ids[v*4+2];
                        g.bone_ids[nv*4+3] = all_bone_ids[v*4+3];
                        g.bone_weights[nv*4+0] = all_bone_weights[v*4+0];
                        g.bone_weights[nv*4+1] = all_bone_weights[v*4+1];
                        g.bone_weights[nv*4+2] = all_bone_weights[v*4+2];
                        g.bone_weights[nv*4+3] = all_bone_weights[v*4+3];
                    }
                }

                g.indices.resize(sub_indices.size());
                for(size_t i=0; i<sub_indices.size(); ++i){
                    uint32_t old_idx = sub_indices[i];
                    g.indices[i] = (old_idx < mb.VertexCount) ? old_to_new[old_idx] : 0;
                }

                g.normals.resize(new_vert_count * 3);
                compute_smooth_normals(new_vert_count, g.indices, g.positions, g.normals);

                g.MeshIndex = (uint32_t)mi;
                g.SubMeshIndex = (uint32_t)si;
                if(sub.RegionIndex < info.HideRegions.size())
                    g.hide_region = info.HideRegions[sub.RegionIndex];
                std::string baseName;
                if(mi<info.Meshes.size() && !info.Meshes[mi].MeshName.empty())
                    baseName = info.Meshes[mi].MeshName;
                else
                    baseName = "mesh_" + std::to_string(mi);
                g.name = baseName + "_" + std::to_string(si);

                out.push_back(std::move(g));
            }
        }
    }
    return true;
}
