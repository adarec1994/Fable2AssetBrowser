bool build_mdl_engine_geometry(const std::vector<unsigned char>& data, std::vector<MDLMeshGeom>& out){
    std::vector<MDLEngRec>     recs;
    std::vector<MDLEngMeshHdr> hdrs;
    std::vector<std::string> hide_regions;
    uint32_t bone_count = 0;
    if(!parse_mdl_engine_records(data, recs, hdrs, bone_count,
                                 hide_regions)) return false;
    const size_t n = data.size();
    bool any=false;
    uint32_t ri=0;
    for(const auto& rec : recs){
        const uint32_t vc     = rec.VertexCount;
        const uint32_t stride = rec.Skinned ? 28u : 20u;
        const uint32_t uv_off = rec.Skinned ? 20u : 12u;
        if(vc==0 || rec.IndexCount<3){ ++ri; continue; }
        if(rec.VertexOffset + (size_t)vc*stride > n){ ++ri; continue; }
        if(rec.IndexOffset + (size_t)rec.IndexCount*2 > n){ ++ri; continue; }

        MDLMeshGeom g;
        g.positions.resize((size_t)vc*3);
        g.uvs.resize((size_t)vc*2);
        if(rec.Skinned){
            g.bone_ids.assign((size_t)vc*4, 0);
            g.bone_weights.assign((size_t)vc*4, 0.0f);
        }
        const uint8_t* vp = data.data()+rec.VertexOffset;
        for(uint32_t v=0; v<vc; ++v){
            const uint8_t* p = vp + (size_t)v*stride;
            g.positions[v*3+0]=half_to_float((uint16_t(p[0])<<8)|p[1]);
            g.positions[v*3+1]=half_to_float((uint16_t(p[2])<<8)|p[3]);
            g.positions[v*3+2]=half_to_float((uint16_t(p[4])<<8)|p[5]);
            g.uvs[v*2+0]=half_to_float((uint16_t(p[uv_off+0])<<8)|p[uv_off+1]);
            g.uvs[v*2+1]=half_to_float((uint16_t(p[uv_off+2])<<8)|p[uv_off+3]);

            if(rec.Skinned){
                uint16_t ids[4] = { p[12], p[13], p[14], p[15] };
                uint8_t  wt [4] = { p[16], p[17], p[18], p[19] };
                int outI=0; float sum=0.0f;
                for(int k=0;k<4;++k){
                    if(ids[k]>=255) continue;
                    if(bone_count>0 && ids[k]>=bone_count) continue;
                    if(wt[k]==0) continue;
                    const size_t o=(size_t)v*4+(size_t)outI;
                    g.bone_ids[o]=ids[k];
                    g.bone_weights[o]=wt[k]/255.0f;
                    sum+=g.bone_weights[o];
                    ++outI;
                }
                if(outI>0 && sum>1e-6f){
                    for(int k=0;k<outI;++k) g.bone_weights[(size_t)v*4+(size_t)k]/=sum;
                }else{
                    outI=0;
                    for(int k=0;k<4;++k){
                        if(ids[k]>=255) continue;
                        if(bone_count>0 && ids[k]>=bone_count) continue;
                        g.bone_ids[(size_t)v*4+0]=ids[k];
                        g.bone_weights[(size_t)v*4+0]=1.0f;
                        outI=1; break;
                    }
                    if(outI==0){ g.bone_ids[(size_t)v*4+0]=0; g.bone_weights[(size_t)v*4+0]=1.0f; }
                }
            }
        }

        std::vector<uint16_t> strip(rec.IndexCount);
        const uint8_t* fp = data.data()+rec.IndexOffset;
        for(uint32_t i=0;i<rec.IndexCount;i++)
            strip[i]=(uint16_t(fp[i*2+0])<<8)|fp[i*2+1];

        auto build_range=[&](uint32_t s, uint32_t e, std::vector<uint32_t>& outIdx){
            outIdx.clear();
            if(e>rec.IndexCount) e=rec.IndexCount;
            if(s>=e) return;
            std::vector<uint16_t> sub(strip.begin()+s, strip.begin()+e);
            bool ff=false; for(uint16_t w: sub){ if(w==0xFFFF){ ff=true; break; } }
            if(ff){ build_triangles_from_strip(sub, outIdx); }
            else{
                size_t tc=sub.size()/3; outIdx.resize(tc*3);
                for(size_t t=0;t<tc;t++){ outIdx[t*3]=sub[t*3]; outIdx[t*3+1]=sub[t*3+1]; outIdx[t*3+2]=sub[t*3+2]; }
            }
        };

        std::vector<MDLEngSub> subs = rec.Submeshes;
        if(subs.empty()){ subs.push_back(MDLEngSub{}); }
        std::sort(subs.begin(), subs.end(),
                  [](const MDLEngSub&a,const MDLEngSub&b){ return a.StartIdx<b.StartIdx; });

        std::vector<uint32_t> fullIdx; build_range(0, rec.IndexCount, fullIdx);
        bool okAll=true; for(uint32_t id : fullIdx){ if(id>=vc){ okAll=false; break; } }
        if(!okAll){ ++ri; continue; }
        compute_smooth_normals(vc, fullIdx, g.positions, g.normals);

        const MDLEngMeshHdr* hdr = (rec.MeshIdx<hdrs.size()) ? &hdrs[rec.MeshIdx] : nullptr;
        const std::string baseName =
            !rec.MeshName.empty() ? rec.MeshName
            : (hdr && !hdr->name.empty() ? hdr->name : ("mesh_"+std::to_string(ri)));
        auto assignMat=[&](MDLMeshGeom& tg, uint8_t matIdx){
            if(hdr && !hdr->mats.empty()){
                const MDLEngMat& mt = hdr->mats[matIdx < hdr->mats.size() ? matIdx : 0];
                tg.diffuse_tex_name=mt.diffuse; tg.specular_tex_name=mt.specular;
                tg.normal_tex_name=mt.normal;   tg.metallic_tex_name=mt.metallic;
                tg.extra_tex_name=mt.extra;
            }
            tg.name=baseName; tg.alpha_test=true; tg.cloth_sim=false; tg.MeshIndex=ri;
        };
        auto assignRegion=[&](MDLMeshGeom& tg, uint32_t regionIndex){
            if(regionIndex < hide_regions.size()) {
                tg.hide_region = hide_regions[regionIndex];
            }
        };

        if(subs.size()==1){
            g.indices = std::move(fullIdx);
            assignMat(g, subs[0].MatIdx);
            assignRegion(g, subs[0].RegionIndex);
            out.push_back(std::move(g));
            any=true;
        }else{
            for(size_t si=0; si<subs.size(); ++si){
                uint32_t s = subs[si].StartIdx;
                uint32_t e = (si+1<subs.size()) ? subs[si+1].StartIdx : rec.IndexCount;
                std::vector<uint32_t> idx; build_range(s, e, idx);
                if(idx.empty()) continue;
                bool ok2=true; for(uint32_t id : idx){ if(id>=vc){ ok2=false; break; } }
                if(!ok2) continue;
                MDLMeshGeom sg;
                sg.positions = g.positions; sg.uvs = g.uvs; sg.normals = g.normals;
                if(rec.Skinned){ sg.bone_ids = g.bone_ids; sg.bone_weights = g.bone_weights; }
                sg.indices = std::move(idx);
                assignMat(sg, subs[si].MatIdx);
                assignRegion(sg, subs[si].RegionIndex);
                sg.SubMeshIndex = (uint32_t)si;
                out.push_back(std::move(sg));
                any=true;
            }
        }
        ++ri;
    }
    return any;
}
