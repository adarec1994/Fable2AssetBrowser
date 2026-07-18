bool reparse_mdl_missing_buffers_optstr(const std::vector<unsigned char>& data,
                                        MDLInfo& info)
{
    if (info.MeshCount == 0) return false;

    while (info.MeshBuffers.size() < info.MeshCount) {
        MDLMeshBufferInfo empty;
        empty.MeshIndex = uint32_t(info.MeshBuffers.size());
        info.MeshBuffers.push_back(empty);
    }

    R r{ data.data(), data.size(), 0 };
    bool any_added = false;

    for (uint32_t mi = 0; mi < info.MeshCount; ++mi) {
        if (info.MeshBuffers[mi].VertexCount > 0) continue;

        for (size_t pos = 1; pos + 9 < r.n; ++pos) {
            if (data[pos] != 0x01) continue;
            if (data[pos - 1] != 0x00) continue;

            uint32_t cand_id = (uint32_t(data[pos+1]) << 24)
                             | (uint32_t(data[pos+2]) << 16)
                             | (uint32_t(data[pos+3]) <<  8)
                             |  uint32_t(data[pos+4]);
            if (cand_id != mi) continue;

            size_t str_end = pos - 1;
            size_t str_start = str_end;
            while (str_start > 0
                   && data[str_start-1] >= 32
                   && data[str_start-1] < 127) {
                --str_start;
            }
            if (str_end - str_start < 3) continue;

            size_t hdr_pos = pos + 9;
            if (hdr_pos + 12 > r.n) continue;

            r.i = hdr_pos;
            uint32_t someCount1 = 0; if (!r.u32be(someCount1)) continue;
            uint32_t tlen       = 0; if (!r.u32be(tlen))       continue;
            uint32_t vtx        = 0; if (!r.u32be(vtx))        continue;
            if (vtx == 0 || vtx >= 65535u) continue;
            if (tlen == 0 || tlen >= 65535u) continue;
            if (someCount1 >= 65535u) continue;
            if (!r.skip(40)) continue;

            uint32_t submesh_count = 0;
            if (!r.u32be(submesh_count)) continue;
            uint32_t next_value = 0;
            if (!r.u32be(next_value))    continue;

            uint32_t final_submesh_count;
            if (next_value != 0xFFFFFFFFu || submesh_count >= 256u) {
                final_submesh_count = 1;
            } else {
                r.i -= 4;
                final_submesh_count = submesh_count;
            }

            bool marker_found = false;
            for (size_t sp = r.i; sp + 4 <= r.n && sp < r.i + 1024; ++sp) {
                uint32_t m = (uint32_t(r.p[sp])   << 24)
                           | (uint32_t(r.p[sp+1]) << 16)
                           | (uint32_t(r.p[sp+2]) <<  8)
                           |  uint32_t(r.p[sp+3]);
                if (m == 0xFFFFFFFFu) {
                    r.i = sp;
                    marker_found = true;
                    break;
                }
            }
            if (!marker_found) continue;

            std::vector<MDLSubMeshInfo> submeshes;
            bool sub_ok = true;
            for (uint32_t s = 0; s < final_submesh_count && sub_ok; ++s) {
                uint32_t marker = 0;
                if (!r.u32be(marker))    { sub_ok = false; break; }
                uint32_t matIdxRaw = 0;
                if (!r.u32be(matIdxRaw)) { sub_ok = false; break; }
                uint8_t subFlag = 0;
                if (!r.u8(subFlag))      { sub_ok = false; break; }
                uint32_t faceCount = 0;
                if (!r.u32be(faceCount)) { sub_ok = false; break; }
                uint32_t startIdx = 0;
                if (!r.u32be(startIdx))  { sub_ok = false; break; }
                float F4[6];
                for (int k = 0; k < 6 && sub_ok; ++k) {
                    if (!r.f32be(F4[k])) { sub_ok = false; break; }
                }
                if (!sub_ok) break;
                MDLSubMeshInfo smi;
                smi.MaterialIndex = uint8_t(matIdxRaw & 0xFFu);
                smi.FaceCount  = faceCount;
                smi.StartIndex = startIdx;
                submeshes.push_back(smi);
            }
            if (!sub_ok) continue;

            const size_t vert_off = r.i;
            if (vert_off + size_t(vtx) * 20 > r.n) continue;
            r.i += size_t(vtx) * 20;
            const size_t face_off = r.i;
            if (face_off + size_t(tlen) * 2 > r.n) continue;

            MDLMeshBufferInfo mb;
            mb.VertexCount    = vtx;
            mb.VertexOffset   = vert_off;
            mb.FaceCount      = tlen;
            mb.FaceOffset     = face_off;
            mb.SubMeshCount   = final_submesh_count;
            mb.SubMeshes      = submeshes;
            mb.IsAltPath      = true;
            mb.IsFoliagePath  = false;
            mb.MeshIndex      = mi;
            info.MeshBuffers[mi] = mb;
            any_added = true;
            break;
        }
    }

    return any_added;
}
