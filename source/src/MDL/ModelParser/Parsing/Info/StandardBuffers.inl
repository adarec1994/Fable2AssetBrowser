    if(StringBlockCount>0){
        uint32_t bufferID=0, bufferID_copy=0, someCount1=0;
        if(!r.u32be(bufferID)) return false;
        if(!r.u32be(bufferID_copy)) return false;
        if(!r.u32be(someCount1)) return false;
        uint32_t tlen=0; if(!r.u32be(tlen)) return false;
        uint32_t vtx=0; if(!r.u32be(vtx)) return false;
        uint32_t sub=0; if(!r.u32be(sub)) return false;

        std::vector<MDLSubMeshInfo> submeshes;
        if(sub>0 && sub<65535u){
            for(uint32_t s=0;s<sub;s++){
                uint32_t marker; if(!r.u32be(marker)) return false;
                uint32_t matIdxRaw; if(!r.u32be(matIdxRaw)) return false;
                uint8_t subFlag; if(!r.u8(subFlag)) return false;
                (void)subFlag;
                uint32_t faceCount; if(!r.u32be(faceCount)) return false;
                uint32_t startIdx; if(!r.u32be(startIdx)) return false;
                float F4[6];
                for(int k=0;k<6;k++) if(!r.f32be(F4[k])) return false;

                MDLSubMeshInfo smi;
                smi.MaterialIndex = (uint8_t)(matIdxRaw & 0xFF);
                smi.FaceCount = faceCount;
                smi.StartIndex = startIdx;
                submeshes.push_back(smi);
            }
        }
        size_t vert_off=0, face_off=0;
        if(vtx>0 && vtx<65535u){
            vert_off=r.i;
            size_t vsz=(size_t)vtx*28;
            if(!r.skip(vsz)) return false;
        }
        if(tlen>0 && tlen<65535u){
            face_off=r.i;
            size_t fsz=(size_t)tlen*2;
            if(!r.skip(fsz)) return false;
        }
        if(vtx>0 && vtx<65535u){
            size_t usz=(size_t)vtx*16;
            if(!r.skip(usz)) return false;
        }
        MDLMeshBufferInfo mb0;
        mb0.VertexCount=vtx;
        mb0.VertexOffset=vert_off;
        mb0.FaceCount=tlen;
        mb0.FaceOffset=face_off;
        mb0.SubMeshCount=sub;
        mb0.IsAltPath=false;
        mb0.SubMeshes=submeshes;
        mb0.MeshIndex=0;
        out.MeshBuffers.push_back(mb0);

        size_t first_end = r.i;
        size_t scan_pos = first_end;

        for(uint32_t mi=1; mi<out.MeshCount; ++mi){
            bool aligned=false;
            for(size_t sp=scan_pos; sp+4<=r.n; ++sp){
                uint32_t marker = (uint32_t(r.p[sp])<<24) | (uint32_t(r.p[sp+1])<<16) |
                                  (uint32_t(r.p[sp+2])<<8) | r.p[sp+3];
                if(marker==0xFFFFFFFF && sp>=24){
                    uint8_t b0=r.p[sp-24], b1=r.p[sp-23], b2=r.p[sp-22], b3=r.p[sp-21];
                    if(b0==0x00 && b1==0x00 && b2==0x00 && b3>=0x01){
                        r.i=sp-24;
                        aligned=true;
                        break;
                    }
                }
            }
            if(!aligned){
                r.i = first_end + 9;
            }
            uint32_t bufferIDn=0, bufferID_copyn=0, someCount1n=0;
            if(!r.u32be(bufferIDn)) return false;
            if(!r.u32be(bufferID_copyn)) return false;
            if(!r.u32be(someCount1n)) return false;
            uint32_t tlenn=0; if(!r.u32be(tlenn)) return false;
            uint32_t vtxn=0; if(!r.u32be(vtxn)) return false;
            uint32_t subn=0; if(!r.u32be(subn)) return false;

            std::vector<MDLSubMeshInfo> submeshesn;
            if(subn>0 && subn<65535u){
                for(uint32_t s=0;s<subn;s++){
                    uint32_t marker; if(!r.u32be(marker)) return false;
                    uint32_t matIdxRaw; if(!r.u32be(matIdxRaw)) return false;
                    uint8_t subFlag; if(!r.u8(subFlag)) return false;
                    (void)subFlag;
                    uint32_t faceCount; if(!r.u32be(faceCount)) return false;
                    uint32_t startIdx; if(!r.u32be(startIdx)) return false;
                    float F4[6];
                    for(int k=0;k<6;k++) if(!r.f32be(F4[k])) return false;

                    MDLSubMeshInfo smi;
                    smi.MaterialIndex = (uint8_t)(matIdxRaw & 0xFF);
                    smi.FaceCount = faceCount;
                    smi.StartIndex = startIdx;
                    submeshesn.push_back(smi);
                }
            }
            size_t vert_offn=0, face_offn=0;
            if(vtxn>0 && vtxn<65535u){
                vert_offn=r.i;
                size_t vszn=(size_t)vtxn*28;
                if(!r.skip(vszn)) return false;
            }
            if(tlenn>0 && tlenn<65535u){
                face_offn=r.i;
                size_t fszn=(size_t)tlenn*2;
                if(!r.skip(fszn)) return false;
            }
            if(vtxn>0 && vtxn<65535u){
                size_t uszn=(size_t)vtxn*16;
                if(!r.skip(uszn)) return false;
            }
            MDLMeshBufferInfo mb;
            mb.VertexCount=vtxn;
            mb.VertexOffset=vert_offn;
            mb.FaceCount=tlenn;
            mb.FaceOffset=face_offn;
            mb.SubMeshCount=subn;
            mb.IsAltPath=false;
            mb.SubMeshes=submeshesn;
            mb.MeshIndex=mi;
            out.MeshBuffers.push_back(mb);
            scan_pos = r.i;
        }
        return true;
    }

    const size_t mesh_buf_search_anchor = r.i;
    bool wasStringFound = false;
    if(r.i < r.n){
        uint8_t nextByte = r.p[r.i];
        if(nextByte >= 32 && nextByte < 127){
            std::string optStr;
            if(r.strz(optStr)){
                wasStringFound = true;
                uint8_t followByte = 0;
                if(r.u8(followByte)){
                    if(followByte != 0x01) {
                        wasStringFound = false;
                        r.i -= 1;
                    } else {
                        uint32_t mesh_id = 0;
                        if(!r.u32be(mesh_id)) return false;
                        uint32_t mesh_id_copy = 0;
                        if(!r.u32be(mesh_id_copy)) return false;
                    }
                }
            }
        }
    }

    for(uint32_t mi=0; mi<out.MeshCount; ++mi){
        if(mi > 0 && wasStringFound){
            bool found = false;
            for(size_t searchPos = r.i; searchPos < r.n; ++searchPos){
                uint8_t nextByte = r.p[searchPos];
                if(nextByte >= 32 && nextByte < 127){
                    r.i = searchPos;
                    std::string optStr;
                    if(r.strz(optStr)){
                        uint8_t followByte = 0;
                        if(r.u8(followByte)){
                            if(followByte == 0x01) {
                                uint32_t mesh_id = 0;
                                if(!r.u32be(mesh_id)) return false;
                                uint32_t mesh_id_copy = 0;
                                if(!r.u32be(mesh_id_copy)) return false;
                                found = true;
                                break;
                            } else {
                                r.i = searchPos + 1;
                                continue;
                            }
                        }
                    }
                }
            }
            if(!found) return false;
        }

        if(wasStringFound){
            uint32_t someCount1=0;
            if(!r.u32be(someCount1)) return false;

            uint32_t tlen=0;
            if(!r.u32be(tlen)) return false;

            uint32_t vtx=0;
            if(!r.u32be(vtx)) return false;

            if(!r.skip(40)) return false;

            size_t submesh_offset = r.i;
            uint32_t submesh_count = 0;
            if(!r.u32be(submesh_count)) return false;

            uint32_t next_value = 0;
            if(!r.u32be(next_value)) return false;

            uint32_t final_submesh_count;
            if(next_value != 0xFFFFFFFF || submesh_count >= 256){
                final_submesh_count = 1;
            } else {
                r.i -= 4;
                final_submesh_count = submesh_count;
            }

            bool markerFound = false;
            for(size_t searchPos = r.i; searchPos + 4 <= r.n && searchPos < r.i + 1000; ++searchPos){
                uint32_t marker = (uint32_t(r.p[searchPos])<<24) | (uint32_t(r.p[searchPos+1])<<16) |
                                 (uint32_t(r.p[searchPos+2])<<8) | r.p[searchPos+3];
                if(marker == 0xFFFFFFFF){
                    r.i = searchPos;
                    markerFound = true;
                    break;
                }
            }

            if(!markerFound) return false;

            std::vector<MDLSubMeshInfo> submeshes;
            for(uint32_t s=0; s<final_submesh_count; s++){
                uint32_t marker; if(!r.u32be(marker)) return false;
                uint32_t matIdxRaw; if(!r.u32be(matIdxRaw)) return false;
                uint8_t subFlag; if(!r.u8(subFlag)) return false;
                (void)subFlag;
                uint32_t faceCount; if(!r.u32be(faceCount)) return false;
                uint32_t startIdx; if(!r.u32be(startIdx)) return false;
                float F4[6];
                for(int k=0;k<6;k++) if(!r.f32be(F4[k])) return false;

                MDLSubMeshInfo smi;
                smi.MaterialIndex = (uint8_t)(matIdxRaw & 0xFF);
                smi.FaceCount = faceCount;
                smi.StartIndex = startIdx;
                submeshes.push_back(smi);
            }

            size_t vert_off=0, face_off=0, uv_off=0;
            uint32_t uv_stride = 0;
            if(vtx>0 && vtx<65535u){
                vert_off=r.i;
                size_t vsz=(size_t)vtx*20;
                if(!r.skip(vsz)) return false;
            }
            if(tlen>0 && tlen<65535u){
                face_off=r.i;
                size_t fsz=(size_t)tlen*2;
                if(!r.skip(fsz)) return false;
            }

            auto looks_like_mesh_header = [&](size_t at) -> bool {
                if (at >= r.n) return false;
                uint8_t b0 = r.p[at];
                if (b0 < 32 || b0 >= 127) return false;
                size_t lim = std::min(r.n, at + 64);
                size_t p = at;
                while (p < lim && r.p[p] != 0) {
                    uint8_t c = r.p[p];
                    if (c < 32 || c >= 127) return false;
                    ++p;
                }
                if (p >= lim || r.p[p] != 0) return false;
                if (p == at) return false;
                if (p - at > 32) return false;
                if (p + 1 >= r.n) return false;
                return r.p[p + 1] == 0x01;
            };

            if (vtx > 0 && vtx < 65535u) {
                const size_t skip_pad_only  = 4;
                const size_t skip_secondary = (size_t)vtx * 16 + 4;
                const bool last_mesh = (mi + 1 == out.MeshCount);
                const bool is_skinned = (out.BoneCount > 0);

                if (is_skinned) {
                    if (r.i + (size_t)vtx * 16 <= r.n) {
                        uv_off    = r.i;
                        uv_stride = 16;
                    }
                } else if (!last_mesh) {
                    if (looks_like_mesh_header(r.i)) {
                    } else if (r.i + skip_pad_only <= r.n
                            && looks_like_mesh_header(r.i + skip_pad_only)) {
                        if (!r.skip(skip_pad_only)) return false;
                    } else if (r.i + skip_secondary <= r.n
                            && looks_like_mesh_header(r.i + skip_secondary)) {
                        uv_off    = r.i;
                        uv_stride = 16;
                        if (!r.skip(skip_secondary)) return false;
                    }
                } else {
                    if (r.i + skip_secondary <= r.n) {
                        uv_off    = r.i;
                        uv_stride = 16;
                    }
                }
            }

            MDLMeshBufferInfo mb;
            mb.VertexCount=vtx;
            mb.VertexOffset=vert_off;
            mb.FaceCount=tlen;
            mb.FaceOffset=face_off;
            mb.SubMeshCount=final_submesh_count;
            mb.IsAltPath=true;
            mb.SubMeshes=submeshes;
            mb.MeshIndex=mi;
            mb.UvBufferOffset=uv_off;
            mb.UvBufferStride=uv_stride;
            out.MeshBuffers.push_back(mb);

        } else {
            bool found = false;
            size_t searchStart = (mi == 0 && mesh_buf_search_anchor < r.i)
                                   ? mesh_buf_search_anchor
                                   : r.i;
            size_t searchLimit = r.n;

            for(size_t searchPos = searchStart; searchPos + 28 <= searchLimit; ++searchPos){
                uint32_t bufferID = (uint32_t(r.p[searchPos])<<24) | (uint32_t(r.p[searchPos+1])<<16) |
                                   (uint32_t(r.p[searchPos+2])<<8) | r.p[searchPos+3];

                if(bufferID != mi) continue;

                uint32_t someCount = (uint32_t(r.p[searchPos+8])<<24) | (uint32_t(r.p[searchPos+9])<<16) |
                                     (uint32_t(r.p[searchPos+10])<<8) | r.p[searchPos+11];
                uint32_t tlen = (uint32_t(r.p[searchPos+12])<<24) | (uint32_t(r.p[searchPos+13])<<16) |
                               (uint32_t(r.p[searchPos+14])<<8) | r.p[searchPos+15];
                uint32_t vtx = (uint32_t(r.p[searchPos+16])<<24) | (uint32_t(r.p[searchPos+17])<<16) |
                              (uint32_t(r.p[searchPos+18])<<8) | r.p[searchPos+19];
                uint32_t sub = (uint32_t(r.p[searchPos+20])<<24) | (uint32_t(r.p[searchPos+21])<<16) |
                              (uint32_t(r.p[searchPos+22])<<8) | r.p[searchPos+23];

                if(someCount >= 65535u || tlen >= 65535u || vtx >= 65535u || sub >= 256u) continue;

                if(sub > 0){
                    uint32_t marker = (uint32_t(r.p[searchPos+24])<<24) | (uint32_t(r.p[searchPos+25])<<16) |
                                     (uint32_t(r.p[searchPos+26])<<8) | r.p[searchPos+27];
                    if(marker != 0xFFFFFFFF) continue;
                }

                r.i = searchPos;
                found = true;
                break;
            }

            if(!found) {
                MDLMeshBufferInfo mb;
                mb.MeshIndex = mi;
                out.MeshBuffers.push_back(mb);
                continue;
            }

            uint32_t bufferID2=0, bufferID_copy2=0, someCount12=0;
            if(!r.u32be(bufferID2)) return false;
            if(!r.u32be(bufferID_copy2)) return false;
            if(!r.u32be(someCount12)) return false;
            uint32_t tlen2=0; if(!r.u32be(tlen2)) return false;
            uint32_t vtx2=0; if(!r.u32be(vtx2)) return false;
            uint32_t sub2=0; if(!r.u32be(sub2)) return false;

            std::vector<MDLSubMeshInfo> submeshes;
            if(sub2>0 && sub2<65535u){
                for(uint32_t s=0;s<sub2;s++){
                    uint32_t marker; if(!r.u32be(marker)) return false;
                    uint32_t matIdxRaw; if(!r.u32be(matIdxRaw)) return false;
                    uint8_t subFlag; if(!r.u8(subFlag)) return false;
                    (void)subFlag;
                    uint32_t faceCount; if(!r.u32be(faceCount)) return false;
                    uint32_t startIdx; if(!r.u32be(startIdx)) return false;
                    float F4[6];
                    for(int k=0;k<6;k++) if(!r.f32be(F4[k])) return false;

                    MDLSubMeshInfo smi;
                    smi.MaterialIndex = (uint8_t)(matIdxRaw & 0xFF);
                    smi.FaceCount = faceCount;
                    smi.StartIndex = startIdx;
                    submeshes.push_back(smi);
                }
            }

            size_t vert_off2=0, face_off2=0;
            if(vtx2>0 && vtx2<65535u){
                vert_off2=r.i;
                size_t vsz2=(size_t)vtx2*28;
                if(!r.skip(vsz2)) return false;
            }
            if(tlen2>0 && tlen2<65535u){
                face_off2=r.i;
                size_t fsz2=(size_t)tlen2*2;
                if(!r.skip(fsz2)) return false;
            }
            if(vtx2>0 && vtx2<65535u){
                size_t usz2=(size_t)vtx2*16;
                if(!r.skip(usz2)) return false;
            }

            MDLMeshBufferInfo mb;
            mb.VertexCount=vtx2;
            mb.VertexOffset=vert_off2;
            mb.FaceCount=tlen2;
            mb.FaceOffset=face_off2;
            mb.SubMeshCount=sub2;
            mb.IsAltPath=false;
            mb.SubMeshes=submeshes;
            mb.MeshIndex=mi;
            out.MeshBuffers.push_back(mb);
        }
    }

    return true;
}
