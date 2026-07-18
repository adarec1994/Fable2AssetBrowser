struct ClothSim {
    std::vector<float>    bind;
    std::vector<uint8_t>  bid;
    std::vector<float>    bw;
    std::vector<uint32_t> idx;
    std::vector<uint32_t> e0, e1;
    std::vector<float>    rest;
    std::vector<uint8_t>  pinned;
    std::vector<float>    pos, prev;
    std::vector<MPVertex> vtx;
    float scale = 1.0f;
    float damping = 0.05f;
    bool  inited = false;
};

static std::shared_ptr<ClothSim> mp_build_cloth(const MDLMeshGeom& g,
                                                std::vector<MPVertex>&& vtx){
    const size_t N = g.positions.size()/3;
    if(N==0 || g.indices.size()<3) return nullptr;
    auto c = std::make_shared<ClothSim>();
    c->bind = g.positions;
    c->idx  = g.indices;
    c->vtx  = std::move(vtx);
    const bool hasBI = (g.bone_ids.size()==N*4), hasBW = (g.bone_weights.size()==N*4);
    c->bid.assign(N*4,0); c->bw.assign(N*4,0.0f);
    for(size_t v=0;v<N;v++) for(int k=0;k<4;k++){
        uint32_t id = hasBI ? g.bone_ids[v*4+k] : 0; if(id>=MP_MAX_BONES) id=0;
        c->bid[v*4+k]=(uint8_t)id;
        c->bw[v*4+k] = hasBW ? g.bone_weights[v*4+k] : (k==0?1.0f:0.0f);
    }
    std::unordered_set<uint64_t> seen;
    auto addEdge=[&](uint32_t a,uint32_t b){
        if(a==b) return; if(a>b) std::swap(a,b);
        if(!seen.insert(((uint64_t)a<<32)|b).second) return;
        const float dx=g.positions[a*3]-g.positions[b*3];
        const float dy=g.positions[a*3+1]-g.positions[b*3+1];
        const float dz=g.positions[a*3+2]-g.positions[b*3+2];
        c->e0.push_back(a); c->e1.push_back(b);
        c->rest.push_back(std::sqrt(dx*dx+dy*dy+dz*dz));
    };
    for(size_t t=0;t+2<c->idx.size();t+=3){
        addEdge(c->idx[t],c->idx[t+1]); addEdge(c->idx[t+1],c->idx[t+2]); addEdge(c->idx[t+2],c->idx[t]);
    }
    float mnx=1e30f,mny=1e30f,mnz=1e30f,mxx=-1e30f,mxy=-1e30f,mxz=-1e30f;
    for(size_t v=0;v<N;v++){
        float x=g.positions[v*3],y=g.positions[v*3+1],z=g.positions[v*3+2];
        mnx=std::min(mnx,x);mny=std::min(mny,y);mnz=std::min(mnz,z);
        mxx=std::max(mxx,x);mxy=std::max(mxy,y);mxz=std::max(mxz,z);
    }
    c->scale = std::sqrt((mxx-mnx)*(mxx-mnx)+(mxy-mny)*(mxy-mny)+(mxz-mnz)*(mxz-mnz));
    if(c->scale<1e-4f) c->scale=1.0f;
    c->damping = g.cloth_damping;

    c->pinned.assign(N,0);
    size_t realPins=0;
    if(g.cloth_pin.size()==N)
        for(size_t v=0;v<N;v++) if(g.cloth_pin[v]){ c->pinned[v]=1; ++realPins; }
    if(!(realPins>0 && realPins < N*9/10)) std::fill(c->pinned.begin(),c->pinned.end(),0);

    std::vector<uint32_t> par(N); for(uint32_t v=0;v<(uint32_t)N;v++) par[v]=v;
    std::function<uint32_t(uint32_t)> find=[&](uint32_t x){ while(par[x]!=x){ par[x]=par[par[x]]; x=par[x]; } return x; };
    for(size_t e=0;e<c->e0.size();e++){ uint32_t a=find(c->e0[e]),b=find(c->e1[e]); if(a!=b) par[a]=b; }
    std::unordered_map<uint32_t,float> cMax,cMin; std::unordered_map<uint32_t,uint8_t> cPinned;
    for(uint32_t v=0;v<(uint32_t)N;v++){
        uint32_t r=find(v); float y=g.positions[v*3+1];
        auto it=cMax.find(r);
        if(it==cMax.end()){ cMax[r]=y; cMin[r]=y; } else { it->second=std::max(it->second,y); cMin[r]=std::min(cMin[r],y); }
        if(c->pinned[v]) cPinned[r]=1;
    }
    for(uint32_t v=0;v<(uint32_t)N;v++){
        uint32_t r=find(v); if(cPinned.count(r)) continue;
        float t = cMax[r] - (cMax[r]-cMin[r])*0.18f;
        if(g.positions[v*3+1]>=t) c->pinned[v]=1;
    }
    c->pos.assign(N*3,0.0f); c->prev.assign(N*3,0.0f);
    return c;
}

static void mp_step_cloth(ClothSim& c, const XMFLOAT4X4* bmats, uint32_t nbones){
    const size_t N = c.pos.size()/3;
    std::vector<float> tgt(N*3);
    for(size_t v=0; v<N; ++v){
        XMMATRIX acc = XMMatrixSet(0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0);
        float wsum=0.0f;
        for(int k=0;k<4;k++){
            float w=c.bw[v*4+k]; if(w<=0.0f) continue;
            uint32_t id=c.bid[v*4+k]; if(id>=nbones) id=0;
            acc = acc + XMLoadFloat4x4(&bmats[id])*w; wsum+=w;
        }
        if(wsum<1e-4f) acc = XMMatrixIdentity();
        XMVECTOR wp = XMVector4Transform(XMVectorSet(c.bind[v*3],c.bind[v*3+1],c.bind[v*3+2],1.0f), acc);
        tgt[v*3]=XMVectorGetX(wp); tgt[v*3+1]=XMVectorGetY(wp); tgt[v*3+2]=XMVectorGetZ(wp);
    }
    if(!c.inited){ c.pos=tgt; c.prev=tgt; c.inited=true; }

    const float damp = 1.0f - std::min(std::max(c.damping,0.0f),0.4f);
    const float grav = c.scale * 0.0016f;
    const float attract = 0.03f;
    for(size_t v=0;v<N;v++){
        if(c.pinned[v]){
            for(int a=0;a<3;a++){ c.pos[v*3+a]=tgt[v*3+a]; c.prev[v*3+a]=tgt[v*3+a]; }
            continue;
        }
        for(int a=0;a<3;a++){
            float x=c.pos[v*3+a], px=c.prev[v*3+a];
            float nx = x + (x-px)*damp + (a==1 ? -grav : 0.0f);
            nx += (tgt[v*3+a]-x)*attract;
            c.prev[v*3+a]=x; c.pos[v*3+a]=nx;
        }
    }
    for(int it=0; it<10; ++it){
        for(size_t e=0;e<c.rest.size();e++){
            const uint32_t a=c.e0[e], b=c.e1[e];
            float dx=c.pos[b*3]-c.pos[a*3], dy=c.pos[b*3+1]-c.pos[a*3+1], dz=c.pos[b*3+2]-c.pos[a*3+2];
            float d=std::sqrt(dx*dx+dy*dy+dz*dz); if(d<1e-6f) continue;
            float wa=c.pinned[a]?0.0f:1.0f, wb=c.pinned[b]?0.0f:1.0f, ws=wa+wb; if(ws<1e-6f) continue;
            float diff=(d-c.rest[e])/d, sa=wa/ws, sb=wb/ws;
            c.pos[a*3]+=dx*diff*sa; c.pos[a*3+1]+=dy*diff*sa; c.pos[a*3+2]+=dz*diff*sa;
            c.pos[b*3]-=dx*diff*sb; c.pos[b*3+1]-=dy*diff*sb; c.pos[b*3+2]-=dz*diff*sb;
        }
    }
    std::vector<float> nrm(N*3,0.0f);
    for(size_t t=0;t+2<c.idx.size();t+=3){
        uint32_t ia=c.idx[t],ib=c.idx[t+1],ic=c.idx[t+2];
        float ax=c.pos[ia*3],ay=c.pos[ia*3+1],az=c.pos[ia*3+2];
        float ux=c.pos[ib*3]-ax,uy=c.pos[ib*3+1]-ay,uz=c.pos[ib*3+2]-az;
        float vx=c.pos[ic*3]-ax,vy=c.pos[ic*3+1]-ay,vz=c.pos[ic*3+2]-az;
        float nx=uy*vz-uz*vy, ny=uz*vx-ux*vz, nz=ux*vy-uy*vx;
        nrm[ia*3]+=nx;nrm[ia*3+1]+=ny;nrm[ia*3+2]+=nz;
        nrm[ib*3]+=nx;nrm[ib*3+1]+=ny;nrm[ib*3+2]+=nz;
        nrm[ic*3]+=nx;nrm[ic*3+1]+=ny;nrm[ic*3+2]+=nz;
    }
    for(size_t v=0;v<N && v<c.vtx.size();v++){
        c.vtx[v].px=c.pos[v*3]; c.vtx[v].py=c.pos[v*3+1]; c.vtx[v].pz=c.pos[v*3+2];
        float x=nrm[v*3],y=nrm[v*3+1],z=nrm[v*3+2], l=std::sqrt(x*x+y*y+z*z);
        if(l>1e-6f){ c.vtx[v].nx=x/l; c.vtx[v].ny=y/l; c.vtx[v].nz=z/l; }
    }
}
