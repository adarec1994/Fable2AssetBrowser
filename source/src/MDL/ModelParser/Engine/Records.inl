struct MDLEngMat {
    std::string diffuse, specular, normal, metallic, extra;
};
struct MDLEngMeshHdr {
    std::string name;
    std::vector<MDLEngMat> mats;
};
struct MDLEngSub {
    uint8_t  MatIdx   = 0;
    uint32_t StartIdx = 0;
    uint32_t RegionIndex = 0;
};
struct MDLEngRec {
    std::string MeshName;
    bool     Skinned = false;
    uint32_t IndexCount = 0, VertexCount = 0, Ext1 = 0, MeshIdx = 0;
    size_t   VertexOffset = 0, IndexOffset = 0;
    std::vector<MDLEngSub> Submeshes;
    uint32_t ClothSimVtx = 0;
    size_t   ClothRestOffset = 0;
    size_t   ClothFlagOffset = 0;
    float    ClothDamping = 0.05f;
};

static bool mdl_read_cloth_block(R& r, MDLEngRec* rec){
    uint32_t a16,u16c,nv,a28,a32,a24,n77;
    if(!r.u32be(a16)||!r.u32be(u16c)||!r.u32be(nv)||!r.u32be(a28)
       ||!r.u32be(a32)||!r.u32be(a24)||!r.u32be(n77)) return false;
    (void)a16;(void)a28;(void)a32;(void)a24;
    if(nv>1000000u||u16c>4000000u||n77>4000000u) return false;
    uint32_t A=0,B=0;
    if(!r.u32be(A)||!r.u32be(B)) return false;
    if(A>1000000u||B>64u) return false;
    if(!r.skip((size_t)A*B))  return false;
    if(!r.skip((size_t)A*16)) return false;
    uint32_t nDist,nBendA,nBendB,nVol,nLink;
    if(!r.u32be(nDist)||!r.u32be(nBendA)||!r.u32be(nBendB)
       ||!r.u32be(nVol)||!r.u32be(nLink)) return false;
    float sp[6];
    for(int k=0;k<6;k++) if(!r.f32be(sp[k])) return false;
    if(!r.skip(2))   return false;
    const size_t restOff = r.i;
    if(!r.skip((size_t)nv*12)) return false;
    if(!r.skip((size_t)nv*8))  return false;
    if(!r.skip((size_t)nv*4))  return false;
    if(!r.skip((size_t)nv*16)) return false;
    if(!r.skip((size_t)nv*4))  return false;
    if(!r.skip((size_t)u16c*2))return false;
    if(!r.skip((size_t)n77*4)) return false;
    if(!r.skip((size_t)n77*16))return false;
    const size_t flagOff = r.i;
    if(!r.skip((size_t)nv*1))  return false;
    if(!r.skip((size_t)nDist*32))  return false;
    if(!r.skip((size_t)nBendA*52)) return false;
    if(!r.skip((size_t)nBendB*56)) return false;
    if(!r.skip((size_t)nVol*148))  return false;
    if(!r.skip((size_t)nLink*8))   return false;
    if(!r.skip((size_t)nv*16)) return false;
    if(rec){
        rec->ClothSimVtx    = nv;
        rec->ClothRestOffset= restOff;
        rec->ClothFlagOffset= flagOff;
        rec->ClothDamping   = sp[2];
    }
    return true;
}

static bool mdl_read_submeshes(R& r, uint32_t subc, MDLEngRec& rec){
    rec.Submeshes.clear();
    rec.Submeshes.reserve(subc);
    for(uint32_t s=0;s<subc;s++){
        uint32_t marker,matIdx,faceCount,startIdx; uint8_t flag;
        if(!r.u32be(marker)||!r.u32be(matIdx)||!r.u8(flag)
           ||!r.u32be(faceCount)||!r.u32be(startIdx)) return false;
        if(!r.skip(24)) return false;
        (void)flag;(void)faceCount;
        MDLEngSub sm; sm.MatIdx=(uint8_t)(matIdx & 0xFF); sm.StartIdx=startIdx;
        sm.RegionIndex=marker;
        rec.Submeshes.push_back(sm);
    }
    return true;
}

static bool mdl_read_rigid_record(R& r, bool full, uint32_t sh4, MDLEngRec& rec){
    rec.Skinned = false;
    if(!r.strz(rec.MeshName)) return false;
    uint8_t lead=0; if(!r.u8(lead)) return false; (void)lead;
    uint32_t idx0,idx1,tric;
    if(!r.u32be(idx0)||!r.u32be(idx1)||!r.u32be(tric)
       ||!r.u32be(rec.IndexCount)||!r.u32be(rec.VertexCount)) return false;
    (void)idx0;(void)tric; rec.MeshIdx = idx1;
    if(rec.IndexCount>4000000u||rec.VertexCount>4000000u) return false;
    if(!r.skip(40)) return false;
    uint32_t subc=0; if(!r.u32be(subc)) return false;
    if(subc>100000u) return false;
    if(!mdl_read_submeshes(r, subc, rec)) return false;
    if(full){
        rec.VertexOffset = r.i;
        if(!r.skip((size_t)rec.VertexCount*20)) return false;
        rec.IndexOffset = r.i;
        if(!r.skip((size_t)rec.IndexCount*2)) return false;
        if(!r.skip((size_t)rec.VertexCount*16*sh4)) return false;
    }else{
        rec.VertexOffset = r.i;
        if(!r.skip((size_t)rec.VertexCount*16*sh4)) return false;
        rec.IndexOffset = r.i;
        if(!r.skip((size_t)rec.IndexCount*2)) return false;
    }
    if(!r.u32be(rec.Ext1)) return false;
    if(rec.Ext1==1){ if(!mdl_read_cloth_block(r, &rec)) return false; }
    else if(rec.Ext1!=0) return false;
    return true;
}

static bool mdl_read_skinned_record(R& r, uint32_t sh4, MDLEngRec& rec){
    rec.Skinned = true;
    if(r.i<r.n && r.p[r.i]==0x01) r.i+=1;
    uint32_t idx0,idx1,tric;
    if(!r.u32be(idx0)||!r.u32be(idx1)||!r.u32be(tric)
       ||!r.u32be(rec.IndexCount)||!r.u32be(rec.VertexCount)) return false;
    (void)idx0;(void)tric; rec.MeshIdx = idx1;
    if(rec.IndexCount>4000000u||rec.VertexCount>4000000u) return false;
    uint32_t subc=0; if(!r.u32be(subc)) return false;
    if(subc>100000u) return false;
    if(!mdl_read_submeshes(r, subc, rec)) return false;
    rec.VertexOffset = r.i;
    if(!r.skip((size_t)rec.VertexCount*28)) return false;
    rec.IndexOffset = r.i;
    if(!r.skip((size_t)rec.IndexCount*2))  return false;
    if(!r.skip((size_t)rec.VertexCount*16*sh4)) return false;
    if(!r.u32be(rec.Ext1)) return false;
    if(rec.Ext1==1){ if(!mdl_read_cloth_block(r, &rec)) return false; }
    else if(rec.Ext1!=0) return false;
    uint32_t ext2=0; if(!r.u32be(ext2)) return false;
    if(ext2==1){
        uint32_t a0,a1v,mcount;
        if(!r.u32be(a0)||!r.u32be(a1v)||!r.u32be(mcount)) return false;
        (void)a0;(void)a1v;
        if(mcount>4000000u) return false;
        if(!r.skip((size_t)mcount*20)) return false;
    }else if(ext2!=0) return false;
    return true;
}

static bool parse_mdl_engine_records(const std::vector<unsigned char>& data,
                                     std::vector<MDLEngRec>& out_recs,
                                     std::vector<MDLEngMeshHdr>& out_hdrs,
                                     uint32_t& out_bone_count,
                                     std::vector<std::string>& out_hide_regions){
    out_recs.clear(); out_hdrs.clear(); out_hide_regions.clear();
    out_bone_count = 0;
    if(data.size() < 0x68) return false;
    if(std::memcmp(data.data(), "MeshFile", 8) != 0 &&
       std::memcmp(data.data(), "DefMeshF", 8) != 0) return false;

    R r{data.data(), data.size(), 0};
    auto fail = [&]()->bool{ out_recs.clear(); out_hdrs.clear();
        out_hide_regions.clear(); return false; };
    auto rd_u32_at = [&](size_t off, uint32_t& v)->bool{
        if(off+4>r.n) return false;
        const uint8_t* q=r.p+off;
        v=(uint32_t(q[0])<<24)|(uint32_t(q[1])<<16)|(uint32_t(q[2])<<8)|uint32_t(q[3]);
        return true; };

    uint32_t lodCount=0;
    if(!rd_u32_at(0x38,lodCount)) return false;
    if(lodCount<1||lodCount>3) return false;
    uint32_t lodSizes[3]={0,0,0}; size_t total=0;
    for(uint32_t i=0;i<lodCount;i++){ if(!rd_u32_at(0x3C+i*4,lodSizes[i])) return false; total+=lodSizes[i]; }
    if((size_t)104+total != r.n) return false;

    size_t off = 0x68;
    for(uint32_t lod=0; lod<lodCount; ++lod){
        const size_t sec_end = off + lodSizes[lod];
        if(sec_end>r.n) return fail();
        if(lodSizes[lod]==0){ off=sec_end; continue; }
        r.i = off;

        if(!r.skip(32)) return fail();
        uint32_t bc=0; if(!r.u32be(bc)) return fail();
        if(bc>100000u) return fail();
        if(lod==0) out_bone_count = bc;
        for(uint32_t i=0;i<bc;i++){ std::string nm; if(!r.strz(nm)||!r.skip(4)) return fail(); }
        uint32_t btc=0; if(!r.u32be(btc)) return fail();
        if(btc>100000u) return fail();
        if(!r.skip((size_t)btc*44)) return fail();

        if(!r.skip(40)) return fail();
        uint32_t mc=0,rigid=0,skinned=0,ex52=0,ex240=0;
        if(!r.u32be(mc)||!r.u32be(rigid)||!r.u32be(skinned)
           ||!r.u32be(ex52)||!r.u32be(ex240)) return fail();
        if(mc>100000u||rigid>100000u||skinned>100000u||ex52||ex240) return fail();
        uint8_t flag=0; if(!r.u8(flag)) return fail();

        uint32_t tagc=0; if(!r.u32be(tagc)) return fail();
        if(tagc>100000u) return fail();
        for(uint32_t t=0;t<tagc;t++){ std::string s; if(!r.strz(s)||!r.skip(1)) return fail(); }

        uint32_t shdesc[5];
        for(int i=0;i<5;i++) if(!r.u32be(shdesc[i])) return fail();
        const uint32_t sh4 = shdesc[4];
        if(sh4>16u) return fail();
        uint32_t fc=0; if(!r.u32be(fc)) return fail();
        if(fc>1000000u) return fail();
        if(!r.skip((size_t)fc*4)) return fail();

        uint32_t hrc=0; if(!r.u32be(hrc)) return fail();
        if(hrc>1000000u) return fail();
        for(uint32_t i=0;i<hrc;i++){
            std::string s; if(!r.strz(s)) return fail();
            if(lod==0) out_hide_regions.push_back(std::move(s));
        }

        const bool capture = (lod==0);
        for(uint32_t m=0;m<mc;m++){
            MDLEngMeshHdr h;
            uint32_t sel=0; if(!r.u32be(sel)) return fail();
            if(sel==2){
                if(!r.skip(68)) return fail();
                if(capture) out_hdrs.push_back(std::move(h));
                continue;
            }
            if(!r.strz(h.name)||!r.skip(8)) return fail();
            if(!r.skip(21)) return fail();
            if(!r.skip(4)||!r.skip(12)) return fail();
            uint32_t matc=0; if(!r.u32be(matc)) return fail();
            if(matc>100000u) return fail();
            for(uint32_t j=0;j<matc;j++){
                MDLEngMat mt;
                if(!r.strz(mt.diffuse) ||!r.strz(mt.specular)||!r.strz(mt.normal)
                   ||!r.strz(mt.metallic)||!r.strz(mt.extra))  return fail();
                std::string s;
                for(int k=0;k<4;k++){ if(!r.strz(s)) return fail(); }
                if(!r.skip(8)) return fail();
                remap_known_mdl_texture_path(mt.diffuse);
                remap_known_mdl_texture_path(mt.specular);
                remap_known_mdl_texture_path(mt.normal);
                remap_known_mdl_texture_path(mt.metallic);
                remap_known_mdl_texture_path(mt.extra);
                h.mats.push_back(std::move(mt));
            }
            if(capture) out_hdrs.push_back(std::move(h));
        }

        uint32_t vtxAccum = 0;
        for(uint32_t i=0;i<rigid;i++){
            MDLEngRec rec;
            if(!mdl_read_rigid_record(r, (lod==0)||(bc>0), sh4, rec)) return fail();
            vtxAccum += rec.VertexCount;
            if(capture) out_recs.push_back(std::move(rec));
        }
        for(uint32_t i=0;i<skinned;i++){
            MDLEngRec rec;
            if(!mdl_read_skinned_record(r, sh4, rec)) return fail();
            if(capture) out_recs.push_back(std::move(rec));
        }
        if(flag){ if(!r.skip((size_t)vtxAccum*4)) return fail(); }

        if(r.i != sec_end) return fail();
        off = sec_end;
    }
    return !out_recs.empty();
}
