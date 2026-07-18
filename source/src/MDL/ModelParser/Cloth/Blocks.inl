bool parse_mdl_cloth_blocks(const std::vector<unsigned char>& data, MDLInfo& info){
    info.ClothBlocks.clear();
    if(data.size() < 0x68) return false;
    if(std::memcmp(data.data(), "MeshFile", 8) != 0) return false;

    R r{data.data(), data.size(), 0};

    auto rd_u32_at = [&](size_t off, uint32_t& v) -> bool {
        if(off + 4 > r.n) return false;
        const uint8_t* q = r.p + off;
        v = (uint32_t(q[0])<<24)|(uint32_t(q[1])<<16)|(uint32_t(q[2])<<8)|uint32_t(q[3]);
        return true;
    };

    uint32_t lodCount = 0;
    if(!rd_u32_at(0x38, lodCount)) return false;
    if(lodCount < 1 || lodCount > 3) return false;
    uint32_t lodSizes[3] = {0,0,0};
    size_t total = 0;
    for(uint32_t i=0;i<lodCount;i++){
        if(!rd_u32_at(0x3C + i*4, lodSizes[i])) return false;
        total += lodSizes[i];
    }
    if((size_t)104 + total != r.n) return false;

    auto expect_zero = [&](size_t k) -> bool {
        if(!r.need(k)) return false;
        for(size_t j=0;j<k;j++) if(r.p[r.i+j] != 0) return false;
        r.i += k; return true;
    };

    auto parse_cloth = [&](uint32_t rec_idx, uint32_t mesh_idx) -> bool {
        const size_t start = r.i;
        uint32_t a16,u16c,nv,a28,a32,a24,n77;
        if(!r.u32be(a16)||!r.u32be(u16c)||!r.u32be(nv)||!r.u32be(a28)
           ||!r.u32be(a32)||!r.u32be(a24)||!r.u32be(n77)) return false;
        (void)a16; (void)a28; (void)a32;
        if(nv > 1000000u || u16c > 4000000u || n77 > 4000000u) return false;
        uint32_t A=0,B=0;
        if(!r.u32be(A)||!r.u32be(B)) return false;
        if(A > 1000000u || B > 64u) return false;
        const size_t boneid_off = r.i;
        if(!r.skip((size_t)A*B))  return false;
        const size_t weight_off = r.i;
        if(!r.skip((size_t)A*16)) return false;
        uint32_t nDist,nBendA,nBendB,nVol,nLink;
        if(!r.u32be(nDist)||!r.u32be(nBendA)||!r.u32be(nBendB)
           ||!r.u32be(nVol)||!r.u32be(nLink)) return false;
        float sim[6];
        for(int k=0;k<6;k++) if(!r.f32be(sim[k])) return false;
        uint8_t b1,b2; if(!r.u8(b1)||!r.u8(b2)) return false; (void)b1; (void)b2;
        const size_t restpos_off = r.i;
        if(!r.skip((size_t)nv*12)) return false;
        if(!r.skip((size_t)nv*8))  return false;
        if(!r.skip((size_t)nv*4))  return false;
        if(!r.skip((size_t)nv*16)) return false;
        if(!r.skip((size_t)nv*4))  return false;
        const size_t idxlist_off = r.i;
        if(!r.skip((size_t)u16c*2)) return false;
        if(!r.skip((size_t)n77*4))  return false;
        if(!r.skip((size_t)n77*16)) return false;
        if(!r.skip((size_t)nv*1))   return false;
        if(!r.skip((size_t)nDist*32)) return false;
        if(nBendA != 0) return false;
        if(!r.skip((size_t)nBendB*56)) return false;
        if(nVol != 0) return false;
        if(!r.skip((size_t)nLink*8)) return false;
        if(!r.skip((size_t)nv*16)) return false;

        MDLClothInfo c;
        c.RecordIndex = rec_idx;   c.MeshIndex = mesh_idx;
        c.BlockOffset = start;     c.BlockSize = r.i - start;
        c.SimVertexCount = nv;     c.TriangleCount = a24;  c.IndexCount = u16c;
        c.EdgeCount = n77;         c.BonesPerVertex = B;
        c.DistConstraints = nDist; c.BendConstraints = nBendB; c.LinkConstraints = nLink;
        for(int k=0;k<6;k++) c.SimParams[k] = sim[k];
        c.RestPosOffset = restpos_off; c.IndexListOffset = idxlist_off;
        c.BoneIdOffset = boneid_off;   c.WeightOffset = weight_off;
        info.ClothBlocks.push_back(c);
        return true;
    };

    size_t off = 0x68;
    for(uint32_t lod=0; lod<lodCount; ++lod){
        const size_t sec_end = off + lodSizes[lod];
        if(sec_end > r.n) { info.ClothBlocks.clear(); return false; }
        r.i = off;

        if(!expect_zero(32)) { info.ClothBlocks.clear(); return false; }
        uint32_t bc=0; if(!r.u32be(bc)) { info.ClothBlocks.clear(); return false; }
        if(bc > 100000u) { info.ClothBlocks.clear(); return false; }
        for(uint32_t i=0;i<bc;i++){ std::string nm; if(!r.strz(nm)||!r.skip(4)) { info.ClothBlocks.clear(); return false; } }
        uint32_t btc=0; if(!r.u32be(btc)) { info.ClothBlocks.clear(); return false; }
        if(!r.skip((size_t)btc*44)) { info.ClothBlocks.clear(); return false; }
        if(!r.skip(40)) { info.ClothBlocks.clear(); return false; }
        uint32_t mc=0,rigid=0,skinned=0;
        if(!r.u32be(mc)||!r.u32be(rigid)||!r.u32be(skinned)) { info.ClothBlocks.clear(); return false; }
        if(!expect_zero(12)) { info.ClothBlocks.clear(); return false; }
        uint8_t tagc=0; if(!r.u8(tagc)) { info.ClothBlocks.clear(); return false; }
        for(uint8_t t=0;t<tagc;t++){ std::string s; if(!r.strz(s)||!r.skip(1)) { info.ClothBlocks.clear(); return false; } }
        if(!r.skip(20)) { info.ClothBlocks.clear(); return false; }
        uint32_t pcount=0; if(!r.u32be(pcount)) { info.ClothBlocks.clear(); return false; }
        if(!r.skip((size_t)pcount*4)) { info.ClothBlocks.clear(); return false; }
        uint32_t sbc=0; if(!r.u32be(sbc)) { info.ClothBlocks.clear(); return false; }
        if(sbc > 1000000u) { info.ClothBlocks.clear(); return false; }
        for(uint32_t i=0;i<sbc;i++){ std::string s; if(!r.strz(s)) { info.ClothBlocks.clear(); return false; } }

        if(mc > 100000u) { info.ClothBlocks.clear(); return false; }
        for(uint32_t m=0;m<mc;m++){
            std::string nm;
            if(!r.skip(4)||!r.strz(nm)||!r.skip(8)) { info.ClothBlocks.clear(); return false; }
            if(!expect_zero(21)) { info.ClothBlocks.clear(); return false; }
            if(!r.skip(4)||!r.skip(12)) { info.ClothBlocks.clear(); return false; }
            uint32_t matc=0; if(!r.u32be(matc)) { info.ClothBlocks.clear(); return false; }
            if(matc > 100000u) { info.ClothBlocks.clear(); return false; }
            for(uint32_t j=0;j<matc;j++){
                for(int t=0;t<5;t++){ std::string s; if(!r.strz(s)) { info.ClothBlocks.clear(); return false; } }
                if(!r.skip(12)) { info.ClothBlocks.clear(); return false; }
                if(r.i < r.n && r.p[r.i] == 0x01) r.i += 1;
            }
        }

        if(rigid != 0) { info.ClothBlocks.clear(); return false; }
        const uint32_t rec_count = rigid + skinned;
        if(rec_count > 100000u) { info.ClothBlocks.clear(); return false; }
        for(uint32_t mi=0; mi<rec_count; ++mi){
            if(r.i < r.n && r.p[r.i] == 0x01) r.i += 1;
            uint32_t idx0,idx1,tric,idxc,vtxc;
            if(!r.u32be(idx0)||!r.u32be(idx1)||!r.u32be(tric)||!r.u32be(idxc)||!r.u32be(vtxc))
                { info.ClothBlocks.clear(); return false; }
            (void)idx0; (void)tric;
            uint32_t subc=0; if(!r.u32be(subc)) { info.ClothBlocks.clear(); return false; }
            if(subc > 100000u) { info.ClothBlocks.clear(); return false; }
            if(!r.skip((size_t)subc*41)) { info.ClothBlocks.clear(); return false; }
            if(!r.skip((size_t)vtxc*28)) { info.ClothBlocks.clear(); return false; }
            if(!r.skip((size_t)idxc*2))  { info.ClothBlocks.clear(); return false; }
            if(!r.skip((size_t)vtxc*16)) { info.ClothBlocks.clear(); return false; }
            uint32_t ext1=0; if(!r.u32be(ext1)) { info.ClothBlocks.clear(); return false; }
            if(ext1 == 1){
                if(!parse_cloth(mi, idx1)) { info.ClothBlocks.clear(); return false; }
            } else if(ext1 != 0){
                info.ClothBlocks.clear(); return false;
            }
            uint32_t ext2=0; if(!r.u32be(ext2)) { info.ClothBlocks.clear(); return false; }
            if(ext2 != 0) { info.ClothBlocks.clear(); return false; }
        }
        if(r.i != sec_end) { info.ClothBlocks.clear(); return false; }
        off = sec_end;
    }
    return true;
}
