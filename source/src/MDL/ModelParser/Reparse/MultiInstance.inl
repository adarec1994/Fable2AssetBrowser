bool reparse_mdl_multi_instance_buffers(const std::vector<unsigned char>& data,
                                        MDLInfo& info)
{
    if (info.MeshCount == 0 || info.Meshes.empty()) return false;
    if (data.size() < 32) return false;

    auto rd_u32be = [](const uint8_t* p) -> uint32_t {
        return  (uint32_t(p[0]) << 24) |
                (uint32_t(p[1]) << 16) |
                (uint32_t(p[2]) <<  8) |
                 uint32_t(p[3]);
    };

    const size_t n = data.size();
    const uint8_t* base = data.data();

    std::string opt_str;
    std::vector<size_t> header_positions;
    {
        std::unordered_map<std::string, std::vector<size_t>> by_str;
        for (size_t pos = 1; pos + 16 < n; ++pos) {
            if (base[pos] != 0x00) continue;
            if (base[pos+1] != 0x01) continue;
            const uint32_t mid  = rd_u32be(base + pos + 2);
            const uint32_t mid2 = rd_u32be(base + pos + 6);
            if (mid > 100000u) continue;
            if (mid2 >= info.MeshCount) continue;

            size_t str_end = pos;
            size_t str_start = str_end;
            while (str_start > 0 &&
                   base[str_start - 1] >= 32 &&
                   base[str_start - 1] < 127) {
                --str_start;
            }
            if (str_end - str_start < 3) continue;
            if (str_end - str_start > 64) continue;

            std::string s(base + str_start, base + str_end);
            by_str[s].push_back(str_start);
        }
        size_t best = 0;
        for (auto& kv : by_str) {
            if (kv.second.size() > best) {
                best = kv.second.size();
                opt_str = kv.first;
                header_positions = std::move(kv.second);
            }
        }
    }
    if (header_positions.size() <= info.MeshBuffers.size()) return false;

    std::sort(header_positions.begin(), header_positions.end());

    const std::vector<MDLMeshInfo> base_meshes = info.Meshes;
    std::vector<MDLMeshBufferInfo> new_buffers;
    std::vector<MDLMeshInfo>       new_meshes;
    new_buffers.reserve(header_positions.size());
    new_meshes.reserve(header_positions.size());

    R r{ data.data(), data.size(), 0 };
    for (size_t hpos : header_positions) {
        size_t after_str = hpos + opt_str.size();
        if (after_str + 10 > n) continue;
        if (base[after_str] != 0x00) continue;
        if (base[after_str + 1] != 0x01) continue;

        const uint32_t mid  = rd_u32be(base + after_str + 2);
        const uint32_t mid2 = rd_u32be(base + after_str + 6);
        if (mid2 >= base_meshes.size()) continue;
        (void)mid;

        r.i = after_str + 10;
        uint32_t someCount1 = 0;
        if (!r.u32be(someCount1)) continue;
        uint32_t tlen = 0;
        if (!r.u32be(tlen)) continue;
        uint32_t vtx = 0;
        if (!r.u32be(vtx)) continue;
        if (vtx == 0 || vtx >= 65535u) continue;
        if (tlen == 0 || tlen >= 65535u) continue;
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
            if (m == 0xFFFFFFFFu) { r.i = sp; marker_found = true; break; }
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
        mb.MeshIndex      = uint32_t(new_buffers.size());

        new_buffers.push_back(mb);
        new_meshes.push_back(base_meshes[mid2]);
    }

    if (new_buffers.size() <= info.MeshBuffers.size()) return false;

    info.MeshBuffers = std::move(new_buffers);
    info.Meshes      = std::move(new_meshes);
    info.MeshCount   = uint32_t(info.Meshes.size());
    return true;
}
