if(is_foliage){

    bool has_class_tag = false;
    std::string class_tag;
    if(r.i < r.n){
        uint8_t b0 = r.p[r.i];
        bool starts_alpha = (b0 >= 'A' && b0 <= 'Z') || (b0 >= 'a' && b0 <= 'z');
        if(starts_alpha){
            size_t save = r.i;
            size_t scan_lim = std::min(r.n, r.i + 64);
            bool valid = false;
            for(size_t p = r.i; p < scan_lim; ++p){
                uint8_t b = r.p[p];
                if(b == 0){ valid = (p > r.i); break; }
                if(b < 32 || b >= 127){ valid = false; break; }
            }
            if(valid){
                r.strz(class_tag, 64);
                if(r.i + 3 <= r.n){
                    if(!r.skip(3)) return false;
                    has_class_tag = true;
                } else {
                    r.i = save;
                }
            }
        }
    }

    uint16_t unk2bytes = 0;
    if(!r.u16be(unk2bytes)) return false;

    bool is_tree_foliage = false;
    {
        size_t save = r.i;
        uint8_t peek = 0;
        if(r.u8(peek) && peek == 0x01){
            uint32_t check_f0 = 0;
            if(r.u32be(check_f0) && check_f0 == 0) is_tree_foliage = true;
        } else {
        }
        r.i = save;
    }

    if(is_tree_foliage){
        uint8_t tree_flag = 0;
        if(!r.u8(tree_flag)) return false;

        for(uint32_t mi=0; mi<out.MeshCount; ++mi){

            uint32_t fields[4] = {0,0,0,0};
            for(int k=0;k<4;k++) if(!r.u32be(fields[k])) return false;

            uint32_t vtx     = fields[3];
            uint32_t face_ic = fields[2];

            MDLMeshBufferInfo mb;
            mb.VertexCount       = vtx;
            mb.VertexOffset      = r.i;
            mb.IsFoliagePath     = true;
            mb.FoliageVertexStride = 36;
            mb.IsAltPath         = false;
            mb.SubMeshCount      = 1;
            mb.MeshIndex         = mi;

            if(vtx > 0 && vtx < 100000u)
                if(!r.skip((size_t)vtx * 36)) return false;

            mb.FaceCount  = face_ic;
            mb.FaceOffset = r.i;

            if(face_ic > 0 && face_ic < 100000u)
                if(!r.skip((size_t)face_ic * 2)) return false;

            if(vtx > 0 && vtx < 100000u)
                if(!r.skip((size_t)vtx * 16)) return false;

            if(mi + 1 < out.MeshCount)
                if(!r.skip(2)) return false;

            out.MeshBuffers.push_back(mb);
        }
    } else {

        for(uint32_t mi=0; mi<out.MeshCount; ++mi){
            uint32_t fields[4] = {0,0,0,0};
            for(int k=0;k<4;k++) if(!r.u32be(fields[k])) return false;

            uint32_t vtx     = fields[3];
            uint32_t face_ic = fields[2];

            uint32_t stride_used = 0;
            size_t vert_off = 0, face_off = 0;

            bool sane = (vtx > 0 && vtx < 200000u) && (face_ic > 0 && face_ic < 200000u);
            bool fits_48 = sane && (r.i + (size_t)vtx * 48 + (size_t)face_ic * 2 <= r.n);

            if(!sane){

                MDLMeshBufferInfo mb;
                mb.VertexCount = 0; mb.VertexOffset = 0;
                mb.FaceCount = 0; mb.FaceOffset = 0;
                mb.SubMeshCount = 1; mb.IsAltPath = false;
                mb.IsFoliagePath = true; mb.FoliageVertexStride = 0;
                mb.MeshIndex = mi;
                out.MeshBuffers.push_back(mb);

                for(uint32_t mj = mi + 1; mj < out.MeshCount; ++mj){
                    MDLMeshBufferInfo mb2;
                    mb2.VertexCount = 0; mb2.VertexOffset = 0;
                    mb2.FaceCount = 0; mb2.FaceOffset = 0;
                    mb2.SubMeshCount = 1; mb2.IsAltPath = false;
                    mb2.IsFoliagePath = true; mb2.FoliageVertexStride = 0;
                    mb2.MeshIndex = mj;
                    out.MeshBuffers.push_back(mb2);
                }
                return true;
            }

            if(fits_48 && !has_class_tag){

                vert_off = r.i;
                if(!r.skip((size_t)vtx * 48)) return false;
                face_off = r.i;
                if(!r.skip((size_t)face_ic * 2)) return false;
                stride_used = 48;
            }

            std::vector<MDLSubMeshInfo> bw_subs;
            if(stride_used == 0 && has_class_tag){

                size_t hdr_start = r.i;
                bool bw_ok = false;
                if(r.i + 40 + 4 <= r.n){
                    if(!r.skip(40)) return false;
                    uint32_t sub_count = 0;
                    if(r.u32be(sub_count) && sub_count > 0 && sub_count <= 64){
                        size_t need = (size_t)sub_count * 41
                                    + (size_t)vtx * 20
                                    + (size_t)face_ic * 2;
                        if(r.i + need <= r.n){

                            bw_subs.reserve(sub_count);
                            bool sub_ok = true;
                            for(uint32_t s = 0; s < sub_count && sub_ok; ++s){
                                uint32_t sub_id = 0, sub_unk = 0;
                                uint8_t  sub_mat = 0;
                                uint32_t sub_face = 0, sub_start = 0;
                                if(!r.u32be(sub_id))    { sub_ok = false; break; }
                                if(!r.u32be(sub_unk))   { sub_ok = false; break; }
                                if(!r.u8(sub_mat))      { sub_ok = false; break; }
                                if(!r.u32be(sub_face))  { sub_ok = false; break; }
                                if(!r.u32be(sub_start)) { sub_ok = false; break; }
                                if(!r.skip(24))         { sub_ok = false; break; }
                                MDLSubMeshInfo si;
                                si.FaceCount = sub_face;
                                si.StartIndex = sub_start;
                                si.MaterialIndex = sub_mat;
                                bw_subs.push_back(si);
                            }
                            if(sub_ok){
                                vert_off = r.i;
                                if(!r.skip((size_t)vtx * 20)) return false;
                                face_off = r.i;
                                if(!r.skip((size_t)face_ic * 2)) return false;
                                stride_used = 20;
                                bw_ok = true;
                            }
                        } else {
                        }
                    }
                }
                if(!bw_ok){

                    r.i = hdr_start;
                    if(fits_48){

                        vert_off = r.i;
                        if(!r.skip((size_t)vtx * 48)) return false;
                        face_off = r.i;
                        if(!r.skip((size_t)face_ic * 2)) return false;
                        stride_used = 48;
                    } else {
                        stride_used = 0;
                        vert_off = hdr_start;
                        face_off = 0;

                        MDLMeshBufferInfo mb;
                        mb.VertexCount = vtx; mb.VertexOffset = vert_off;
                        mb.FaceCount = face_ic; mb.FaceOffset = face_off;
                        mb.SubMeshCount = 1; mb.IsAltPath = false;
                        mb.IsFoliagePath = true; mb.FoliageVertexStride = 0;
                        mb.MeshIndex = mi;
                        out.MeshBuffers.push_back(mb);
                        for(uint32_t mj = mi + 1; mj < out.MeshCount; ++mj){
                            MDLMeshBufferInfo mb2;
                            mb2.VertexCount = 0; mb2.VertexOffset = 0;
                            mb2.FaceCount = 0; mb2.FaceOffset = 0;
                            mb2.SubMeshCount = 1; mb2.IsAltPath = false;
                            mb2.IsFoliagePath = true; mb2.FoliageVertexStride = 0;
                            mb2.MeshIndex = mj;
                            out.MeshBuffers.push_back(mb2);
                        }
                        return true;
                    }
                }
            }

            if(stride_used == 0){

                return false;
            }

            MDLMeshBufferInfo mb;
            mb.VertexCount       = vtx;
            mb.VertexOffset      = vert_off;
            mb.FaceCount         = face_ic;
            mb.FaceOffset        = face_off;
            mb.SubMeshCount      = bw_subs.empty() ? 1 : (uint32_t)bw_subs.size();
            mb.SubMeshes         = bw_subs;
            mb.IsAltPath         = false;
            mb.IsFoliagePath     = true;
            mb.FoliageVertexStride = stride_used;
            mb.MeshIndex         = mi;
            out.MeshBuffers.push_back(mb);
        }
    }
    return true;
}

