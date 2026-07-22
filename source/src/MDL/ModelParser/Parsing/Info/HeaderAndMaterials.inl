bool parse_mdl_info(const std::vector<unsigned char>& data, MDLInfo& out){
    return parse_mdl_info(data, out, "");
}

bool parse_mdl_info(const std::vector<unsigned char>& data, MDLInfo& out, const std::string& file_path){
    if(data.size() < 8) return false;
    R r{data.data(), data.size(), 0};

    std::string path_lower = file_path;
    std::transform(path_lower.begin(), path_lower.end(), path_lower.begin(), ::tolower);
    bool is_foliage = (path_lower.find("/foliage/") != std::string::npos) ||
                      (path_lower.find("\\foliage\\") != std::string::npos) ||
                      (path_lower.find("/foliage\\") != std::string::npos) ||
                      (path_lower.find("\\foliage/") != std::string::npos);

    std::string magic((const char*)r.p, 8);
    bool has_magic = (magic == "MeshFile");

    if(has_magic){
        out.Magic = magic;
        r.i += 8;
        uint32_t tmp32=0;
        if(!r.u32be(tmp32)) return false;
        if(!r.u32be(out.HeaderSize)) return false;
        if(!r.skip(88)) return false;
    } else {
        r.i = 0;
        out.Magic.clear();
    }

    if(!r.skip(8*4)) return false;

    if(!r.u32be(out.BoneCount)) return false;
    out.Bones.clear(); out.Bones.reserve(out.BoneCount);
    for(uint32_t i=0;i<out.BoneCount;i++){
        std::string nm; if(!r.strz(nm)) return false;
        uint32_t pid=0; if(!r.u32be(pid)) return false;
        MDLBoneInfo b; b.Name=nm; b.ParentID=(pid==0xFFFFFFFFu)?-1:(int)pid;
        out.Bones.push_back(std::move(b));
    }

    if(!r.u32be(out.BoneTransformCount)) return false;
    out.BoneTransforms.clear();
    if(out.BoneTransformCount==out.BoneCount && out.BoneCount>0){
        out.BoneTransforms.reserve(out.BoneTransformCount);
        for(uint32_t i=0;i<out.BoneTransformCount;i++){
            std::vector<float> tf(11);
            for(int k=0;k<11;k++) if(!r.f32be(tf[k])) return false;
            out.BoneTransforms.push_back(std::move(tf));
        }
        out.HasBoneTransforms=true;
    }else{
        uint32_t m=out.BoneTransformCount; if(m>65535u) m=65535u;
        if(!r.skip((size_t)m*44)) return false;
        out.HasBoneTransforms=false;
    }

    for(int k=0;k<10;k++){ float f; if(!r.f32be(f)) return false; }

    if(!r.u32be(out.MeshCount)) return false;
    if(!r.skip(2*4)) return false;

    bool has_tree_tag = false;
    if(!r.skip(12)) return false;
    {
        size_t save = r.i;
        uint8_t tag_count = 0;
        if(!r.u8(tag_count)) return false;
        if(tag_count > 0 && tag_count < 32){
            bool ok = true;
            std::vector<std::string> tags;
            tags.reserve(tag_count);
            for(uint8_t k = 0; k < tag_count && ok; ++k){
                size_t scan_lim = std::min(r.n, r.i + 64);
                bool found_null = false;
                for(size_t p = r.i; p < scan_lim; ++p){
                    uint8_t b = r.p[p];
                    if(b == 0){ found_null = (p > r.i); break; }
                    bool is_alpha = (b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z');
                    bool is_alnum = is_alpha || (b >= '0' && b <= '9') || b == '_';
                    if(p == r.i){ if(!is_alpha){ found_null = false; break; } }
                    else        { if(!is_alnum){ found_null = false; break; } }
                }
                if(!found_null){ ok = false; break; }
                std::string s;
                if(!r.strz(s, 64)){ ok = false; break; }
                if(s.empty() || s.size() > 32){ ok = false; break; }
                uint8_t flag = 0;
                if(!r.u8(flag)){ ok = false; break; }
                tags.push_back(std::move(s));
            }
            if(!ok){
                r.i = save;
            } else {
                for(const auto& t : tags){
                    if(t == "IsTree" || t == "NewTree"){ has_tree_tag = true; }
                }
            }
        } else if(tag_count != 0){
            r.i = save;
        } else {
        }
    }
    if(!r.skip(5*4)) return false;

    if(!r.u32be(out.Unk6Count)) return false;
    if(out.Unk6Count>0 && out.Unk6Count<65535u){
        for(uint32_t i=0;i<out.Unk6Count;i++){ float f; if(!r.f32be(f)) return false; }
    }

    uint32_t StringBlockCount=0;
    if(!r.u32be(StringBlockCount)) return false;
    out.HideRegions.clear();
    if(StringBlockCount>0 && StringBlockCount<1000000u){
        out.HideRegions.reserve(StringBlockCount);
        for(uint32_t i=0;i<StringBlockCount;i++){
            std::string s; if(!r.strz(s)) return false;
            out.HideRegions.push_back(std::move(s));
        }
    }

    out.Meshes.clear(); out.Meshes.reserve(out.MeshCount);
    out.MeshBuffers.clear(); out.MeshBuffers.reserve(out.MeshCount);

    for(uint32_t mi=0; mi<out.MeshCount; ++mi){
        uint32_t u1=0; if(!r.u32be(u1)) return false;
        std::string meshName; if(!r.strz(meshName)) return false;
        float f2; if(!r.f32be(f2)) return false; if(!r.f32be(f2)) return false;
        if(!r.skip(21)) return false;
        float f4; if(!r.f32be(f4)) return false;
        uint32_t u5[3]; for(int k=0;k<3;k++) if(!r.u32be(u5[k])) return false;
        uint32_t mcount=0; if(!r.u32be(mcount)) return false;
        MDLMeshInfo mesh; mesh.MeshName=meshName; mesh.MaterialCount=mcount;
        if(mcount>0 && mcount<65535u){
            mesh.Materials.reserve(mcount);
            for(uint32_t j=0;j<mcount;j++){
                MDLMaterialInfo m;
                if(!r.strz(m.DiffuseTexName)) return false;
                if(!r.strz(m.SpecularTexName)) return false;
                if(!r.strz(m.NormalTexName)) return false;
                if(!r.strz(m.MetallicTexName)) return false;
                if(!r.strz(m.ExtraTexName)) return false;
                remap_known_mdl_texture_path(m.DiffuseTexName);
                remap_known_mdl_texture_path(m.SpecularTexName);
                remap_known_mdl_texture_path(m.NormalTexName);
                remap_known_mdl_texture_path(m.MetallicTexName);
                remap_known_mdl_texture_path(m.ExtraTexName);

                bool used_scan = false;
                if(has_tree_tag && j + 1 == mcount){
                    size_t scan_from = r.i;
                    size_t end_search = std::min(r.n, r.i + 1024);
                    if(mi + 1 < out.MeshCount){
                        for(size_t p = r.i; p + 4 < end_search; ++p){
                            if(r.p[p]==0 && r.p[p+1]==0 && r.p[p+2]==0 && r.p[p+3]==0x01){
                                uint8_t nxt = r.p[p+4];
                                if((nxt >= 'A' && nxt <= 'Z') || (nxt >= 'a' && nxt <= 'z')){
                                    r.i = p; used_scan = true; break;
                                }
                            }
                        }
                    } else {
                        for(size_t p = r.i; p + 7 <= end_search; ++p){
                            if(r.p[p+2]==0x01 && r.p[p+3]==0 && r.p[p+4]==0 && r.p[p+5]==0 && r.p[p+6]==0){
                                r.i = p; used_scan = true; break;
                            }
                        }
                    }
                }
                if(!used_scan){
                    if(!r.u32be(m.Unk1)) return false;
                    if(!r.u32be(m.Unk2[0])) return false;
                    if(!r.u32be(m.Unk2[1])) return false;
                    size_t keep=r.i; uint8_t peek=0;
                    if(r.u8(peek)){ if(peek!=0x01){ r.i=keep; } } else { r.i=keep; }
                }
                mesh.Materials.push_back(std::move(m));
            }
        }
        out.Meshes.push_back(std::move(mesh));
    }
