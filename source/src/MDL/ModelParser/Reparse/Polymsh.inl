bool reparse_mdl_buffers_via_polymsh_scan(const std::vector<unsigned char>& data,
                                          MDLInfo& info)
{
    if (data.size() < 16) return false;

    static const unsigned char kMagic[9] = {
        'p','o','l','y','m','s','h','\0','\x01'
    };

    std::vector<MDLMeshBufferInfo> recovered;
    R r{ data.data(), data.size(), 0 };

    for (size_t scan = 0; scan + sizeof(kMagic) <= data.size(); ++scan) {
        if (std::memcmp(data.data() + scan, kMagic, sizeof(kMagic)) != 0) continue;

        r.i = scan + sizeof(kMagic);
        uint32_t mesh_id = 0, mesh_id_copy = 0;
        if (!r.u32be(mesh_id))      continue;
        if (!r.u32be(mesh_id_copy)) continue;

        uint32_t someCount1 = 0, tlen = 0, vtx = 0;
        if (!r.u32be(someCount1) || !r.u32be(tlen) || !r.u32be(vtx)) continue;
        if (vtx == 0 || vtx > 65535u || tlen == 0 || tlen > 65535u) continue;
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

        bool markerFound = false;
        for (size_t sp = r.i; sp + 4 <= r.n && sp < r.i + 1024; ++sp) {
            uint32_t m = (uint32_t(r.p[sp])<<24) | (uint32_t(r.p[sp+1])<<16) |
                         (uint32_t(r.p[sp+2])<<8) | r.p[sp+3];
            if (m == 0xFFFFFFFFu) { r.i = sp; markerFound = true; break; }
        }
        if (!markerFound) continue;

        std::vector<MDLSubMeshInfo> submeshes;
        bool sub_ok = true;
        for (uint32_t s = 0; s < final_submesh_count && sub_ok; ++s) {
            uint32_t marker = 0;
            if (!r.u32be(marker)) { sub_ok = false; break; }
            uint32_t matIdxRaw = 0;
            if (!r.u32be(matIdxRaw)) { sub_ok = false; break; }
            uint8_t subFlag = 0;
            if (!r.u8(subFlag))      { sub_ok = false; break; }
            (void)subFlag;
            uint32_t faceCount = 0, startIdx = 0;
            if (!r.u32be(faceCount)) { sub_ok = false; break; }
            if (!r.u32be(startIdx))  { sub_ok = false; break; }
            float F4[6];
            for (int k = 0; k < 6 && sub_ok; ++k)
                if (!r.f32be(F4[k])) { sub_ok = false; break; }
            if (!sub_ok) break;

            MDLSubMeshInfo smi;
            smi.RegionIndex = marker;
            smi.MaterialIndex = (uint8_t)(matIdxRaw & 0xFFu);
            smi.FaceCount  = faceCount;
            smi.StartIndex = startIdx;
            submeshes.push_back(smi);
        }
        if (!sub_ok) continue;

        const size_t vert_off = r.i;
        if (!r.skip((size_t)vtx * 20)) continue;
        const size_t face_off = r.i;
        if (!r.skip((size_t)tlen * 2)) continue;

        MDLMeshBufferInfo mb;
        mb.VertexCount   = vtx;
        mb.VertexOffset  = vert_off;
        mb.FaceCount     = tlen;
        mb.FaceOffset    = face_off;
        mb.SubMeshCount  = final_submesh_count;
        mb.SubMeshes     = submeshes;
        mb.IsAltPath     = true;
        mb.IsFoliagePath = false;
        mb.MeshIndex     = (uint32_t)recovered.size();
        recovered.push_back(std::move(mb));

        scan = r.i - 1;
    }

    if (recovered.empty()) return false;

    info.MeshBuffers.clear();
    info.MeshBuffers.reserve(info.Meshes.size());
    for (size_t mi = 0; mi < info.Meshes.size(); ++mi) {
        if (mi < recovered.size()) {
            MDLMeshBufferInfo mb = recovered[mi];
            mb.MeshIndex = (uint32_t)mi;
            info.MeshBuffers.push_back(std::move(mb));
        } else {
            MDLMeshBufferInfo mb;
            mb.MeshIndex = (uint32_t)mi;
            info.MeshBuffers.push_back(std::move(mb));
        }
    }
    if (info.MeshCount == 0 || info.MeshCount < (uint32_t)info.Meshes.size()) {
        info.MeshCount = (uint32_t)info.Meshes.size();
    }
    return true;
}
