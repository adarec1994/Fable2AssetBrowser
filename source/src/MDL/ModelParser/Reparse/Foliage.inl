bool reparse_mdl_as_foliage_48b(const std::vector<unsigned char>& data,
                                MDLInfo& info)
{
    if (info.MeshCount != 1) return false;

    R r{ data.data(), data.size(), 0 };

    for (size_t scan = 0; scan + 18 <= r.n; ++scan) {
        r.i = scan;
        uint16_t u16 = 0;
        if (!r.u16be(u16)) break;
        if (u16 != 0) continue;

        uint32_t f0 = 0, f1 = 0, f2 = 0, f3 = 0;
        if (!r.u32be(f0)) break;
        if (!r.u32be(f1)) break;
        if (!r.u32be(f2)) break;
        if (!r.u32be(f3)) break;

        const uint32_t tlen = f2;
        const uint32_t vtx  = f3;
        if (vtx == 0 || vtx > 100000u) continue;
        if (tlen == 0 || tlen > 100000u) continue;

        const size_t needed = size_t(vtx) * 48 + size_t(tlen) * 2;
        if (r.i + needed > r.n) continue;
        if (r.n - (r.i + needed) > 16) continue;

        MDLMeshBufferInfo mb;
        mb.VertexCount         = vtx;
        mb.VertexOffset        = r.i;
        mb.FaceCount           = tlen;
        mb.FaceOffset          = r.i + size_t(vtx) * 48;
        mb.SubMeshCount        = 1;
        mb.IsAltPath           = false;
        mb.IsFoliagePath       = true;
        mb.FoliageVertexStride = 48;
        mb.MeshIndex           = 0;

        info.MeshBuffers.clear();
        info.MeshBuffers.push_back(mb);
        return true;
    }

    for (size_t scan = 0; scan + 18 <= r.n; ++scan) {
        r.i = scan;
        uint16_t u16 = 0;
        if (!r.u16be(u16)) break;
        if (u16 > 255) continue;

        uint32_t f0 = 0, f1 = 0, f2 = 0, f3 = 0;
        if (!r.u32be(f0)) break;
        if (!r.u32be(f1)) break;
        if (!r.u32be(f2)) break;
        if (!r.u32be(f3)) break;

        if (f0 != 0) continue;

        const uint32_t tlen = f2;
        const uint32_t vtx  = f3;
        if (vtx == 0 || vtx > 100000u) continue;
        if (tlen == 0 || tlen > 100000u) continue;

        const size_t needed = size_t(vtx) * 48 + size_t(tlen) * 2;
        if (r.i + needed > r.n) continue;
        if (r.n - (r.i + needed) > 16) continue;

        MDLMeshBufferInfo mb;
        mb.VertexCount         = vtx;
        mb.VertexOffset        = r.i;
        mb.FaceCount           = tlen;
        mb.FaceOffset          = r.i + size_t(vtx) * 48;
        mb.SubMeshCount        = 1;
        mb.IsAltPath           = false;
        mb.IsFoliagePath       = true;
        mb.FoliageVertexStride = 48;
        mb.MeshIndex           = 0;

        info.MeshBuffers.clear();
        info.MeshBuffers.push_back(mb);
        return true;
    }

    return false;
}
