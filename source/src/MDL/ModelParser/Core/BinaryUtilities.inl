struct R {
    const uint8_t* p=nullptr; size_t n=0; size_t i=0;
    bool need(size_t k) const { return i+k<=n; }
    bool u8 (uint8_t& v){ if(!need(1)) return false; v=p[i++]; return true; }
    bool u16be(uint16_t& v){ if(!need(2)) return false; const uint8_t* q=p+i; i+=2; v=(uint16_t(q[0])<<8)|uint16_t(q[1]); return true; }
    bool u32be(uint32_t& v){ if(!need(4)) return false; const uint8_t* q=p+i; i+=4; v=(uint32_t(q[0])<<24)|(uint32_t(q[1])<<16)|(uint32_t(q[2])<<8)|uint32_t(q[3]); return true; }
    bool f32be(float& f){ uint32_t u; if(!u32be(u)) return false; std::memcpy(&f,&u,4); return true; }
    bool strz(std::string& s, size_t maxlen=8192){ s.clear(); size_t lim=std::min(n,i+maxlen); while(i<lim){ char c=(char)p[i++]; if(c==0) return true; s.push_back(c);} return true; }
    bool skip(size_t k){ if(!need(k)) return false; i+=k; return true; }
};
static inline float half_to_float(uint16_t h){
    uint32_t s=(h>>15)&1u, e=(h>>10)&0x1Fu, f=h&0x3FFu, E, F;
    if(e==0){ if(f==0){E=0;F=0;} else{ int t=0; while((f&0x400u)==0){ f<<=1; t++; } f&=0x3FFu; E=127-15-t; F=f<<13; } }
    else if(e==31){ E=255; F=f?0x7FFFFF:0; }
    else{ E=e+(127-15); F=f<<13; }
    uint32_t u=(s<<31)|(E<<23)|F; float r; std::memcpy(&r,&u,4); return r;
}
static void build_triangles_from_strip(const std::vector<uint16_t>& strip, std::vector<uint32_t>& out_idx){
    out_idx.clear(); if(strip.size()<3) return;
    const uint16_t RESTART=0xFFFF; bool wind=false; uint16_t a=strip[0], b=strip[1];
    for(size_t i=2;i<strip.size();++i){
        uint16_t c=strip[i];
        if(a==RESTART||b==RESTART||c==RESTART){
            size_t j=i+1; while(j<strip.size() && strip[j]==RESTART) j++;
            if(j+1<strip.size()){ a=strip[j]; b=strip[j+1]; i=j+1; wind=false; continue; } else break;
        }
        if(a!=b && b!=c && c!=a){
            if(!wind){ out_idx.push_back(a); out_idx.push_back(b); out_idx.push_back(c); }
            else{ out_idx.push_back(b); out_idx.push_back(a); out_idx.push_back(c); }
        }
        a=b; b=c; wind=!wind;
    }
}
static void compute_smooth_normals(size_t vcount, const std::vector<uint32_t>& idx, const std::vector<float>& pos, std::vector<float>& out_n){
    out_n.assign(vcount*3,0.0f);
    for(size_t i=0;i+2<idx.size(); i+=3){
        uint32_t ia=idx[i], ib=idx[i+1], ic=idx[i+2];
        if((size_t)ia*3+2>=pos.size()||(size_t)ib*3+2>=pos.size()||(size_t)ic*3+2>=pos.size()) continue;
        float ax=pos[ia*3+0], ay=pos[ia*3+1], az=pos[ia*3+2];
        float bx=pos[ib*3+0], by=pos[ib*3+1], bz=pos[ib*3+2];
        float cx=pos[ic*3+0], cy=pos[ic*3+1], cz=pos[ic*3+2];
        float ux=bx-ax, uy=by-ay, uz=bz-az;
        float vx=cx-ax, vy=cy-ay, vz=cz-az;
        float nx=uy*vz-uz*vy, ny=uz*vx-ux*vz, nz=ux*vy-uy*vx;
        out_n[ia*3+0]+=nx; out_n[ia*3+1]+=ny; out_n[ia*3+2]+=nz;
        out_n[ib*3+0]+=nx; out_n[ib*3+1]+=ny; out_n[ib*3+2]+=nz;
        out_n[ic*3+0]+=nx; out_n[ic*3+1]+=ny; out_n[ic*3+2]+=nz;
    }
    for(size_t v=0; v<vcount; ++v){
        float x=out_n[v*3+0], y=out_n[v*3+1], z=out_n[v*3+2];
        float l=std::sqrt(x*x+y*y+z*z); if(l>1e-6f){ out_n[v*3+0]=x/l; out_n[v*3+1]=y/l; out_n[v*3+2]=z/l; } else { out_n[v*3+0]=0; out_n[v*3+1]=1; out_n[v*3+2]=0; }
    }

    if (vcount >= 8) {
        double cx_acc = 0, cy_acc = 0, cz_acc = 0;
        for (size_t v = 0; v < vcount; ++v) {
            cx_acc += pos[v*3+0];
            cy_acc += pos[v*3+1];
            cz_acc += pos[v*3+2];
        }
        const float cx = (float)(cx_acc / (double)vcount);
        const float cy = (float)(cy_acc / (double)vcount);
        const float cz = (float)(cz_acc / (double)vcount);

        size_t outward = 0, inward = 0;
        for (size_t v = 0; v < vcount; ++v) {
            float dx = pos[v*3+0] - cx;
            float dy = pos[v*3+1] - cy;
            float dz = pos[v*3+2] - cz;
            float r2 = dx*dx + dy*dy + dz*dz;
            if (r2 < 1e-12f) continue;
            float d = dx*out_n[v*3+0] + dy*out_n[v*3+1] + dz*out_n[v*3+2];
            if (d >  1e-6f) ++outward;
            else if (d < -1e-6f) ++inward;
        }
        const size_t voted = outward + inward;
        if (voted >= 8 && inward * 5 >= voted * 4) {
            for (size_t v = 0; v < vcount; ++v) {
                out_n[v*3+0] = -out_n[v*3+0];
                out_n[v*3+1] = -out_n[v*3+1];
                out_n[v*3+2] = -out_n[v*3+2];
            }
        }
    }
}
