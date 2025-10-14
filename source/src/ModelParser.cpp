#include "ModelParser.h"
#include "Files.h"
#include "Utils.h"
#include "BNKCore.cpp"
#include <algorithm>
#include <unordered_map>
#include <filesystem>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <fstream>

using std::uint8_t; using std::uint16_t; using std::uint32_t;

bool build_mdl_buffer_for_name(const std::string &mdl_name, std::vector<unsigned char> &out){
    auto p_headers = find_bnk_by_filename("globals_model_headers.bnk");
    auto p_rest    = find_bnk_by_filename("globals_models.bnk");
    if(!p_headers || !p_rest) return false;
    BNKReader r_headers(*p_headers);
    BNKReader r_rest(*p_rest);
    std::unordered_map<std::string,int> mapH, mapR;
    for(size_t i=0;i<r_headers.list_files().size();++i){
        auto &e=r_headers.list_files()[i];
        std::string f=std::filesystem::path(e.name).filename().string();
        std::transform(f.begin(),f.end(),f.begin(),::tolower);
        mapH.emplace(f,(int)i);
    }
    for(size_t i=0;i<r_rest.list_files().size();++i){
        auto &e=r_rest.list_files()[i];
        std::string f=std::filesystem::path(e.name).filename().string();
        std::transform(f.begin(),f.end(),f.begin(),::tolower);
        mapR.emplace(f,(int)i);
    }
    std::string key=std::filesystem::path(mdl_name).filename().string();
    std::transform(key.begin(),key.end(),key.begin(),::tolower);
    if(!mapH.count(key) || !mapR.count(key)) return false;

    auto tmpdir = std::filesystem::temp_directory_path() / "f2_mdl_hex";
    std::error_code ec; std::filesystem::create_directories(tmpdir, ec);
    auto tmp_h = tmpdir / "h.bin";
    auto tmp_r = tmpdir / "r.bin";
    try{
        extract_one(*p_headers, mapH.at(key), tmp_h.string());
        extract_one(*p_rest,    mapR.at(key), tmp_r.string());
        auto vh = read_all_bytes(tmp_h);
        auto vr = read_all_bytes(tmp_r);
        out.clear(); out.reserve(vh.size()+vr.size());
        out.insert(out.end(), vh.begin(), vh.end());
        out.insert(out.end(), vr.begin(), vr.end());
        std::filesystem::remove(tmp_h, ec);
        std::filesystem::remove(tmp_r, ec);
    }catch(...){ return false; }
    return true;
}

namespace {
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
}
}

bool parse_mdl_info(const std::vector<unsigned char>& data, MDLInfo& out){
    out = MDLInfo{};
    if(data.size() < 8) return false;
    R r{data.data(), data.size(), 0};

    out.Magic.assign((const char*)r.p, 8);

    bool has_magic = (out.Magic == "MeshFile");

    if(has_magic) {
        r.i += 8;
        uint32_t tmp32=0;
        if(!r.u32be(tmp32)) return false;
        if(!r.u32be(out.HeaderSize)) return false;
        if(!r.skip(88)) return false;
        if(!r.skip(8*4)) return false;
    } else {
        r.i = 136;
        out.Magic.clear();
    }

    if(!r.u32be(out.BoneCount)) return false;
    out.Bones.clear(); out.Bones.reserve(out.BoneCount);
    for(uint32_t i=0;i<out.BoneCount;i++){
        std::string nm; if(!r.strz(nm)) return false;
        uint32_t pid=0; if(!r.u32be(pid)) return false;
        MDLBoneInfo b; b.Name=nm; b.ParentID=(pid==0xFFFFFFFFu)?-1:(int)pid;
        out.Bones.push_back(std::move(b));
    }

    if(!r.u32be(out.BoneTransformCount)) return false;
    out.BoneTransforms.clear();
    if(out.BoneTransformCount==out.BoneCount && out.BoneCount>0){
        out.BoneTransforms.reserve(out.BoneTransformCount);
        for(uint32_t i=0;i<out.BoneTransformCount;i++){
            std::vector<float> tf(11);
            for(int k=0;k<11;k++) if(!r.f32be(tf[k])) return false;
            out.BoneTransforms.push_back(std::move(tf));
        }
        out.HasBoneTransforms=true;
    }else{
        uint32_t m=out.BoneTransformCount; if(m>65535u) m=65535u;
        if(!r.skip((size_t)m*44)) return false;
        out.HasBoneTransforms=false;
    }

    for(int k=0;k<10;k++){ float f; if(!r.f32be(f)) return false; }

    if(!r.u32be(out.MeshCount)) return false;
    if(!r.skip(2*4)) return false;
    if(!r.skip(13)) return false;
    if(!r.skip(5*4)) return false;

    if(!r.u32be(out.Unk6Count)) return false;
    if(out.Unk6Count>0 && out.Unk6Count<65535u){
        for(uint32_t i=0;i<out.Unk6Count;i++){ float f; if(!r.f32be(f)) return false; }
    }

    uint32_t StringBlockCount=0;
    if(!r.u32be(StringBlockCount)) return false;
    if(StringBlockCount>0 && StringBlockCount<1000000u){
        for(uint32_t i=0;i<StringBlockCount;i++){
            std::string s; if(!r.strz(s)) return false;
        }
    }

    out.Meshes.clear(); out.Meshes.reserve(out.MeshCount);
    out.MeshBuffers.clear(); out.MeshBuffers.reserve(out.MeshCount);

    for(uint32_t mi=0; mi<out.MeshCount; ++mi){
        uint32_t u1=0; if(!r.u32be(u1)) return false;
        std::string meshName; if(!r.strz(meshName)) return false;
        float f2; if(!r.f32be(f2)) return false; if(!r.f32be(f2)) return false;
        if(!r.skip(21)) return false;
        float f4; if(!r.f32be(f4)) return false;
        uint32_t u5[3]; for(int k=0;k<3;k++) if(!r.u32be(u5[k])) return false;
        uint32_t mcount=0; if(!r.u32be(mcount)) return false;
        MDLMeshInfo mesh; mesh.MeshName=meshName; mesh.MaterialCount=mcount;
        if(mcount>0 && mcount<65535u){
            mesh.Materials.reserve(mcount);
            for(uint32_t j=0;j<mcount;j++){
                MDLMaterialInfo m;
                if(!r.strz(m.TextureName)) return false;
                if(!r.strz(m.SpecularMapName)) return false;
                if(!r.strz(m.NormalMapName)) return false;
                if(!r.strz(m.UnkName)) return false;
                if(!r.strz(m.TintName)) return false;
                if(!r.u32be(m.Unk1)) return false;
                if(!r.u32be(m.Unk2[0])) return false;
                if(!r.u32be(m.Unk2[1])) return false;
                size_t keep=r.i; uint8_t peek=0;
                if(r.u8(peek)){ if(peek!=0x01){ r.i=keep; } } else { r.i=keep; }
                mesh.Materials.push_back(std::move(m));
            }
        }
        out.Meshes.push_back(std::move(mesh));
    }

    if(StringBlockCount>0){
        auto parse_normal = [&](uint32_t mi)->bool{
            bool found = false;
            size_t searchStart = r.i;
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
            if(!found) return false;
            uint32_t bufferID=0, bufferID_copy=0, someCount1=0;
            if(!r.u32be(bufferID)) return false;
            if(!r.u32be(bufferID_copy)) return false;
            if(!r.u32be(someCount1)) return false;
            uint32_t tlen=0; if(!r.u32be(tlen)) return false;
            uint32_t vtx=0; if(!r.u32be(vtx)) return false;
            uint32_t sub=0; if(!r.u32be(sub)) return false;
            if(sub>0 && sub<65535u){
                for(uint32_t s=0;s<sub;s++){
                    uint32_t S1; uint8_t S2; uint32_t S3a,S3b,S3c; float F4[6];
                    if(!r.u32be(S1)) return false;
                    if(!r.u8(S2)) return false;
                    if(!r.u32be(S3a)) return false;
                    if(!r.u32be(S3b)) return false;
                    if(!r.u32be(S3c)) return false;
                    for(int k=0;k<6;k++) if(!r.f32be(F4[k])) return false;
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
            MDLMeshBufferInfo mb;
            mb.VertexCount=vtx;
            mb.VertexOffset=vert_off;
            mb.FaceCount=tlen;
            mb.FaceOffset=face_off;
            mb.SubMeshCount=sub;
            mb.IsAltPath=false;
            out.MeshBuffers.push_back(mb);
            return true;
        };

        if(!parse_normal(0)) return false;
        size_t first_end = r.i;
        bool did_skip9 = false;
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
                if(!did_skip9){
                    r.i = first_end + 9;
                    did_skip9 = true;
                }
            }
            uint32_t bufferID=0, bufferID_copy=0, someCount1=0;
            if(!r.u32be(bufferID)) return false;
            if(!r.u32be(bufferID_copy)) return false;
            if(!r.u32be(someCount1)) return false;
            uint32_t tlen=0; if(!r.u32be(tlen)) return false;
            uint32_t vtx=0; if(!r.u32be(vtx)) return false;
            uint32_t sub=0; if(!r.u32be(sub)) return false;
            if(sub>0 && sub<65535u){
                for(uint32_t s=0;s<sub;s++){
                    uint32_t S1; uint8_t S2; uint32_t S3a,S3b,S3c; float F4[6];
                    if(!r.u32be(S1)) return false;
                    if(!r.u8(S2)) return false;
                    if(!r.u32be(S3a)) return false;
                    if(!r.u32be(S3b)) return false;
                    if(!r.u32be(S3c)) return false;
                    for(int k=0;k<6;k++) if(!r.f32be(F4[k])) return false;
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
            MDLMeshBufferInfo mb;
            mb.VertexCount=vtx;
            mb.VertexOffset=vert_off;
            mb.FaceCount=tlen;
            mb.FaceOffset=face_off;
            mb.SubMeshCount=sub;
            mb.IsAltPath=false;
            out.MeshBuffers.push_back(mb);
            scan_pos = r.i;
        }
        return true;
    }

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
                        if(!r.skip(8)) return false;
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
                                if(!r.skip(8)) return false;
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

            if(!r.skip(41)) return false;

            while(r.i + 4 <= r.n){
                uint32_t check = (uint32_t(r.p[r.i])<<24) | (uint32_t(r.p[r.i+1])<<16) |
                                (uint32_t(r.p[r.i+2])<<8) | r.p[r.i+3];
                if(check == 0xFFFFFFFF){
                    if(!r.skip(41)) return false;
                } else {
                    break;
                }
            }

            size_t vert_off=0, face_off=0;
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

            MDLMeshBufferInfo mb;
            mb.VertexCount=vtx;
            mb.VertexOffset=vert_off;
            mb.FaceCount=tlen;
            mb.FaceOffset=face_off;
            mb.SubMeshCount=1;
            mb.IsAltPath=true;
            out.MeshBuffers.push_back(mb);

        } else {
            bool found = false;
            size_t searchStart = r.i;
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

            if(!found) return false;

            uint32_t bufferID=0, bufferID_copy=0, someCount1=0;
            if(!r.u32be(bufferID)) return false;
            if(!r.u32be(bufferID_copy)) return false;
            if(!r.u32be(someCount1)) return false;
            uint32_t tlen=0; if(!r.u32be(tlen)) return false;
            uint32_t vtx=0; if(!r.u32be(vtx)) return false;
            uint32_t sub=0; if(!r.u32be(sub)) return false;

            if(sub>0 && sub<65535u){
                for(uint32_t s=0;s<sub;s++){
                    uint32_t S1; uint8_t S2; uint32_t S3a,S3b,S3c; float F4[6];
                    if(!r.u32be(S1)) return false;
                    if(!r.u8(S2)) return false;
                    if(!r.u32be(S3a)) return false;
                    if(!r.u32be(S3b)) return false;
                    if(!r.u32be(S3c)) return false;
                    for(int k=0;k<6;k++) if(!r.f32be(F4[k])) return false;
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

            MDLMeshBufferInfo mb;
            mb.VertexCount=vtx;
            mb.VertexOffset=vert_off;
            mb.FaceCount=tlen;
            mb.FaceOffset=face_off;
            mb.SubMeshCount=sub;
            mb.IsAltPath=false;
            out.MeshBuffers.push_back(mb);
        }
    }
    return true;
}

bool parse_mdl_geometry(const std::vector<unsigned char>& data, const MDLInfo& info, std::vector<MDLMeshGeom>& out){
    out.clear();
    if(info.MeshBuffers.size()!=info.Meshes.size()) return true;
    R r{data.data(), data.size(), 0};
    for(size_t mi=0; mi<info.MeshBuffers.size(); ++mi){
        const auto& mb=info.MeshBuffers[mi];
        MDLMeshGeom g;
        if(mi<info.Meshes.size() && !info.Meshes[mi].Materials.empty())
            g.diffuse_tex_name=info.Meshes[mi].Materials[0].TextureName;

        size_t vertex_stride = mb.IsAltPath ? 20 : 28;

        if(mb.VertexCount==0 || mb.FaceCount==0 || mb.VertexOffset+(size_t)mb.VertexCount*vertex_stride>r.n || mb.FaceOffset+(size_t)mb.FaceCount*2>r.n){
            out.push_back(std::move(g)); continue;
        }

        g.positions.resize((size_t)mb.VertexCount*3);
        g.normals.resize((size_t)mb.VertexCount*3);
        g.uvs.resize((size_t)mb.VertexCount*2);
        g.bone_ids.resize((size_t)mb.VertexCount*4);
        g.bone_weights.resize((size_t)mb.VertexCount*4);

        const uint8_t* vp=r.p+mb.VertexOffset;
        for(uint32_t v=0; v<mb.VertexCount; ++v){
            const uint8_t* p=vp+v*vertex_stride;
            uint16_t hx=(uint16_t(p[0])<<8)|p[1];
            uint16_t hy=(uint16_t(p[2])<<8)|p[3];
            uint16_t hz=(uint16_t(p[4])<<8)|p[5];
            g.positions[v*3+0]=half_to_float(hx);
            g.positions[v*3+1]=half_to_float(hy);
            g.positions[v*3+2]=half_to_float(hz);

            g.normals[v*3+0]=0.0f;
            g.normals[v*3+1]=1.0f;
            g.normals[v*3+2]=0.0f;

            if(!mb.IsAltPath && vertex_stride >= 20){
                uint8_t bone_idx = p[15];
                uint8_t weight_val = p[19];

                if(bone_idx < 255){
                    g.bone_ids[v*4+0] = bone_idx;
                    g.bone_ids[v*4+1] = 0;
                    g.bone_ids[v*4+2] = 0;
                    g.bone_ids[v*4+3] = 0;

                    float w = (weight_val > 0) ? (weight_val / 255.0f) : 1.0f;
                    g.bone_weights[v*4+0] = w;
                    g.bone_weights[v*4+1] = 0.0f;
                    g.bone_weights[v*4+2] = 0.0f;
                    g.bone_weights[v*4+3] = 0.0f;
                } else {
                    g.bone_ids[v*4+0] = 0;
                    g.bone_ids[v*4+1] = 0;
                    g.bone_ids[v*4+2] = 0;
                    g.bone_ids[v*4+3] = 0;

                    g.bone_weights[v*4+0] = 1.0f;
                    g.bone_weights[v*4+1] = 0.0f;
                    g.bone_weights[v*4+2] = 0.0f;
                    g.bone_weights[v*4+3] = 0.0f;
                }
            } else {
                g.bone_ids[v*4+0] = 0;
                g.bone_ids[v*4+1] = 0;
                g.bone_ids[v*4+2] = 0;
                g.bone_ids[v*4+3] = 0;

                g.bone_weights[v*4+0] = 1.0f;
                g.bone_weights[v*4+1] = 0.0f;
                g.bone_weights[v*4+2] = 0.0f;
                g.bone_weights[v*4+3] = 0.0f;
            }

            size_t uv_offset = mb.IsAltPath ? 12 : 20;
            uint16_t uu=(uint16_t(p[uv_offset+0])<<8)|p[uv_offset+1];
            uint16_t vv=(uint16_t(p[uv_offset+2])<<8)|p[uv_offset+3];
            g.uvs[v*2+0]=half_to_float(uu);
            g.uvs[v*2+1]=half_to_float(vv);
        }

        std::vector<uint16_t> strip(mb.FaceCount);
        const uint8_t* fp=r.p+mb.FaceOffset; bool hasFFFF=false;
        for(uint32_t i=0;i<mb.FaceCount;i++){
            uint16_t w=(uint16_t(fp[i*2+0])<<8)|fp[i*2+1];
            strip[i]=w; if(w==0xFFFF) hasFFFF=true;
        }
        if(hasFFFF){ build_triangles_from_strip(strip, g.indices); }
        else{
            size_t triCount=strip.size()/3; g.indices.resize(triCount*3);
            for(size_t t=0;t<triCount;t++){ g.indices[t*3+0]=strip[t*3+0]; g.indices[t*3+1]=strip[t*3+1]; g.indices[t*3+2]=strip[t*3+2]; }
        }

        compute_smooth_normals(mb.VertexCount, g.indices, g.positions, g.normals);

        out.push_back(std::move(g));
    }
    return true;
}