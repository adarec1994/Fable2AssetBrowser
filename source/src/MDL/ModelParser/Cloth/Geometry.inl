bool build_mdl_cloth_geometry(const std::vector<unsigned char>& data, std::vector<MDLMeshGeom>& out){
    MDLInfo tmp;
    if(!parse_mdl_cloth_blocks(data, tmp) || tmp.ClothBlocks.empty()) return false;
    const size_t n = data.size();
    auto rdf32 = [&](size_t o) -> float {
        uint32_t u=(uint32_t(data[o])<<24)|(uint32_t(data[o+1])<<16)|(uint32_t(data[o+2])<<8)|uint32_t(data[o+3]);
        float f; std::memcpy(&f,&u,4); return f;
    };
    auto rdu16 = [&](size_t o) -> uint16_t { return (uint16_t)((uint16_t(data[o])<<8)|data[o+1]); };

    bool any=false;
    for(const auto& c : tmp.ClothBlocks){
        const uint32_t nv = c.SimVertexCount;
        if(nv==0 || c.IndexCount < 3) continue;
        if(c.RestPosOffset + (size_t)nv*12 > n) continue;
        if(c.IndexListOffset + (size_t)c.IndexCount*2 > n) continue;

        MDLMeshGeom g;
        g.positions.resize((size_t)nv*3);
        for(uint32_t v=0; v<nv; ++v){
            const size_t o = c.RestPosOffset + (size_t)v*12;
            g.positions[v*3+0]=rdf32(o);
            g.positions[v*3+1]=rdf32(o+4);
            g.positions[v*3+2]=rdf32(o+8);
        }
        g.indices.resize(c.IndexCount);
        bool ok=true;
        for(uint32_t i=0;i<c.IndexCount;++i){
            uint16_t idx = rdu16(c.IndexListOffset + (size_t)i*2);
            if(idx >= nv){ ok=false; break; }
            g.indices[i]=idx;
        }
        if(!ok) continue;

        if(c.BonesPerVertex>0 && c.BonesPerVertex<=4
           && c.BoneIdOffset + (size_t)nv*c.BonesPerVertex <= n
           && c.WeightOffset + (size_t)nv*16 <= n){
            g.bone_ids.assign((size_t)nv*4, 0);
            g.bone_weights.assign((size_t)nv*4, 0.0f);
            for(uint32_t v=0; v<nv; ++v){
                for(uint32_t k=0;k<c.BonesPerVertex;++k)
                    g.bone_ids[v*4+k] = data[c.BoneIdOffset + (size_t)v*c.BonesPerVertex + k];
                for(int k=0;k<4;k++)
                    g.bone_weights[v*4+k] = rdf32(c.WeightOffset + (size_t)v*16 + (size_t)k*4);
            }
        }

        compute_smooth_normals(nv, g.indices, g.positions, g.normals);
        g.name = "[cloth] mesh " + std::to_string(c.MeshIndex);
        g.is_cloth = true;
        g.MeshIndex = 100000u + c.RecordIndex;
        out.push_back(std::move(g));
        any=true;
    }
    return any;
}
