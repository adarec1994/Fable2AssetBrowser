#include "MdlFbxExport.h"
#include "ModelParser.h"
#include "MdlTexExport.h"
#include "../textures/TexParser.h"
#include "../Utilities/State.h"
#include "../Utilities/Utils.h"
#include "../Utilities/Files.h"
#include "../UI/OutputLog.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <vector>
#include <string>
#include <chrono>

extern bool mdl_export_decode_texture_to_png(
    const std::vector<unsigned char>& tex_buf,
    std::vector<uint8_t>& png_out);

extern bool build_any_tex_buffer_for_name(const std::string& tex_name,
                                          std::vector<unsigned char>& out,
                                          const std::string& preferred_bnk);

namespace {

struct Buf {
    std::vector<uint8_t> data;
    void put_u8 (uint8_t  v) { data.push_back(v); }
    void put_u16(uint16_t v) { put_u8(v & 0xFF); put_u8((v >> 8) & 0xFF); }
    void put_u32(uint32_t v) {
        put_u8(v & 0xFF); put_u8((v >> 8) & 0xFF);
        put_u8((v >> 16) & 0xFF); put_u8((v >> 24) & 0xFF);
    }
    void put_u64(uint64_t v) {
        put_u32((uint32_t)(v & 0xFFFFFFFFu));
        put_u32((uint32_t)((v >> 32) & 0xFFFFFFFFu));
    }
    void put_i32(int32_t v)  { put_u32((uint32_t)v); }
    void put_i64(int64_t v)  { put_u64((uint64_t)v); }
    void put_f32(float v)    { uint32_t u; std::memcpy(&u, &v, 4); put_u32(u); }
    void put_f64(double v)   { uint64_t u; std::memcpy(&u, &v, 8); put_u64(u); }
    void put_bytes(const void* p, size_t n) {
        const uint8_t* b = (const uint8_t*)p;
        data.insert(data.end(), b, b + n);
    }
};

struct Prop {
    enum Type { I32, I64, F32, F64, STRING, BYTES, ARR_I32, ARR_I64, ARR_F32, ARR_F64 } type;
    int32_t  i32  = 0;
    int64_t  i64  = 0;
    float    f32  = 0;
    double   f64  = 0;
    std::string str;
    bool     str_is_bytes = false;
    std::vector<int32_t> ai;
    std::vector<int64_t> al;
    std::vector<float>   af;
    std::vector<double>  ad;

    static Prop I (int32_t v)            { Prop p; p.type = I32; p.i32 = v; return p; }
    static Prop L (int64_t v)            { Prop p; p.type = I64; p.i64 = v; return p; }
    static Prop F (float v)              { Prop p; p.type = F32; p.f32 = v; return p; }
    static Prop D (double v)             { Prop p; p.type = F64; p.f64 = v; return p; }
    static Prop S (const std::string& s) { Prop p; p.type = STRING; p.str = s; return p; }
    static Prop R (const std::vector<uint8_t>& b) {
        Prop p; p.type = BYTES;
        p.str.assign((const char*)b.data(), b.size());
        p.str_is_bytes = true;
        return p;
    }
    static Prop AI(std::vector<int32_t> v) { Prop p; p.type = ARR_I32; p.ai = std::move(v); return p; }
    static Prop AL(std::vector<int64_t> v) { Prop p; p.type = ARR_I64; p.al = std::move(v); return p; }
    static Prop AF(std::vector<float>   v) { Prop p; p.type = ARR_F32; p.af = std::move(v); return p; }
    static Prop AD(std::vector<double>  v) { Prop p; p.type = ARR_F64; p.ad = std::move(v); return p; }

    void emit(Buf& out) const {
        switch (type) {
            case I32:    out.put_u8('I'); out.put_i32(i32); break;
            case I64:    out.put_u8('L'); out.put_i64(i64); break;
            case F32:    out.put_u8('F'); out.put_f32(f32); break;
            case F64:    out.put_u8('D'); out.put_f64(f64); break;
            case STRING: out.put_u8(str_is_bytes ? 'R' : 'S');
                         out.put_u32((uint32_t)str.size());
                         out.put_bytes(str.data(), str.size());
                         break;
            case BYTES:  out.put_u8('R');
                         out.put_u32((uint32_t)str.size());
                         out.put_bytes(str.data(), str.size());
                         break;

            case ARR_I32: {
                out.put_u8('i');
                out.put_u32((uint32_t)ai.size());
                out.put_u32(0);
                out.put_u32((uint32_t)(ai.size() * sizeof(int32_t)));
                for (auto v : ai) out.put_i32(v);
                break;
            }
            case ARR_I64: {
                out.put_u8('l');
                out.put_u32((uint32_t)al.size());
                out.put_u32(0);
                out.put_u32((uint32_t)(al.size() * sizeof(int64_t)));
                for (auto v : al) out.put_i64(v);
                break;
            }
            case ARR_F32: {
                out.put_u8('f');
                out.put_u32((uint32_t)af.size());
                out.put_u32(0);
                out.put_u32((uint32_t)(af.size() * sizeof(float)));
                for (auto v : af) out.put_f32(v);
                break;
            }
            case ARR_F64: {
                out.put_u8('d');
                out.put_u32((uint32_t)ad.size());
                out.put_u32(0);
                out.put_u32((uint32_t)(ad.size() * sizeof(double)));
                for (auto v : ad) out.put_f64(v);
                break;
            }
        }
    }

    size_t byte_size() const {
        switch (type) {
            case I32: return 1 + 4;
            case I64: return 1 + 8;
            case F32: return 1 + 4;
            case F64: return 1 + 8;
            case STRING:
            case BYTES: return 1 + 4 + str.size();
            case ARR_I32: return 1 + 12 + ai.size() * 4;
            case ARR_I64: return 1 + 12 + al.size() * 8;
            case ARR_F32: return 1 + 12 + af.size() * 4;
            case ARR_F64: return 1 + 12 + ad.size() * 8;
        }
        return 0;
    }
};

struct Node {
    std::string name;
    std::vector<Prop> props;
    std::vector<Node> children;

    Node() = default;
    Node(std::string n) : name(std::move(n)) {}

    Node& add_prop(Prop p) { props.push_back(std::move(p)); return *this; }
    Node& add_child(Node c) {
        children.push_back(std::move(c));
        return children.back();
    }
};

void write_node(Buf& out, const Node& n) {
    size_t header_off = out.data.size();
    out.put_u32(0);
    out.put_u32((uint32_t)n.props.size());

    size_t prop_list_len_off = out.data.size();
    out.put_u32(0);
    out.put_u8((uint8_t)n.name.size());
    out.put_bytes(n.name.data(), n.name.size());

    size_t props_start = out.data.size();
    for (const auto& p : n.props) p.emit(out);
    size_t props_end = out.data.size();

    uint32_t plen = (uint32_t)(props_end - props_start);
    out.data[prop_list_len_off + 0] = (uint8_t)(plen & 0xFF);
    out.data[prop_list_len_off + 1] = (uint8_t)((plen >> 8) & 0xFF);
    out.data[prop_list_len_off + 2] = (uint8_t)((plen >> 16) & 0xFF);
    out.data[prop_list_len_off + 3] = (uint8_t)((plen >> 24) & 0xFF);

    for (const auto& c : n.children) write_node(out, c);
    if (!n.children.empty()) {

        for (int i = 0; i < 13; ++i) out.put_u8(0);
    }

    uint32_t end_off = (uint32_t)out.data.size();
    out.data[header_off + 0] = (uint8_t)(end_off & 0xFF);
    out.data[header_off + 1] = (uint8_t)((end_off >> 8) & 0xFF);
    out.data[header_off + 2] = (uint8_t)((end_off >> 16) & 0xFF);
    out.data[header_off + 3] = (uint8_t)((end_off >> 24) & 0xFF);
}

Node make_p(const char* name, const char* type1, const char* type2,
            const char* flags, std::vector<Prop> values) {
    Node p("P");
    p.add_prop(Prop::S(name));
    p.add_prop(Prop::S(type1));
    p.add_prop(Prop::S(type2));
    p.add_prop(Prop::S(flags));
    for (auto& v : values) p.add_prop(std::move(v));
    return p;
}

struct IdGen {
    int64_t next = 1000000000ll;
    int64_t make() { return next++; }
};

struct Mat4 {
    double m[16];
    static Mat4 identity() {
        Mat4 r{};
        for (int i = 0; i < 16; ++i) r.m[i] = 0.0;
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0;
        return r;
    }
    static Mat4 multiply(const Mat4& a, const Mat4& b) {
        Mat4 r{};
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                double s = 0;
                for (int k = 0; k < 4; ++k)
                    s += a.m[i*4 + k] * b.m[k*4 + j];
                r.m[i*4 + j] = s;
            }
        }
        return r;
    }

    std::vector<double> as_column_major() const {
        std::vector<double> out(16);
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                out[j*4 + i] = m[i*4 + j];
        return out;
    }
    static Mat4 from_trs_quat(double qx, double qy, double qz, double qw,
                              double tx, double ty, double tz,
                              double sx, double sy, double sz) {

        const double xx = qx*qx, yy = qy*qy, zz = qz*qz;
        const double xy = qx*qy, xz = qx*qz, yz = qy*qz;
        const double wx = qw*qx, wy = qw*qy, wz = qw*qz;
        Mat4 r{};
        r.m[0]  = (1 - 2*(yy + zz)) * sx;
        r.m[1]  = (2*(xy - wz))     * sy;
        r.m[2]  = (2*(xz + wy))     * sz;
        r.m[3]  = tx;
        r.m[4]  = (2*(xy + wz))     * sx;
        r.m[5]  = (1 - 2*(xx + zz)) * sy;
        r.m[6]  = (2*(yz - wx))     * sz;
        r.m[7]  = ty;
        r.m[8]  = (2*(xz - wy))     * sx;
        r.m[9]  = (2*(yz + wx))     * sy;
        r.m[10] = (1 - 2*(xx + yy)) * sz;
        r.m[11] = tz;
        r.m[12] = 0; r.m[13] = 0; r.m[14] = 0; r.m[15] = 1;
        return r;
    }
    Mat4 inverse_or_identity() const {

        Mat4 inv{};
        const double* a = m;
        inv.m[0]  =  a[5]*a[10]*a[15] - a[5]*a[11]*a[14] - a[9]*a[6]*a[15] +
                     a[9]*a[7]*a[14] + a[13]*a[6]*a[11] - a[13]*a[7]*a[10];
        inv.m[4]  = -a[4]*a[10]*a[15] + a[4]*a[11]*a[14] + a[8]*a[6]*a[15] -
                     a[8]*a[7]*a[14] - a[12]*a[6]*a[11] + a[12]*a[7]*a[10];
        inv.m[8]  =  a[4]*a[9]*a[15]  - a[4]*a[11]*a[13] - a[8]*a[5]*a[15] +
                     a[8]*a[7]*a[13]  + a[12]*a[5]*a[11] - a[12]*a[7]*a[9];
        inv.m[12] = -a[4]*a[9]*a[14]  + a[4]*a[10]*a[13] + a[8]*a[5]*a[14] -
                     a[8]*a[6]*a[13]  - a[12]*a[5]*a[10] + a[12]*a[6]*a[9];
        inv.m[1]  = -a[1]*a[10]*a[15] + a[1]*a[11]*a[14] + a[9]*a[2]*a[15] -
                     a[9]*a[3]*a[14]  - a[13]*a[2]*a[11] + a[13]*a[3]*a[10];
        inv.m[5]  =  a[0]*a[10]*a[15] - a[0]*a[11]*a[14] - a[8]*a[2]*a[15] +
                     a[8]*a[3]*a[14]  + a[12]*a[2]*a[11] - a[12]*a[3]*a[10];
        inv.m[9]  = -a[0]*a[9]*a[15]  + a[0]*a[11]*a[13] + a[8]*a[1]*a[15] -
                     a[8]*a[3]*a[13]  - a[12]*a[1]*a[11] + a[12]*a[3]*a[9];
        inv.m[13] =  a[0]*a[9]*a[14]  - a[0]*a[10]*a[13] - a[8]*a[1]*a[14] +
                     a[8]*a[2]*a[13]  + a[12]*a[1]*a[10] - a[12]*a[2]*a[9];
        inv.m[2]  =  a[1]*a[6]*a[15]  - a[1]*a[7]*a[14]  - a[5]*a[2]*a[15] +
                     a[5]*a[3]*a[14]  + a[13]*a[2]*a[7]  - a[13]*a[3]*a[6];
        inv.m[6]  = -a[0]*a[6]*a[15]  + a[0]*a[7]*a[14]  + a[4]*a[2]*a[15] -
                     a[4]*a[3]*a[14]  - a[12]*a[2]*a[7]  + a[12]*a[3]*a[6];
        inv.m[10] =  a[0]*a[5]*a[15]  - a[0]*a[7]*a[13]  - a[4]*a[1]*a[15] +
                     a[4]*a[3]*a[13]  + a[12]*a[1]*a[7]  - a[12]*a[3]*a[5];
        inv.m[14] = -a[0]*a[5]*a[14]  + a[0]*a[6]*a[13]  + a[4]*a[1]*a[14] -
                     a[4]*a[2]*a[13]  - a[12]*a[1]*a[6]  + a[12]*a[2]*a[5];
        inv.m[3]  = -a[1]*a[6]*a[11]  + a[1]*a[7]*a[10]  + a[5]*a[2]*a[11] -
                     a[5]*a[3]*a[10]  - a[9]*a[2]*a[7]   + a[9]*a[3]*a[6];
        inv.m[7]  =  a[0]*a[6]*a[11]  - a[0]*a[7]*a[10]  - a[4]*a[2]*a[11] +
                     a[4]*a[3]*a[10]  + a[8]*a[2]*a[7]   - a[8]*a[3]*a[6];
        inv.m[11] = -a[0]*a[5]*a[11]  + a[0]*a[7]*a[9]   + a[4]*a[1]*a[11] -
                     a[4]*a[3]*a[9]   - a[8]*a[1]*a[7]   + a[8]*a[3]*a[5];
        inv.m[15] =  a[0]*a[5]*a[10]  - a[0]*a[6]*a[9]   - a[4]*a[1]*a[10] +
                     a[4]*a[2]*a[9]   + a[8]*a[1]*a[6]   - a[8]*a[2]*a[5];
        double det = a[0]*inv.m[0] + a[1]*inv.m[4] + a[2]*inv.m[8] + a[3]*inv.m[12];
        if (std::abs(det) < 1e-20) return Mat4::identity();
        double inv_det = 1.0 / det;
        for (int i = 0; i < 16; ++i) inv.m[i] *= inv_det;
        return inv;
    }
};

}

bool mdl_to_fbx_full(const std::vector<unsigned char>& mdl_data,
                     const std::string& fbx_path,
                     const std::string& mdl_source_path,
                     std::string& err_msg) {
    err_msg.clear();

    MDLInfo info;
    if (!parse_mdl_info(mdl_data, info, mdl_source_path)) {
        err_msg = "Failed to parse MDL info";
        return false;
    }
    std::vector<MDLMeshGeom> geoms;
    if (!parse_mdl_geometry(mdl_data, info, geoms)) {
        err_msg = "Failed to parse MDL geometry";
        return false;
    }
    if (geoms.empty()) {
        err_msg = "No geometry found";
        return false;
    }

    IdGen ids;

    struct BoneOut {
        int orig_idx;
        int filtered_idx;
        int parent_filt;
        std::string name;

        double qx, qy, qz, qw, tx, ty, tz, sx, sy, sz;
        Mat4 local;
        Mat4 world;
        int64_t model_id;
        int64_t attr_id;
        int64_t cluster_id;
        bool used_as_cluster;
    };
    std::vector<BoneOut> bones;
    std::vector<int> orig_to_filt(info.BoneCount, -1);
    for (size_t i = 0; i < info.Bones.size(); ++i) {
        if (info.Bones[i].Name.find("Rig_Asset") != std::string::npos) continue;
        BoneOut b{};
        b.orig_idx = (int)i;
        b.filtered_idx = (int)bones.size();
        b.name = info.Bones[i].Name;
        b.parent_filt = -1;
        if (i < info.BoneTransforms.size() &&
            info.BoneTransforms[i].size() >= 10) {
            const auto& tf = info.BoneTransforms[i];
            b.qx = tf[0]; b.qy = tf[1]; b.qz = tf[2]; b.qw = tf[3];
            b.tx = tf[4]; b.ty = tf[5]; b.tz = tf[6];
            b.sx = tf[7]; b.sy = tf[8]; b.sz = tf[9];
        } else {
            b.qx = b.qy = b.qz = 0; b.qw = 1;
            b.tx = b.ty = b.tz = 0;
            b.sx = b.sy = b.sz = 1;
        }
        b.local = Mat4::from_trs_quat(b.qx, b.qy, b.qz, b.qw,
                                      b.tx, b.ty, b.tz,
                                      b.sx, b.sy, b.sz);
        b.world = Mat4::identity();
        b.model_id = ids.make();
        b.attr_id  = ids.make();
        b.cluster_id = 0;
        b.used_as_cluster = false;
        orig_to_filt[i] = b.filtered_idx;
        bones.push_back(std::move(b));
    }

    for (auto& b : bones) {
        int parent_orig = info.Bones[b.orig_idx].ParentID;
        if (parent_orig >= 0 && parent_orig < (int)orig_to_filt.size()) {
            int pf = orig_to_filt[parent_orig];
            if (pf >= 0 && pf != b.filtered_idx) b.parent_filt = pf;
        }
    }
    for (auto& b : bones) {
        if (b.parent_filt < 0) {
            b.world = b.local;
        } else {
            b.world = Mat4::multiply(bones[b.parent_filt].world, b.local);
        }
    }

    {
        constexpr double kS2 = 0.70710678118654752440;
        for (auto& b : bones) {
            if (b.parent_filt >= 0) continue;

            double old_ty = b.ty, old_tz = b.tz;
            b.ty = -old_tz;
            b.tz =  old_ty;

            double q2x = b.qx, q2y = b.qy, q2z = b.qz, q2w = b.qw;
            b.qx = kS2 * (q2x + q2w);
            b.qy = kS2 * (q2y - q2z);
            b.qz = kS2 * (q2z + q2y);
            b.qw = kS2 * (q2w - q2x);

            b.local = Mat4::from_trs_quat(b.qx, b.qy, b.qz, b.qw,
                                          b.tx, b.ty, b.tz,
                                          b.sx, b.sy, b.sz);
        }

        for (auto& b : bones) {
            if (b.parent_filt < 0) {
                b.world = b.local;
            } else {
                b.world = Mat4::multiply(bones[b.parent_filt].world, b.local);
            }
        }
    }

    enum TexChannel { CH_DIFFUSE, CH_NORMAL, CH_SPECULAR, CH_METALLIC, CH_EXTRA, CH_COUNT };

    struct EmbeddedTex {
        int64_t  video_id   = 0;
        int64_t  tex_id     = 0;
        std::string name;
        std::string filename;
        std::string mime_ext;
        std::vector<uint8_t> bytes;
        const char* fbx_property = "DiffuseColor";
    };

    struct MeshOut {
        int64_t geom_id;
        int64_t model_id;
        int64_t mat_id;
        int64_t skin_id;
        std::string name;
        EmbeddedTex tex[CH_COUNT];

        std::unordered_map<int, int64_t> cluster_for_bone;
        std::unordered_map<int, std::vector<int>> cluster_indexes;
        std::unordered_map<int, std::vector<double>> cluster_weights;
    };
    std::vector<MeshOut> meshes_out;
    meshes_out.reserve(geoms.size());

    const MdlTexExport::Format tex_fmt =
        MdlTexExport::format_from_string(S.mdl_texture_export_format);

    for (size_t gi = 0; gi < geoms.size(); ++gi) {
        const auto& g = geoms[gi];
        MeshOut mo{};
        mo.geom_id  = ids.make();
        mo.model_id = ids.make();
        mo.mat_id   = ids.make();
        mo.skin_id  = ids.make();
        mo.name     = g.name.empty() ? ("mesh_" + std::to_string(gi)) : g.name;

        struct ChanBinding {
            const std::string& src;
            const char* fbx_prop;
        };
        const ChanBinding bindings[CH_COUNT] = {
            { g.diffuse_tex_name,  "DiffuseColor"    },
            { g.normal_tex_name,   "NormalMap"       },
            { g.specular_tex_name, "SpecularColor"   },
            { g.metallic_tex_name, "ReflectionColor" },
            { g.extra_tex_name,    "EmissiveColor"   },
        };

        for (int ch = 0; ch < CH_COUNT; ++ch) {
            const auto& bind = bindings[ch];
            if (bind.src.empty()) continue;
            std::vector<unsigned char> tex_buf;
            if (!build_any_tex_buffer_for_name(bind.src, tex_buf,
                                               std::string())) {
                continue;
            }
            MdlTexExport::EncodedTex enc;
            if (!MdlTexExport::encode_largest_mip(tex_buf, tex_fmt, enc) ||
                enc.bytes.empty()) {
                continue;
            }
            EmbeddedTex& et = mo.tex[ch];
            et.video_id     = ids.make();
            et.tex_id       = ids.make();
            et.name         = bind.src;
            et.mime_ext     = enc.extension;
            et.filename     = bind.src + et.mime_ext;
            et.bytes        = std::move(enc.bytes);
            et.fbx_property = bind.fbx_prop;
        }

        const size_t vcount = g.positions.size() / 3;
        for (size_t v = 0; v < vcount; ++v) {
            for (int j = 0; j < 4; ++j) {
                if (v * 4 + j >= g.bone_ids.size()) break;
                int orig = g.bone_ids[v * 4 + j];
                float w = (v * 4 + j < g.bone_weights.size())
                              ? g.bone_weights[v * 4 + j] : 0.0f;
                if (w <= 0.0f) continue;
                if (orig < 0 || orig >= (int)orig_to_filt.size()) continue;
                int filt = orig_to_filt[orig];
                if (filt < 0) continue;
                mo.cluster_indexes[filt].push_back((int)v);
                mo.cluster_weights[filt].push_back((double)w);
            }
        }
        for (auto& kv : mo.cluster_indexes) {
            int filt = kv.first;
            int64_t cid = ids.make();
            mo.cluster_for_bone[filt] = cid;
            bones[filt].used_as_cluster = true;
        }

        meshes_out.push_back(std::move(mo));
    }

    Node root("");

    {
        Node hdr("FBXHeaderExtension");
        hdr.add_child(Node("FBXHeaderVersion")).add_prop(Prop::I(1003));
        hdr.add_child(Node("FBXVersion")).add_prop(Prop::I(7400));
        hdr.add_child(Node("EncryptionType")).add_prop(Prop::I(0));
        Node ts("CreationTimeStamp");
        std::time_t tt = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &tt);
#else
        localtime_r(&tt, &tm);
#endif
        ts.add_child(Node("Version")).add_prop(Prop::I(1000));
        ts.add_child(Node("Year")).add_prop(Prop::I(tm.tm_year + 1900));
        ts.add_child(Node("Month")).add_prop(Prop::I(tm.tm_mon + 1));
        ts.add_child(Node("Day")).add_prop(Prop::I(tm.tm_mday));
        ts.add_child(Node("Hour")).add_prop(Prop::I(tm.tm_hour));
        ts.add_child(Node("Minute")).add_prop(Prop::I(tm.tm_min));
        ts.add_child(Node("Second")).add_prop(Prop::I(tm.tm_sec));
        ts.add_child(Node("Millisecond")).add_prop(Prop::I(0));
        hdr.add_child(std::move(ts));
        hdr.add_child(Node("Creator")).add_prop(Prop::S("Fable 2 Asset Browser"));
        root.add_child(std::move(hdr));
    }
    root.add_child(Node("CreationTime")).add_prop(Prop::S("1970-01-01 00:00:00:000"));
    root.add_child(Node("Creator")).add_prop(Prop::S("Fable 2 Asset Browser"));

    {
        Node gs("GlobalSettings");
        gs.add_child(Node("Version")).add_prop(Prop::I(1000));
        Node props70("Properties70");
        props70.add_child(make_p("UpAxis",         "int", "Integer", "", { Prop::I(2)  }));
        props70.add_child(make_p("UpAxisSign",     "int", "Integer", "", { Prop::I(1)  }));
        props70.add_child(make_p("FrontAxis",      "int", "Integer", "", { Prop::I(1)  }));
        props70.add_child(make_p("FrontAxisSign",  "int", "Integer", "", { Prop::I(-1) }));
        props70.add_child(make_p("CoordAxis",      "int", "Integer", "", { Prop::I(0)  }));
        props70.add_child(make_p("CoordAxisSign",  "int", "Integer", "", { Prop::I(1)  }));
        props70.add_child(make_p("OriginalUpAxis",     "int", "Integer", "", { Prop::I(2) }));
        props70.add_child(make_p("OriginalUpAxisSign", "int", "Integer", "", { Prop::I(1) }));
        props70.add_child(make_p("UnitScaleFactor",         "double", "Number", "", { Prop::D(1.0) }));
        props70.add_child(make_p("OriginalUnitScaleFactor", "double", "Number", "", { Prop::D(1.0) }));
        gs.add_child(std::move(props70));
        root.add_child(std::move(gs));
    }

    {
        Node docs("Documents");
        docs.add_child(Node("Count")).add_prop(Prop::I(1));
        Node doc("Document");
        int64_t doc_id = ids.make();
        doc.add_prop(Prop::L(doc_id));
        doc.add_prop(Prop::S(""));
        doc.add_prop(Prop::S("Scene"));
        doc.add_child(Node("RootNode")).add_prop(Prop::L(0));
        docs.add_child(std::move(doc));
        root.add_child(std::move(docs));
    }
    root.add_child(Node("References"));

    {
        Node defs("Definitions");
        defs.add_child(Node("Version")).add_prop(Prop::I(100));

        int total = 1 + (int)meshes_out.size() * 3;
        for (auto& m : meshes_out) {
            for (int ch = 0; ch < CH_COUNT; ++ch) {
                if (m.tex[ch].video_id) total += 2;
            }
            total += (int)m.cluster_for_bone.size() + 1;
        }
        total += (int)bones.size() * 2;
        defs.add_child(Node("Count")).add_prop(Prop::I(total));
        for (const char* t : {"GlobalSettings", "Geometry", "Model", "Material",
                              "Texture", "Video", "Pose", "NodeAttribute",
                              "Deformer"}) {
            Node ot("ObjectType");
            ot.add_prop(Prop::S(t));
            ot.add_child(Node("Count")).add_prop(Prop::I(1));
            defs.add_child(std::move(ot));
        }
        root.add_child(std::move(defs));
    }

    Node objects("Objects");

    for (const auto& b : bones) {

        Node m("Model");
        m.add_prop(Prop::L(b.model_id));
        m.add_prop(Prop::S("Model::" + b.name));
        m.add_prop(Prop::S("LimbNode"));
        m.add_child(Node("Version")).add_prop(Prop::I(232));
        Node p70("Properties70");
        p70.add_child(make_p("Lcl Translation", "Lcl Translation", "", "A",
                             { Prop::D(b.tx), Prop::D(b.ty), Prop::D(b.tz) }));

        p70.add_child(make_p("Lcl Scaling", "Lcl Scaling", "", "A",
                             { Prop::D(b.sx), Prop::D(b.sy), Prop::D(b.sz) }));

        {
            double sinr_cosp = 2.0 * (b.qw * b.qx + b.qy * b.qz);
            double cosr_cosp = 1.0 - 2.0 * (b.qx * b.qx + b.qy * b.qy);
            double rx = std::atan2(sinr_cosp, cosr_cosp);
            double sinp = 2.0 * (b.qw * b.qy - b.qz * b.qx);
            double ry = std::abs(sinp) >= 1
                ? std::copysign(3.14159265358979323846 / 2.0, sinp)
                : std::asin(sinp);
            double siny_cosp = 2.0 * (b.qw * b.qz + b.qx * b.qy);
            double cosy_cosp = 1.0 - 2.0 * (b.qy * b.qy + b.qz * b.qz);
            double rz = std::atan2(siny_cosp, cosy_cosp);
            const double k = 180.0 / 3.14159265358979323846;
            p70.add_child(make_p("Lcl Rotation", "Lcl Rotation", "", "A",
                                 { Prop::D(rx * k), Prop::D(ry * k), Prop::D(rz * k) }));
        }
        m.add_child(std::move(p70));
        objects.add_child(std::move(m));

        Node na("NodeAttribute");
        na.add_prop(Prop::L(b.attr_id));
        na.add_prop(Prop::S("NodeAttribute::"));
        na.add_prop(Prop::S("LimbNode"));
        Node nap("Properties70");
        nap.add_child(make_p("Size", "double", "Number", "", { Prop::D(1.0) }));
        na.add_child(std::move(nap));
        na.add_child(Node("TypeFlags")).add_prop(Prop::S("Skeleton"));
        objects.add_child(std::move(na));
    }

    for (size_t gi = 0; gi < geoms.size(); ++gi) {
        const auto& g = geoms[gi];
        const auto& mo = meshes_out[gi];

        Node gn("Geometry");
        gn.add_prop(Prop::L(mo.geom_id));
        gn.add_prop(Prop::S("Geometry::" + mo.name));
        gn.add_prop(Prop::S("Mesh"));

        std::vector<double> verts(g.positions.begin(), g.positions.end());
        for (size_t i = 0; i + 2 < verts.size(); i += 3) {
            double y = verts[i + 1], z = verts[i + 2];
            verts[i + 1] = -z;
            verts[i + 2] =  y;
        }
        gn.add_child(Node("Vertices")).add_prop(Prop::AD(std::move(verts)));

        std::vector<int32_t> pvi;
        pvi.reserve(g.indices.size());
        for (size_t i = 0; i < g.indices.size(); i += 3) {
            pvi.push_back((int32_t)g.indices[i + 0]);
            pvi.push_back((int32_t)g.indices[i + 1]);
            pvi.push_back((int32_t)~g.indices[i + 2]);
        }
        gn.add_child(Node("PolygonVertexIndex")).add_prop(Prop::AI(std::move(pvi)));

        gn.add_child(Node("GeometryVersion")).add_prop(Prop::I(124));

        if (g.normals.size() == g.positions.size()) {
            Node n("LayerElementNormal");
            n.add_prop(Prop::I(0));
            n.add_child(Node("Version")).add_prop(Prop::I(101));
            n.add_child(Node("Name")).add_prop(Prop::S(""));
            n.add_child(Node("MappingInformationType")).add_prop(Prop::S("ByVertice"));
            n.add_child(Node("ReferenceInformationType")).add_prop(Prop::S("Direct"));
            std::vector<double> nv(g.normals.begin(), g.normals.end());

            for (size_t i = 0; i + 2 < nv.size(); i += 3) {
                double y = nv[i + 1], z = nv[i + 2];
                nv[i + 1] = -z;
                nv[i + 2] =  y;
            }
            n.add_child(Node("Normals")).add_prop(Prop::AD(std::move(nv)));
            gn.add_child(std::move(n));
        }

        if (!g.uvs.empty()) {
            Node uv("LayerElementUV");
            uv.add_prop(Prop::I(0));
            uv.add_child(Node("Version")).add_prop(Prop::I(101));
            uv.add_child(Node("Name")).add_prop(Prop::S("UVMap"));
            uv.add_child(Node("MappingInformationType")).add_prop(Prop::S("ByVertice"));
            uv.add_child(Node("ReferenceInformationType")).add_prop(Prop::S("Direct"));
            std::vector<double> uvv(g.uvs.begin(), g.uvs.end());

            for (size_t i = 1; i < uvv.size(); i += 2) uvv[i] = 1.0 - uvv[i];
            uv.add_child(Node("UV")).add_prop(Prop::AD(std::move(uvv)));
            gn.add_child(std::move(uv));
        }

        {
            Node lm("LayerElementMaterial");
            lm.add_prop(Prop::I(0));
            lm.add_child(Node("Version")).add_prop(Prop::I(101));
            lm.add_child(Node("Name")).add_prop(Prop::S(""));
            lm.add_child(Node("MappingInformationType")).add_prop(Prop::S("AllSame"));
            lm.add_child(Node("ReferenceInformationType")).add_prop(Prop::S("IndexToDirect"));
            lm.add_child(Node("Materials")).add_prop(Prop::AI({ 0 }));
            gn.add_child(std::move(lm));
        }

        {
            Node l("Layer");
            l.add_prop(Prop::I(0));
            l.add_child(Node("Version")).add_prop(Prop::I(100));
            auto add_le = [&](const char* type) {
                Node le("LayerElement");
                le.add_child(Node("Type")).add_prop(Prop::S(type));
                le.add_child(Node("TypedIndex")).add_prop(Prop::I(0));
                l.add_child(std::move(le));
            };
            if (g.normals.size() == g.positions.size()) add_le("LayerElementNormal");
            if (!g.uvs.empty()) add_le("LayerElementUV");
            add_le("LayerElementMaterial");
            gn.add_child(std::move(l));
        }
        objects.add_child(std::move(gn));

        Node mn("Model");
        mn.add_prop(Prop::L(mo.model_id));
        mn.add_prop(Prop::S("Model::" + mo.name));
        mn.add_prop(Prop::S("Mesh"));
        mn.add_child(Node("Version")).add_prop(Prop::I(232));
        Node mp("Properties70");
        mp.add_child(make_p("Lcl Translation", "Lcl Translation", "", "A",
                            { Prop::D(0.0), Prop::D(0.0), Prop::D(0.0) }));
        mn.add_child(std::move(mp));
        objects.add_child(std::move(mn));

        Node mat("Material");
        mat.add_prop(Prop::L(mo.mat_id));
        mat.add_prop(Prop::S("Material::" + mo.name));
        mat.add_prop(Prop::S(""));
        mat.add_child(Node("Version")).add_prop(Prop::I(102));
        mat.add_child(Node("ShadingModel")).add_prop(Prop::S("phong"));
        mat.add_child(Node("MultiLayer")).add_prop(Prop::I(0));
        Node matp("Properties70");
        matp.add_child(make_p("DiffuseColor", "Color", "", "A",
                              { Prop::D(0.8), Prop::D(0.8), Prop::D(0.8) }));
        matp.add_child(make_p("SpecularColor", "Color", "", "A",
                              { Prop::D(0.2), Prop::D(0.2), Prop::D(0.2) }));
        matp.add_child(make_p("Shininess", "double", "Number", "", { Prop::D(20.0) }));
        mat.add_child(std::move(matp));
        objects.add_child(std::move(mat));

        for (int ch = 0; ch < CH_COUNT; ++ch) {
            const EmbeddedTex& et = mo.tex[ch];
            if (et.video_id == 0) continue;

            Node vn("Video");
            vn.add_prop(Prop::L(et.video_id));
            vn.add_prop(Prop::S("Video::" + et.name));
            vn.add_prop(Prop::S("Clip"));
            vn.add_child(Node("Type")).add_prop(Prop::S("Clip"));
            Node vp("Properties70");
            vp.add_child(make_p("Path", "KString", "XRefUrl", "",
                                { Prop::S(et.filename) }));
            vn.add_child(std::move(vp));
            vn.add_child(Node("UseMipMap")).add_prop(Prop::I(0));
            vn.add_child(Node("Filename")).add_prop(Prop::S(et.filename));
            vn.add_child(Node("RelativeFilename")).add_prop(Prop::S(et.filename));

            vn.add_child(Node("Content")).add_prop(Prop::R(et.bytes));
            objects.add_child(std::move(vn));

            Node tn("Texture");
            tn.add_prop(Prop::L(et.tex_id));
            tn.add_prop(Prop::S("Texture::" + et.name));
            tn.add_prop(Prop::S(""));
            tn.add_child(Node("Type")).add_prop(Prop::S("TextureVideoClip"));
            tn.add_child(Node("Version")).add_prop(Prop::I(202));
            tn.add_child(Node("TextureName")).add_prop(Prop::S("Texture::" + et.name));
            Node tp("Properties70");
            tp.add_child(make_p("UVSet", "KString", "", "", { Prop::S("UVMap") }));
            tn.add_child(std::move(tp));
            tn.add_child(Node("FileName")).add_prop(Prop::S(et.filename));
            tn.add_child(Node("RelativeFilename")).add_prop(Prop::S(et.filename));
            objects.add_child(std::move(tn));
        }

        if (!mo.cluster_for_bone.empty()) {
            Node skin("Deformer");
            skin.add_prop(Prop::L(mo.skin_id));
            skin.add_prop(Prop::S("Deformer::" + mo.name + "_skin"));
            skin.add_prop(Prop::S("Skin"));
            skin.add_child(Node("Version")).add_prop(Prop::I(101));
            skin.add_child(Node("Link_DeformAcuracy")).add_prop(Prop::D(50.0));
            objects.add_child(std::move(skin));

            for (auto& kv : mo.cluster_for_bone) {
                int filt = kv.first;
                int64_t cid = kv.second;
                const BoneOut& b = bones[filt];

                Node c("Deformer");
                c.add_prop(Prop::L(cid));
                c.add_prop(Prop::S("SubDeformer::Cluster_" + b.name));
                c.add_prop(Prop::S("Cluster"));
                c.add_child(Node("Version")).add_prop(Prop::I(100));
                c.add_child(Node("UserData")).add_prop(Prop::S("")).add_prop(Prop::S(""));

                std::vector<int32_t> idxs(mo.cluster_indexes.at(filt).begin(),
                                          mo.cluster_indexes.at(filt).end());
                std::vector<double> wts = mo.cluster_weights.at(filt);
                c.add_child(Node("Indexes")).add_prop(Prop::AI(std::move(idxs)));
                c.add_child(Node("Weights")).add_prop(Prop::AD(std::move(wts)));

                Mat4 link = b.world;
                Mat4 inv  = link.inverse_or_identity();
                c.add_child(Node("Transform")).add_prop(Prop::AD(inv.as_column_major()));
                c.add_child(Node("TransformLink")).add_prop(Prop::AD(link.as_column_major()));

                objects.add_child(std::move(c));
            }
        }
    }

    if (!bones.empty()) {
        Node pose("Pose");
        pose.add_prop(Prop::L(ids.make()));
        pose.add_prop(Prop::S("Pose::Bind"));
        pose.add_prop(Prop::S("BindPose"));
        pose.add_child(Node("Type")).add_prop(Prop::S("BindPose"));
        pose.add_child(Node("Version")).add_prop(Prop::I(100));
        pose.add_child(Node("NbPoseNodes")).add_prop(Prop::I((int32_t)bones.size()));
        for (const auto& b : bones) {
            Node pn("PoseNode");
            pn.add_child(Node("Node")).add_prop(Prop::L(b.model_id));
            pn.add_child(Node("Matrix")).add_prop(Prop::AD(b.world.as_column_major()));
            pose.add_child(std::move(pn));
        }
        objects.add_child(std::move(pose));
    }

    root.add_child(std::move(objects));

    Node conns("Connections");

    auto add_oo = [&](int64_t src, int64_t dst) {
        Node c("C");
        c.add_prop(Prop::S("OO"));
        c.add_prop(Prop::L(src));
        c.add_prop(Prop::L(dst));
        conns.add_child(std::move(c));
    };
    auto add_op = [&](int64_t src, int64_t dst, const std::string& prop) {
        Node c("C");
        c.add_prop(Prop::S("OP"));
        c.add_prop(Prop::L(src));
        c.add_prop(Prop::L(dst));
        c.add_prop(Prop::S(prop));
        conns.add_child(std::move(c));
    };

    for (const auto& b : bones) {
        if (b.parent_filt < 0) add_oo(b.model_id, 0);
        else                   add_oo(b.model_id, bones[b.parent_filt].model_id);
        add_oo(b.attr_id, b.model_id);
    }

    for (const auto& mo : meshes_out) {
        add_oo(mo.geom_id, mo.model_id);
        add_oo(mo.model_id, 0);
        add_oo(mo.mat_id, mo.model_id);

        for (int ch = 0; ch < CH_COUNT; ++ch) {
            const EmbeddedTex& et = mo.tex[ch];
            if (et.video_id == 0) continue;
            add_oo(et.video_id, et.tex_id);
            add_op(et.tex_id,   mo.mat_id, et.fbx_property);
        }
        if (!mo.cluster_for_bone.empty()) {
            add_oo(mo.skin_id, mo.geom_id);
            for (auto& kv : mo.cluster_for_bone) {
                int filt = kv.first;
                int64_t cid = kv.second;
                add_oo(cid, mo.skin_id);
                add_oo(bones[filt].model_id, cid);
            }
        }
    }

    root.add_child(std::move(conns));
    root.add_child(Node("Takes"));

    Buf out;

    static const uint8_t kMagic[] = {
        'K','a','y','d','a','r','a',' ','F','B','X',' ','B','i','n','a',
        'r','y',' ',' ', 0x00, 0x1A, 0x00
    };
    out.put_bytes(kMagic, sizeof(kMagic));
    out.put_u32(7400);

    for (const auto& c : root.children) write_node(out, c);

    for (int i = 0; i < 13; ++i) out.put_u8(0);

    for (int i = 0; i < 16; ++i) out.put_u8(0);
    while (out.data.size() & 0xF) out.put_u8(0);
    out.put_u32(0);
    out.put_u32(7400);
    for (int i = 0; i < 120; ++i) out.put_u8(0);
    static const uint8_t kFooterMagic[] = {
        0xF8, 0x5A, 0x8C, 0x6A, 0xDE, 0xF5, 0xD9, 0x7E,
        0xEC, 0xE9, 0x0C, 0xE3, 0x75, 0x8F, 0x29, 0x0B
    };
    out.put_bytes(kFooterMagic, 16);

    std::ofstream f(fbx_path, std::ios::binary | std::ios::trunc);
    if (!f) {
        err_msg = "cannot open " + fbx_path + " for writing";
        return false;
    }
    f.write((const char*)out.data.data(), (std::streamsize)out.data.size());
    if (!f.good()) {
        err_msg = "write failed for " + fbx_path;
        return false;
    }
    return true;
}
