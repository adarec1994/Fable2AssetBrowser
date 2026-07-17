#include "MdlWriter.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace MdlWriter {

namespace {





constexpr uint32_t kHdrTailFlags0 = 0x01000000u;
constexpr float    kHdrTailF1     = 0.05f;
constexpr float    kHdrTailF2     = 1.0f;
constexpr uint32_t kHdrTailFlags1 = 0u;
constexpr uint32_t kHdrTailFlags2 = 0u;
constexpr uint32_t kHdrTailFlags3 = 0x100u;
constexpr float    kHdrTailF3     = 10000.0f;

constexpr float    kMeshHdrLodNear = 1.0f;    
constexpr float    kMeshHdrLodFar  = 99.0f;
constexpr float    kMeshHdrScale   = 1.0f;    
constexpr uint32_t kMeshHdrU0      = 0u;      
constexpr uint32_t kMeshHdrU1      = 0u;
constexpr uint32_t kMeshHdrU2      = 0xFFFFFFFFu;
constexpr float    kMatFloat0      = 0.1f;
constexpr float    kMatFloat1      = 0.17f;

constexpr uint16_t kVertexFlag     = 0u;      
constexpr uint32_t kSubmeshMarker  = 0xFFFFFFFFu; 

struct W {
    std::vector<uint8_t>& v;
    void u8(uint8_t x) { v.push_back(x); }
    void u16be(uint16_t x) { v.push_back((uint8_t)(x >> 8)); v.push_back((uint8_t)x); }
    void u32be(uint32_t x) {
        v.push_back((uint8_t)(x >> 24)); v.push_back((uint8_t)(x >> 16));
        v.push_back((uint8_t)(x >> 8));  v.push_back((uint8_t)x);
    }
    void f32be(float f) { uint32_t u; std::memcpy(&u, &f, 4); u32be(u); }
    void strz(const std::string& s) {
        v.insert(v.end(), s.begin(), s.end());
        v.push_back(0);
    }
    void zeros(size_t n) { v.insert(v.end(), n, 0); }
};

uint16_t float_to_half(float f) {
    uint32_t x; std::memcpy(&x, &f, 4);
    const uint32_t sign = (x >> 16) & 0x8000u;
    int32_t exp = (int32_t)((x >> 23) & 0xFF) - 127 + 15;
    uint32_t man = x & 0x7FFFFFu;
    if (exp <= 0) {
        if (exp < -10) return (uint16_t)sign;              
        man |= 0x800000u;
        uint32_t shift = (uint32_t)(14 - exp);
        uint32_t half_man = man >> shift;
        
        if ((man >> (shift - 1)) & 1u) ++half_man;
        return (uint16_t)(sign | half_man);
    }
    if (exp >= 31) return (uint16_t)(sign | 0x7C00u);      
    uint32_t half = sign | ((uint32_t)exp << 10) | (man >> 13);
    if (man & 0x1000u) ++half;                             
    return (uint16_t)half;
}

inline int clamp_snorm(float v, int scale) {
    float s = v * (float)scale;
    int i = (int)std::lround(s);
    if (i > scale) i = scale;
    if (i < -scale) i = -scale;
    return i;
}


uint32_t pack_normal_11_11_10(const float n[3]) {
    uint32_t xb = (uint32_t)(clamp_snorm(n[0], 1023) & 0x7FF);
    uint32_t yb = (uint32_t)(clamp_snorm(n[1], 1023) & 0x7FF);
    uint32_t zb = (uint32_t)(clamp_snorm(n[2], 511) & 0x3FF);
    return xb | (yb << 11) | (zb << 22);
}


uint32_t pack_tangent_dec3n(const float t[3], float sign) {
    uint32_t xb = (uint32_t)(clamp_snorm(t[0], 511) & 0x3FF);
    uint32_t yb = (uint32_t)(clamp_snorm(t[1], 511) & 0x3FF);
    uint32_t zb = (uint32_t)(clamp_snorm(t[2], 511) & 0x3FF);
    uint32_t sb = (sign >= 0.0f) ? 1u : 3u;
    return xb | (yb << 10) | (zb << 20) | (sb << 30);
}

struct Bounds {
    float mn[3] = { 1e30f,  1e30f,  1e30f};
    float mx[3] = {-1e30f, -1e30f, -1e30f};
    void add(const float* p) {
        for (int i = 0; i < 3; ++i) {
            mn[i] = std::min(mn[i], p[i]);
            mx[i] = std::max(mx[i], p[i]);
        }
    }
    void add(const Bounds& b) {
        for (int i = 0; i < 3; ++i) {
            mn[i] = std::min(mn[i], b.mn[i]);
            mx[i] = std::max(mx[i], b.mx[i]);
        }
    }
    bool valid() const { return mn[0] <= mx[0]; }
    void fix() {
        if (!valid()) { for (int i = 0; i < 3; ++i) { mn[i] = 0; mx[i] = 0; } }
    }
    void center(float c[3]) const {
        for (int i = 0; i < 3; ++i) c[i] = 0.5f * (mn[i] + mx[i]);
    }
    float radius_from(const float c[3], const std::vector<float>& pos) const {
        float r2 = 0.0f;
        for (size_t v = 0; v + 2 < pos.size(); v += 3) {
            float dx = pos[v] - c[0], dy = pos[v+1] - c[1], dz = pos[v+2] - c[2];
            r2 = std::max(r2, dx*dx + dy*dy + dz*dz);
        }
        return std::sqrt(r2);
    }
    float radius_corner(const float c[3]) const {
        float dx = std::max(std::abs(mx[0] - c[0]), std::abs(mn[0] - c[0]));
        float dy = std::max(std::abs(mx[1] - c[1]), std::abs(mn[1] - c[1]));
        float dz = std::max(std::abs(mx[2] - c[2]), std::abs(mn[2] - c[2]));
        return std::sqrt(dx*dx + dy*dy + dz*dz);
    }
};


void compute_tangents(const MeshInput& m,
                      std::vector<float>& tan, std::vector<float>& sign)
{
    const size_t vcount = m.positions.size() / 3;
    std::vector<float> ta(vcount * 3, 0.0f), tb(vcount * 3, 0.0f);
    for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        uint32_t i0 = m.indices[t], i1 = m.indices[t+1], i2 = m.indices[t+2];
        if (i0 >= vcount || i1 >= vcount || i2 >= vcount) continue;
        const float* p0 = &m.positions[i0*3];
        const float* p1 = &m.positions[i1*3];
        const float* p2 = &m.positions[i2*3];
        const float* w0 = &m.uvs[i0*2];
        const float* w1 = &m.uvs[i1*2];
        const float* w2 = &m.uvs[i2*2];
        float e1[3] = {p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2]};
        float e2[3] = {p2[0]-p0[0], p2[1]-p0[1], p2[2]-p0[2]};
        float du1 = w1[0]-w0[0], dv1 = w1[1]-w0[1];
        float du2 = w2[0]-w0[0], dv2 = w2[1]-w0[1];
        float det = du1 * dv2 - du2 * dv1;
        if (std::abs(det) < 1e-12f) continue;
        float r = 1.0f / det;
        float T[3] = {(e1[0]*dv2 - e2[0]*dv1) * r,
                      (e1[1]*dv2 - e2[1]*dv1) * r,
                      (e1[2]*dv2 - e2[2]*dv1) * r};
        float B[3] = {(e2[0]*du1 - e1[0]*du2) * r,
                      (e2[1]*du1 - e1[1]*du2) * r,
                      (e2[2]*du1 - e1[2]*du2) * r};
        for (uint32_t vi : {i0, i1, i2}) {
            for (int c = 0; c < 3; ++c) {
                ta[vi*3+c] += T[c];
                tb[vi*3+c] += B[c];
            }
        }
    }
    tan.assign(vcount * 3, 0.0f);
    sign.assign(vcount, 1.0f);
    for (size_t v = 0; v < vcount; ++v) {
        const float* n = &m.normals[v*3];
        float t[3] = {ta[v*3], ta[v*3+1], ta[v*3+2]};
        float ndt = n[0]*t[0] + n[1]*t[1] + n[2]*t[2];
        t[0] -= n[0]*ndt; t[1] -= n[1]*ndt; t[2] -= n[2]*ndt;
        float l = std::sqrt(t[0]*t[0] + t[1]*t[1] + t[2]*t[2]);
        if (l < 1e-9f) {
            
            float ax = std::abs(n[0]), ay = std::abs(n[1]), az = std::abs(n[2]);
            float up[3] = {0, 0, 1};
            if (az >= ax && az >= ay) { up[0] = 1; up[2] = 0; }
            t[0] = n[1]*up[2] - n[2]*up[1];
            t[1] = n[2]*up[0] - n[0]*up[2];
            t[2] = n[0]*up[1] - n[1]*up[0];
            l = std::sqrt(t[0]*t[0] + t[1]*t[1] + t[2]*t[2]);
            if (l < 1e-9f) { t[0] = 1; t[1] = 0; t[2] = 0; l = 1; }
        }
        tan[v*3+0] = t[0]/l; tan[v*3+1] = t[1]/l; tan[v*3+2] = t[2]/l;
        float cx = n[1]*t[2] - n[2]*t[1];
        float cy = n[2]*t[0] - n[0]*t[2];
        float cz = n[0]*t[1] - n[1]*t[0];
        float d = cx*tb[v*3] + cy*tb[v*3+1] + cz*tb[v*3+2];
        sign[v] = (d < 0.0f) ? -1.0f : 1.0f;
    }
}



void split_mesh(const MeshInput& src, std::vector<MeshInput>& out)
{
    const size_t vcount = src.positions.size() / 3;
    if (vcount <= 0xFFFF) { out.push_back(src); return; }

    const uint32_t kMax = 0xFFFF;
    std::vector<int32_t> remap(vcount, -1);
    MeshInput cur;
    auto flush = [&](size_t part) {
        if (!cur.indices.empty()) {
            cur.name = src.name + "_part" + std::to_string(part);
            cur.tex_diffuse = src.tex_diffuse;
            cur.tex_specular = src.tex_specular;
            cur.tex_normal = src.tex_normal;
            cur.tex_metallic = src.tex_metallic;
            cur.tex_extra = src.tex_extra;
            out.push_back(std::move(cur));
        }
        cur = MeshInput{};
        std::fill(remap.begin(), remap.end(), -1);
    };
    size_t part = 0;
    for (size_t t = 0; t + 2 < src.indices.size(); t += 3) {
        
        if (cur.positions.size() / 3 + 3 > kMax) flush(part++);
        for (int k = 0; k < 3; ++k) {
            uint32_t ov = src.indices[t + k];
            if (remap[ov] < 0) {
                remap[ov] = (int32_t)(cur.positions.size() / 3);
                cur.positions.insert(cur.positions.end(),
                                     src.positions.begin() + ov*3,
                                     src.positions.begin() + ov*3 + 3);
                cur.normals.insert(cur.normals.end(),
                                   src.normals.begin() + ov*3,
                                   src.normals.begin() + ov*3 + 3);
                cur.uvs.insert(cur.uvs.end(),
                               src.uvs.begin() + ov*2,
                               src.uvs.begin() + ov*2 + 2);
            }
            cur.indices.push_back((uint32_t)remap[ov]);
        }
    }
    flush(part);
}

}  

bool build(const std::vector<MeshInput>& meshes_in, BuiltMdl& out,
           std::string& err)
{
    out = BuiltMdl{};
    if (meshes_in.empty()) { err = "no meshes to write"; return false; }

    std::vector<MeshInput> meshes;
    for (const auto& m : meshes_in) {
        if (m.positions.empty() || m.indices.size() < 3) continue;
        if (m.positions.size() % 3 || m.normals.size() != m.positions.size() ||
            m.uvs.size() / 2 != m.positions.size() / 3) {
            err = "mesh '" + m.name + "' has inconsistent attribute counts";
            return false;
        }
        split_mesh(m, meshes);
    }
    if (meshes.empty()) { err = "no non-empty meshes to write"; return false; }

    
    Bounds model_b;
    for (const auto& m : meshes)
        for (size_t v = 0; v + 2 < m.positions.size(); v += 3)
            model_b.add(&m.positions[v]);
    model_b.fix();
    float mc[3]; model_b.center(mc);
    float mr = 0.0f;
    for (const auto& m : meshes)
        mr = std::max(mr, model_b.radius_from(mc, m.positions));
    if (mr <= 0.0f) mr = model_b.radius_corner(mc);

    
    std::vector<uint8_t> sec;
    W w{sec};

    w.zeros(32);                       
    w.u32be(0);                        
    w.u32be(0);                        
    w.f32be(mc[0]); w.f32be(mc[1]); w.f32be(mc[2]); w.f32be(mr);   
    for (int i = 0; i < 3; ++i) w.f32be(model_b.mn[i]);            
    for (int i = 0; i < 3; ++i) w.f32be(model_b.mx[i]);            

    const uint32_t mcount = (uint32_t)meshes.size();
    w.u32be(mcount);                   
    w.u32be(mcount);                   
    w.u32be(0);                        
    w.u32be(0);                        
    w.u32be(0);                        
    w.u8(0);                           
    w.u32be(0);                        
    for (int i = 0; i < 5; ++i) w.u32be(0);   
    w.u32be(0);                        
    w.u32be(0);                        

    
    for (const auto& m : meshes) {
        w.u32be(0);                    
        w.strz(m.name);
        w.f32be(kMeshHdrLodNear);
        w.f32be(kMeshHdrLodFar);
        w.zeros(21);
        w.f32be(kMeshHdrScale);
        w.u32be(kMeshHdrU0);
        w.u32be(kMeshHdrU1);
        w.u32be(kMeshHdrU2);
        w.u32be(1);                    
        w.strz(m.tex_diffuse);         
        w.strz(m.tex_specular);
        w.strz(m.tex_normal);
        w.strz(m.tex_metallic);
        w.strz(m.tex_extra);
        for (int k = 0; k < 4; ++k) w.strz(std::string());
        w.f32be(kMatFloat0);
        w.f32be(kMatFloat1);
    }

    
    uint32_t total_vertices = 0, total_tris = 0;
    for (size_t mi = 0; mi < meshes.size(); ++mi) {
        const auto& m = meshes[mi];
        const size_t vcount = m.positions.size() / 3;
        const uint32_t idxc = (uint32_t)(m.indices.size() / 3) * 3;
        const uint32_t tric = idxc / 3;
        total_vertices += (uint32_t)vcount;
        total_tris += tric;

        Bounds b;
        for (size_t v = 0; v + 2 < m.positions.size(); v += 3)
            b.add(&m.positions[v]);
        b.fix();
        float c[3]; b.center(c);
        float r = b.radius_from(c, m.positions);
        if (r <= 0.0f) r = b.radius_corner(c);

        std::vector<float> tan, tsign;
        compute_tangents(m, tan, tsign);

        w.strz(m.name);
        w.u8(0x01);
        w.u32be((uint32_t)mi);         
        w.u32be((uint32_t)mi);         
        w.u32be(tric);                 
        w.u32be(idxc);                 
        w.u32be((uint32_t)vcount);     
        for (int i = 0; i < 3; ++i) w.f32be(b.mn[i]);   
        for (int i = 0; i < 3; ++i) w.f32be(b.mx[i]);   
        w.f32be(c[0]); w.f32be(c[1]); w.f32be(c[2]); w.f32be(r);  

        w.u32be(1);                    
        w.u32be(kSubmeshMarker);       
        w.u32be(0);                    
        w.u8(0);                       
        w.u32be(tric);                 
        w.u32be(0);                    
        for (int i = 0; i < 3; ++i) w.f32be(b.mn[i]);   
        for (int i = 0; i < 3; ++i) w.f32be(b.mx[i]);

        
        for (size_t v = 0; v < vcount; ++v) {
            w.u16be(float_to_half(m.positions[v*3+0]));
            w.u16be(float_to_half(m.positions[v*3+1]));
            w.u16be(float_to_half(m.positions[v*3+2]));
            w.u16be(kVertexFlag);
            w.u32be(pack_normal_11_11_10(&m.normals[v*3]));
            w.u16be(float_to_half(m.uvs[v*2+0]));
            w.u16be(float_to_half(m.uvs[v*2+1]));
            w.u32be(pack_tangent_dec3n(&tan[v*3], tsign[v]));
        }
        for (uint32_t i = 0; i < idxc; ++i)
            w.u16be((uint16_t)m.indices[i]);
        
        w.u32be(0);                    
        
    }

    
    out.header.reserve(104);
    W h{out.header};
    h.v.insert(h.v.end(), {'M','e','s','h','F','i','l','e'});
    h.u32be(36);                       
    h.u32be(88);                       
    h.f32be(mc[0]); h.f32be(mc[1]); h.f32be(mc[2]); h.f32be(mr);
    for (int i = 0; i < 3; ++i) h.f32be(model_b.mn[i]);
    for (int i = 0; i < 3; ++i) h.f32be(model_b.mx[i]);
    h.u32be(1);                        
    h.u32be((uint32_t)sec.size());     
    h.u32be(0);
    h.u32be(0);
    h.f32be(2.0f * mr);
    h.u32be(kHdrTailFlags0);
    h.f32be(kHdrTailF1);
    h.f32be(kHdrTailF2);
    h.u32be(kHdrTailFlags1);
    h.u32be(kHdrTailFlags2);
    h.u32be(kHdrTailFlags3);
    h.f32be(kHdrTailF3);

    if (out.header.size() != 104) { err = "internal: header not 104 bytes"; return false; }

    out.body = std::move(sec);
    out.mesh_count = mcount;
    out.vertex_count = total_vertices;
    out.triangle_count = total_tris;
    return true;
}

}
