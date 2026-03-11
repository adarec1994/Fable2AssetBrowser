#include "ModelParser.h"
#include "../Utilities/Files.h"
#include "../Utilities/Utils.h"
#include "../BNKCore.cpp"
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
        std::string f=e.name;
        std::transform(f.begin(),f.end(),f.begin(),::tolower);
        std::replace(f.begin(), f.end(), '\\', '/');
        mapH.emplace(f,(int)i);
    }
    for(size_t i=0;i<r_rest.list_files().size();++i){
        auto &e=r_rest.list_files()[i];
        std::string f=e.name;
        std::transform(f.begin(),f.end(),f.begin(),::tolower);
        std::replace(f.begin(), f.end(), '\\', '/');
        mapR.emplace(f,(int)i);
    }
    std::string key=mdl_name;
    std::transform(key.begin(),key.end(),key.begin(),::tolower);
    std::replace(key.begin(), key.end(), '\\', '/');
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
    return parse_mdl_info(data, out, "");
}

bool parse_mdl_info(const std::vector<unsigned char>& data, MDLInfo& out, const std::string& file_path){
    out = MDLInfo{};
    if(data.size() < 8) return false;
    R r{data.data(), data.size(), 0};

    std::string path_lower = file_path;
    std::transform(path_lower.begin(), path_lower.end(), path_lower.begin(), ::tolower);
    bool is_foliage = (path_lower.find("/foliage/") != std::string::npos) ||
                      (path_lower.find("\\foliage\\") != std::string::npos) ||
                      (path_lower.find("/foliage\\") != std::string::npos) ||
                      (path_lower.find("\\foliage/") != std::string::npos);

    std::string magic((const char*)r.p, 8);
    bool has_magic = (magic == "MeshFile");

    if(has_magic){
        out.Magic = magic;
        r.i += 8;
        uint32_t tmp32=0;
        if(!r.u32be(tmp32)) return false;
        if(!r.u32be(out.HeaderSize)) return false;
        if(!r.skip(88)) return false;
    } else {
        r.i = 0;
        out.Magic.clear();
    }

    if(!r.skip(8*4)) return false;

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

if(is_foliage){
    uint16_t unk2bytes = 0;
    if(!r.u16be(unk2bytes)) return false;

    // Detect tree variant: extra u8=0x01 before mesh headers, stride-36 half-float
    bool is_tree_foliage = false;
    {
        size_t save = r.i;
        uint8_t peek = 0;
        if(r.u8(peek) && peek == 0x01){
            uint32_t check_f0 = 0;
            if(r.u32be(check_f0) && check_f0 == 0) is_tree_foliage = true;
        }
        r.i = save;
    }

    if(is_tree_foliage){
        uint8_t tree_flag = 0;
        if(!r.u8(tree_flag)) return false;

        for(uint32_t mi=0; mi<out.MeshCount; ++mi){
            // fields[0]=mi, fields[1]=face_ic-2, fields[2]=face_ic, fields[3]=vtx
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

            // UV/tangent extra block after face buffer
            if(vtx > 0 && vtx < 100000u)
                if(!r.skip((size_t)vtx * 16)) return false;

            // 2-byte separator between meshes (not after last)
            if(mi + 1 < out.MeshCount)
                if(!r.skip(2)) return false;

            out.MeshBuffers.push_back(mb);
        }
    } else {
        // Plant variant: stride-48 float32, no extra UV block
        for(uint32_t mi=0; mi<out.MeshCount; ++mi){
            // fields[0]=bufferID(0), fields[1]=face_count-2, fields[2]=face_count, fields[3]=vtx_count
            uint32_t fields[4] = {0,0,0,0};
            for(int k=0;k<4;k++) if(!r.u32be(fields[k])) return false;

            uint32_t vtx     = fields[3];
            uint32_t face_ic = fields[2];

            size_t vert_off = 0, face_off = 0;
            if(vtx > 0 && vtx < 100000u){
                vert_off = r.i;
                if(!r.skip((size_t)vtx * 48)) return false;
            }
            if(face_ic > 0 && face_ic < 100000u){
                face_off = r.i;
                if(!r.skip((size_t)face_ic * 2)) return false;
            }

            MDLMeshBufferInfo mb;
            mb.VertexCount       = vtx;
            mb.VertexOffset      = vert_off;
            mb.FaceCount         = face_ic;
            mb.FaceOffset        = face_off;
            mb.SubMeshCount      = 1;
            mb.IsAltPath         = false;
            mb.IsFoliagePath     = true;
            mb.FoliageVertexStride = 48;
            mb.MeshIndex         = mi;
            out.MeshBuffers.push_back(mb);
        }
    }
    return true;
}

    if(StringBlockCount>0){
        uint32_t bufferID=0, bufferID_copy=0, someCount1=0;
        if(!r.u32be(bufferID)) return false;
        if(!r.u32be(bufferID_copy)) return false;
        if(!r.u32be(someCount1)) return false;
        uint32_t tlen=0; if(!r.u32be(tlen)) return false;
        uint32_t vtx=0; if(!r.u32be(vtx)) return false;
        uint32_t sub=0; if(!r.u32be(sub)) return false;

        std::vector<MDLSubMeshInfo> submeshes;
        if(sub>0 && sub<65535u){
            for(uint32_t s=0;s<sub;s++){
                uint32_t marker; if(!r.u32be(marker)) return false;
                uint32_t subIdx; if(!r.u32be(subIdx)) return false;
                uint8_t matIdx; if(!r.u8(matIdx)) return false;
                uint32_t faceCount; if(!r.u32be(faceCount)) return false;
                uint32_t startIdx; if(!r.u32be(startIdx)) return false;
                float F4[6];
                for(int k=0;k<6;k++) if(!r.f32be(F4[k])) return false;

                MDLSubMeshInfo smi;
                smi.MaterialIndex = matIdx;
                smi.FaceCount = faceCount;
                smi.StartIndex = startIdx;
                submeshes.push_back(smi);
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
        MDLMeshBufferInfo mb0;
        mb0.VertexCount=vtx;
        mb0.VertexOffset=vert_off;
        mb0.FaceCount=tlen;
        mb0.FaceOffset=face_off;
        mb0.SubMeshCount=sub;
        mb0.IsAltPath=false;
        mb0.SubMeshes=submeshes;
        mb0.MeshIndex=0;
        out.MeshBuffers.push_back(mb0);

        size_t first_end = r.i;
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
                r.i = first_end + 9;
            }
            uint32_t bufferIDn=0, bufferID_copyn=0, someCount1n=0;
            if(!r.u32be(bufferIDn)) return false;
            if(!r.u32be(bufferID_copyn)) return false;
            if(!r.u32be(someCount1n)) return false;
            uint32_t tlenn=0; if(!r.u32be(tlenn)) return false;
            uint32_t vtxn=0; if(!r.u32be(vtxn)) return false;
            uint32_t subn=0; if(!r.u32be(subn)) return false;

            std::vector<MDLSubMeshInfo> submeshesn;
            if(subn>0 && subn<65535u){
                for(uint32_t s=0;s<subn;s++){
                    uint32_t marker; if(!r.u32be(marker)) return false;
                    uint32_t subIdx; if(!r.u32be(subIdx)) return false;
                    uint8_t matIdx; if(!r.u8(matIdx)) return false;
                    uint32_t faceCount; if(!r.u32be(faceCount)) return false;
                    uint32_t startIdx; if(!r.u32be(startIdx)) return false;
                    float F4[6];
                    for(int k=0;k<6;k++) if(!r.f32be(F4[k])) return false;

                    MDLSubMeshInfo smi;
                    smi.MaterialIndex = matIdx;
                    smi.FaceCount = faceCount;
                    smi.StartIndex = startIdx;
                    submeshesn.push_back(smi);
                }
            }
            size_t vert_offn=0, face_offn=0;
            if(vtxn>0 && vtxn<65535u){
                vert_offn=r.i;
                size_t vszn=(size_t)vtxn*28;
                if(!r.skip(vszn)) return false;
            }
            if(tlenn>0 && tlenn<65535u){
                face_offn=r.i;
                size_t fszn=(size_t)tlenn*2;
                if(!r.skip(fszn)) return false;
            }
            if(vtxn>0 && vtxn<65535u){
                size_t uszn=(size_t)vtxn*16;
                if(!r.skip(uszn)) return false;
            }
            MDLMeshBufferInfo mb;
            mb.VertexCount=vtxn;
            mb.VertexOffset=vert_offn;
            mb.FaceCount=tlenn;
            mb.FaceOffset=face_offn;
            mb.SubMeshCount=subn;
            mb.IsAltPath=false;
            mb.SubMeshes=submeshesn;
            mb.MeshIndex=mi;
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
                        uint32_t mesh_id = 0;
                        if(!r.u32be(mesh_id)) return false;
                        uint32_t mesh_id_copy = 0;
                        if(!r.u32be(mesh_id_copy)) return false;
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
                                uint32_t mesh_id = 0;
                                if(!r.u32be(mesh_id)) return false;
                                uint32_t mesh_id_copy = 0;
                                if(!r.u32be(mesh_id_copy)) return false;
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

            if(!r.skip(40)) return false;

            size_t submesh_offset = r.i;
            uint32_t submesh_count = 0;
            if(!r.u32be(submesh_count)) return false;

            uint32_t next_value = 0;
            if(!r.u32be(next_value)) return false;

            uint32_t final_submesh_count;
            if(next_value != 0xFFFFFFFF || submesh_count >= 256){
                final_submesh_count = 1;
            } else {
                r.i -= 4;
                final_submesh_count = submesh_count;
            }

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

            std::vector<MDLSubMeshInfo> submeshes;
            for(uint32_t s=0; s<final_submesh_count; s++){
                uint32_t marker; if(!r.u32be(marker)) return false;
                uint32_t subIdx; if(!r.u32be(subIdx)) return false;
                uint8_t matIdx; if(!r.u8(matIdx)) return false;
                uint32_t faceCount; if(!r.u32be(faceCount)) return false;
                uint32_t startIdx; if(!r.u32be(startIdx)) return false;
                float F4[6];
                for(int k=0;k<6;k++) if(!r.f32be(F4[k])) return false;

                MDLSubMeshInfo smi;
                smi.MaterialIndex = matIdx;
                smi.FaceCount = faceCount;
                smi.StartIndex = startIdx;
                submeshes.push_back(smi);
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
            mb.SubMeshCount=final_submesh_count;
            mb.IsAltPath=true;
            mb.SubMeshes=submeshes;
            mb.MeshIndex=mi;
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

            uint32_t bufferID2=0, bufferID_copy2=0, someCount12=0;
            if(!r.u32be(bufferID2)) return false;
            if(!r.u32be(bufferID_copy2)) return false;
            if(!r.u32be(someCount12)) return false;
            uint32_t tlen2=0; if(!r.u32be(tlen2)) return false;
            uint32_t vtx2=0; if(!r.u32be(vtx2)) return false;
            uint32_t sub2=0; if(!r.u32be(sub2)) return false;

            std::vector<MDLSubMeshInfo> submeshes;
            if(sub2>0 && sub2<65535u){
                for(uint32_t s=0;s<sub2;s++){
                    uint32_t marker; if(!r.u32be(marker)) return false;
                    uint32_t subIdx; if(!r.u32be(subIdx)) return false;
                    uint8_t matIdx; if(!r.u8(matIdx)) return false;
                    uint32_t faceCount; if(!r.u32be(faceCount)) return false;
                    uint32_t startIdx; if(!r.u32be(startIdx)) return false;
                    float F4[6];
                    for(int k=0;k<6;k++) if(!r.f32be(F4[k])) return false;

                    MDLSubMeshInfo smi;
                    smi.MaterialIndex = matIdx;
                    smi.FaceCount = faceCount;
                    smi.StartIndex = startIdx;
                    submeshes.push_back(smi);
                }
            }

            size_t vert_off2=0, face_off2=0;
            if(vtx2>0 && vtx2<65535u){
                vert_off2=r.i;
                size_t vsz2=(size_t)vtx2*28;
                if(!r.skip(vsz2)) return false;
            }
            if(tlen2>0 && tlen2<65535u){
                face_off2=r.i;
                size_t fsz2=(size_t)tlen2*2;
                if(!r.skip(fsz2)) return false;
            }
            if(vtx2>0 && vtx2<65535u){
                size_t usz2=(size_t)vtx2*16;
                if(!r.skip(usz2)) return false;
            }

            MDLMeshBufferInfo mb;
            mb.VertexCount=vtx2;
            mb.VertexOffset=vert_off2;
            mb.FaceCount=tlen2;
            mb.FaceOffset=face_off2;
            mb.SubMeshCount=sub2;
            mb.IsAltPath=false;
            mb.SubMeshes=submeshes;
            mb.MeshIndex=mi;
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

        if(mb.IsFoliagePath){
            // --- Plant foliage: stride 48, big-endian float32 ---
            if(mb.FoliageVertexStride == 48 || mb.FoliageVertexStride == 0){
                if(mb.VertexCount==0 || mb.FaceCount==0 ||
                   mb.VertexOffset+(size_t)mb.VertexCount*48>r.n ||
                   mb.FaceOffset+(size_t)mb.FaceCount*2>r.n){
                    MDLMeshGeom g;
                    if(mi<info.Meshes.size() && !info.Meshes[mi].Materials.empty())
                        g.diffuse_tex_name=info.Meshes[mi].Materials[0].TextureName;
                    out.push_back(std::move(g));
                    continue;
                }
                MDLMeshGeom g;
                g.positions.resize((size_t)mb.VertexCount*3);
                g.normals.resize((size_t)mb.VertexCount*3);
                g.uvs.resize((size_t)mb.VertexCount*2);
                const uint8_t* vp=r.p+mb.VertexOffset;
                auto bswap=[](float f){ uint32_t u; std::memcpy(&u,&f,4);
                    u=(u>>24)|((u>>8)&0xFF00)|((u<<8)&0xFF0000)|(u<<24);
                    std::memcpy(&f,&u,4); return f; };
                for(uint32_t v=0;v<mb.VertexCount;++v){
                    const uint8_t* p=vp+v*48;
                    float px,py,pz,nx,ny,nz,uu,vv;
                    std::memcpy(&px,p+0, 4); std::memcpy(&py,p+4, 4); std::memcpy(&pz,p+8, 4);
                    std::memcpy(&nx,p+12,4); std::memcpy(&ny,p+16,4); std::memcpy(&nz,p+20,4);
                    std::memcpy(&uu,p+24,4); std::memcpy(&vv,p+28,4);
                    g.positions[v*3+0]=bswap(px); g.positions[v*3+1]=bswap(py); g.positions[v*3+2]=bswap(pz);
                    g.normals[v*3+0]=bswap(nx);   g.normals[v*3+1]=bswap(ny);   g.normals[v*3+2]=bswap(nz);
                    g.uvs[v*2+0]=bswap(uu);       g.uvs[v*2+1]=bswap(vv);
                }
                std::vector<uint16_t> strip(mb.FaceCount);
                const uint8_t* fp=r.p+mb.FaceOffset;
                bool hasFFFF=false;
                for(uint32_t i=0;i<mb.FaceCount;i++){
                    uint16_t w=(uint16_t(fp[i*2+0])<<8)|fp[i*2+1];
                    strip[i]=w; if(w==0xFFFF) hasFFFF=true;
                }
                if(hasFFFF){ build_triangles_from_strip(strip,g.indices); }
                else{
                    size_t tc=strip.size()/3; g.indices.resize(tc*3);
                    for(size_t t=0;t<tc;t++){
                        g.indices[t*3+0]=strip[t*3+0]; g.indices[t*3+1]=strip[t*3+1]; g.indices[t*3+2]=strip[t*3+2];
                    }
                }
                if(mi<info.Meshes.size() && !info.Meshes[mi].Materials.empty())
                    g.diffuse_tex_name=info.Meshes[mi].Materials[0].TextureName;
                g.MeshIndex=(uint32_t)mi; g.SubMeshIndex=0;
                if(mi<info.Meshes.size() && !info.Meshes[mi].MeshName.empty())
                    g.name=info.Meshes[mi].MeshName;
                else
                    g.name="mesh_"+std::to_string(mi);
                out.push_back(std::move(g));
                continue;
            }

            // --- Tree foliage: stride 36, big-endian half-float ---
            // pos at offset 0 (u16×3), uv at offset 12 (u16×2), normals computed
            if(mb.FoliageVertexStride == 36){
                if(mb.VertexCount==0 || mb.FaceCount==0 ||
                   mb.VertexOffset+(size_t)mb.VertexCount*36>r.n ||
                   mb.FaceOffset+(size_t)mb.FaceCount*2>r.n){
                    MDLMeshGeom g;
                    if(mi<info.Meshes.size() && !info.Meshes[mi].Materials.empty())
                        g.diffuse_tex_name=info.Meshes[mi].Materials[0].TextureName;
                    out.push_back(std::move(g));
                    continue;
                }
                MDLMeshGeom g;
                g.positions.resize((size_t)mb.VertexCount*3);
                g.uvs.resize((size_t)mb.VertexCount*2);
                const uint8_t* vp=r.p+mb.VertexOffset;
                for(uint32_t v=0;v<mb.VertexCount;++v){
                    const uint8_t* p=vp+v*36;
                    g.positions[v*3+0]=half_to_float((uint16_t(p[0])<<8)|p[1]);
                    g.positions[v*3+1]=half_to_float((uint16_t(p[2])<<8)|p[3]);
                    g.positions[v*3+2]=half_to_float((uint16_t(p[4])<<8)|p[5]);
                    g.uvs[v*2+0]=half_to_float((uint16_t(p[12])<<8)|p[13]);
                    g.uvs[v*2+1]=half_to_float((uint16_t(p[14])<<8)|p[15]);
                }
                std::vector<uint16_t> strip(mb.FaceCount);
                const uint8_t* fp=r.p+mb.FaceOffset;
                bool hasFFFF=false;
                for(uint32_t i=0;i<mb.FaceCount;i++){
                    uint16_t w=(uint16_t(fp[i*2+0])<<8)|fp[i*2+1];
                    strip[i]=w; if(w==0xFFFF) hasFFFF=true;
                }
                if(hasFFFF){ build_triangles_from_strip(strip,g.indices); }
                else{
                    size_t tc=strip.size()/3; g.indices.resize(tc*3);
                    for(size_t t=0;t<tc;t++){
                        g.indices[t*3+0]=strip[t*3+0]; g.indices[t*3+1]=strip[t*3+1]; g.indices[t*3+2]=strip[t*3+2];
                    }
                }
                g.normals.resize((size_t)mb.VertexCount*3);
                compute_smooth_normals(mb.VertexCount, g.indices, g.positions, g.normals);
                if(mi<info.Meshes.size() && !info.Meshes[mi].Materials.empty())
                    g.diffuse_tex_name=info.Meshes[mi].Materials[0].TextureName;
                g.MeshIndex=(uint32_t)mi; g.SubMeshIndex=0;
                if(mi<info.Meshes.size() && !info.Meshes[mi].MeshName.empty())
                    g.name=info.Meshes[mi].MeshName;
                else
                    g.name="mesh_"+std::to_string(mi);
                out.push_back(std::move(g));
                continue;
            }

            // Unknown foliage stride — push empty
            {
                MDLMeshGeom g;
                if(mi<info.Meshes.size() && !info.Meshes[mi].Materials.empty())
                    g.diffuse_tex_name=info.Meshes[mi].Materials[0].TextureName;
                out.push_back(std::move(g));
                continue;
            }
        }

        size_t vertex_stride = mb.IsAltPath ? 20 : 28;

        if(mb.VertexCount==0 || mb.FaceCount==0 || mb.VertexOffset+(size_t)mb.VertexCount*vertex_stride>r.n || mb.FaceOffset+(size_t)mb.FaceCount*2>r.n){
            MDLMeshGeom g;
            if(mi<info.Meshes.size() && !info.Meshes[mi].Materials.empty())
                g.diffuse_tex_name=info.Meshes[mi].Materials[0].TextureName;
            out.push_back(std::move(g));
            continue;
        }

        std::vector<float> all_positions((size_t)mb.VertexCount*3);
        std::vector<float> all_uvs((size_t)mb.VertexCount*2);
        std::vector<uint16_t> all_bone_ids((size_t)mb.VertexCount*4);
        std::vector<float> all_bone_weights((size_t)mb.VertexCount*4);

        const uint8_t* vp=r.p+mb.VertexOffset;
        for(uint32_t v=0; v<mb.VertexCount; ++v){
            const uint8_t* p=vp+v*vertex_stride;
            uint16_t hx=(uint16_t(p[0])<<8)|p[1];
            uint16_t hy=(uint16_t(p[2])<<8)|p[3];
            uint16_t hz=(uint16_t(p[4])<<8)|p[5];
            all_positions[v*3+0]=half_to_float(hx);
            all_positions[v*3+1]=half_to_float(hy);
            all_positions[v*3+2]=half_to_float(hz);

            if(!mb.IsAltPath && vertex_stride >= 20){
                uint8_t bone_idx = p[15];
                uint8_t weight_val = p[19];
                if(bone_idx < 255){
                    all_bone_ids[v*4+0] = bone_idx;
                    all_bone_ids[v*4+1] = 0;
                    all_bone_ids[v*4+2] = 0;
                    all_bone_ids[v*4+3] = 0;
                    float w = (weight_val > 0) ? (weight_val / 255.0f) : 1.0f;
                    all_bone_weights[v*4+0] = w;
                    all_bone_weights[v*4+1] = 0.0f;
                    all_bone_weights[v*4+2] = 0.0f;
                    all_bone_weights[v*4+3] = 0.0f;
                } else {
                    all_bone_ids[v*4+0] = 0;
                    all_bone_ids[v*4+1] = 0;
                    all_bone_ids[v*4+2] = 0;
                    all_bone_ids[v*4+3] = 0;
                    all_bone_weights[v*4+0] = 1.0f;
                    all_bone_weights[v*4+1] = 0.0f;
                    all_bone_weights[v*4+2] = 0.0f;
                    all_bone_weights[v*4+3] = 0.0f;
                }
            } else {
                all_bone_ids[v*4+0] = 0;
                all_bone_ids[v*4+1] = 0;
                all_bone_ids[v*4+2] = 0;
                all_bone_ids[v*4+3] = 0;
                all_bone_weights[v*4+0] = 1.0f;
                all_bone_weights[v*4+1] = 0.0f;
                all_bone_weights[v*4+2] = 0.0f;
                all_bone_weights[v*4+3] = 0.0f;
            }

            size_t uv_offset = mb.IsAltPath ? 12 : 20;
            uint16_t uu=(uint16_t(p[uv_offset+0])<<8)|p[uv_offset+1];
            uint16_t vv=(uint16_t(p[uv_offset+2])<<8)|p[uv_offset+3];
            all_uvs[v*2+0]=half_to_float(uu);
            all_uvs[v*2+1]=half_to_float(vv);
        }

        std::vector<uint16_t> strip(mb.FaceCount);
        const uint8_t* fp=r.p+mb.FaceOffset;
        bool hasFFFF=false;
        for(uint32_t i=0;i<mb.FaceCount;i++){
            uint16_t w=(uint16_t(fp[i*2+0])<<8)|fp[i*2+1];
            strip[i]=w;
            if(w==0xFFFF) hasFFFF=true;
        }

        if(mb.SubMeshes.empty() || mb.SubMeshCount <= 1){
            MDLMeshGeom g;
            g.positions = all_positions;
            g.uvs = all_uvs;
            g.bone_ids = all_bone_ids;
            g.bone_weights = all_bone_weights;

            if(hasFFFF){ build_triangles_from_strip(strip, g.indices); }
            else{
                size_t triCount=strip.size()/3;
                g.indices.resize(triCount*3);
                for(size_t t=0;t<triCount;t++){
                    g.indices[t*3+0]=strip[t*3+0];
                    g.indices[t*3+1]=strip[t*3+1];
                    g.indices[t*3+2]=strip[t*3+2];
                }
            }

            g.normals.resize(mb.VertexCount * 3);
            compute_smooth_normals(mb.VertexCount, g.indices, g.positions, g.normals);

            if(mi<info.Meshes.size() && !info.Meshes[mi].Materials.empty())
                g.diffuse_tex_name=info.Meshes[mi].Materials[0].TextureName;

            g.MeshIndex = (uint32_t)mi;
            g.SubMeshIndex = 0;
            if(mi<info.Meshes.size() && !info.Meshes[mi].MeshName.empty())
                g.name = info.Meshes[mi].MeshName;
            else
                g.name = "mesh_" + std::to_string(mi);

            out.push_back(std::move(g));
        } else {
            for(size_t si=0; si<mb.SubMeshes.size(); ++si){
                const auto& sub = mb.SubMeshes[si];

                MDLMeshGeom g;

                if(mi<info.Meshes.size() && si<info.Meshes[mi].Materials.size())
                    g.diffuse_tex_name=info.Meshes[mi].Materials[si].TextureName;
                else if(mi<info.Meshes.size() && !info.Meshes[mi].Materials.empty())
                    g.diffuse_tex_name=info.Meshes[mi].Materials[0].TextureName;

                uint32_t start_idx = sub.StartIndex;
                uint32_t end_idx;
                if(si + 1 < mb.SubMeshes.size())
                    end_idx = mb.SubMeshes[si + 1].StartIndex;
                else
                    end_idx = (uint32_t)strip.size();

                if(start_idx >= strip.size() || start_idx >= end_idx){
                    out.push_back(std::move(g));
                    continue;
                }

                if(end_idx > strip.size()) end_idx = (uint32_t)strip.size();

                std::vector<uint16_t> sub_strip(strip.begin() + start_idx, strip.begin() + end_idx);

                std::vector<uint32_t> sub_indices;
                build_triangles_from_strip(sub_strip, sub_indices);

                if(sub_indices.empty()){
                    out.push_back(std::move(g));
                    continue;
                }

                std::vector<bool> used_verts(mb.VertexCount, false);
                for(uint32_t idx : sub_indices){
                    if(idx < mb.VertexCount) used_verts[idx] = true;
                }

                std::vector<uint32_t> old_to_new(mb.VertexCount, 0xFFFFFFFF);
                uint32_t new_vert_count = 0;
                for(uint32_t v=0; v<mb.VertexCount; ++v){
                    if(used_verts[v]){
                        old_to_new[v] = new_vert_count++;
                    }
                }

                g.positions.resize(new_vert_count * 3);
                g.uvs.resize(new_vert_count * 2);
                g.bone_ids.resize(new_vert_count * 4);
                g.bone_weights.resize(new_vert_count * 4);

                for(uint32_t v=0; v<mb.VertexCount; ++v){
                    if(used_verts[v]){
                        uint32_t nv = old_to_new[v];
                        g.positions[nv*3+0] = all_positions[v*3+0];
                        g.positions[nv*3+1] = all_positions[v*3+1];
                        g.positions[nv*3+2] = all_positions[v*3+2];
                        g.uvs[nv*2+0] = all_uvs[v*2+0];
                        g.uvs[nv*2+1] = all_uvs[v*2+1];
                        g.bone_ids[nv*4+0] = all_bone_ids[v*4+0];
                        g.bone_ids[nv*4+1] = all_bone_ids[v*4+1];
                        g.bone_ids[nv*4+2] = all_bone_ids[v*4+2];
                        g.bone_ids[nv*4+3] = all_bone_ids[v*4+3];
                        g.bone_weights[nv*4+0] = all_bone_weights[v*4+0];
                        g.bone_weights[nv*4+1] = all_bone_weights[v*4+1];
                        g.bone_weights[nv*4+2] = all_bone_weights[v*4+2];
                        g.bone_weights[nv*4+3] = all_bone_weights[v*4+3];
                    }
                }

                g.indices.resize(sub_indices.size());
                for(size_t i=0; i<sub_indices.size(); ++i){
                    uint32_t old_idx = sub_indices[i];
                    g.indices[i] = (old_idx < mb.VertexCount) ? old_to_new[old_idx] : 0;
                }

                g.normals.resize(new_vert_count * 3);
                compute_smooth_normals(new_vert_count, g.indices, g.positions, g.normals);

                g.MeshIndex = (uint32_t)mi;
                g.SubMeshIndex = (uint32_t)si;
                std::string baseName;
                if(mi<info.Meshes.size() && !info.Meshes[mi].MeshName.empty())
                    baseName = info.Meshes[mi].MeshName;
                else
                    baseName = "mesh_" + std::to_string(mi);
                g.name = baseName + "_" + std::to_string(si);

                out.push_back(std::move(g));
            }
        }
    }
    return true;
}