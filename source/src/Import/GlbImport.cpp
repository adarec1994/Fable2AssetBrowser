#include "GlbImport.h"

#include "crude_json.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

using crude_json::value;

namespace GlbImport {

namespace {

inline uint32_t rd32le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

double num(const value& v, const char* key, double def) {
    return (v.contains(key) && v[key].is_number())
               ? v[key].get<crude_json::number>() : def;
}
int inum(const value& v, const char* key, int def) {
    return (int)num(v, key, (double)def);
}
std::string str(const value& v, const char* key) {
    return (v.contains(key) && v[key].is_string())
               ? v[key].get<crude_json::string>() : std::string();
}
bool boolean(const value& v, const char* key, bool def) {
    return (v.contains(key) && v[key].is_boolean())
               ? v[key].get<crude_json::boolean>() : def;
}

bool base64_decode(const std::string& in, std::vector<uint8_t>& out) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    out.clear();
    out.reserve(in.size() * 3 / 4);
    uint32_t buf = 0; int bits = 0;
    for (char c : in) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        int v = val(c);
        if (v < 0) return false;
        buf = (buf << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((uint8_t)(buf >> bits));
        }
    }
    return true;
}

struct Mat4 {
    
    float m[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};

    static Mat4 identity() { return Mat4{}; }

    static Mat4 trs(const float t[3], const float q[4], const float s[3]) {
        Mat4 r;
        const float x = q[0], y = q[1], z = q[2], w = q[3];
        const float x2 = x + x, y2 = y + y, z2 = z + z;
        const float xx = x * x2, yy = y * y2, zz = z * z2;
        const float xy = x * y2, xz = x * z2, yz = y * z2;
        const float wx = w * x2, wy = w * y2, wz = w * z2;
        r.m[0] = (1 - (yy + zz)) * s[0];
        r.m[1] = (xy + wz) * s[0];
        r.m[2] = (xz - wy) * s[0];
        r.m[4] = (xy - wz) * s[1];
        r.m[5] = (1 - (xx + zz)) * s[1];
        r.m[6] = (yz + wx) * s[1];
        r.m[8] = (xz + wy) * s[2];
        r.m[9] = (yz - wx) * s[2];
        r.m[10] = (1 - (xx + yy)) * s[2];
        r.m[12] = t[0]; r.m[13] = t[1]; r.m[14] = t[2];
        return r;
    }

    Mat4 operator*(const Mat4& o) const {
        Mat4 r;
        for (int c = 0; c < 4; ++c)
            for (int rw = 0; rw < 4; ++rw)
                r.m[c * 4 + rw] = m[0 * 4 + rw] * o.m[c * 4 + 0] +
                                  m[1 * 4 + rw] * o.m[c * 4 + 1] +
                                  m[2 * 4 + rw] * o.m[c * 4 + 2] +
                                  m[3 * 4 + rw] * o.m[c * 4 + 3];
        return r;
    }

    void xform_point(const float p[3], float out[3]) const {
        out[0] = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12];
        out[1] = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13];
        out[2] = m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14];
    }
    void xform_dir(const float p[3], float out[3]) const {
        out[0] = m[0] * p[0] + m[4] * p[1] + m[8] * p[2];
        out[1] = m[1] * p[0] + m[5] * p[1] + m[9] * p[2];
        out[2] = m[2] * p[0] + m[6] * p[1] + m[10] * p[2];
    }
};

struct Loader {
    const value* root = nullptr;
    std::vector<uint8_t> bin;               
    std::vector<std::vector<uint8_t>> buffers;
    std::filesystem::path base_dir;
    Scene* out = nullptr;
    std::string* err = nullptr;

    const value* arr_item(const char* arr_name, int idx) const {
        if (idx < 0 || !root->contains(arr_name)) return nullptr;
        const value& a = (*root)[arr_name];
        if (!a.is_array()) return nullptr;
        const auto& av = a.get<crude_json::array>();
        if ((size_t)idx >= av.size()) return nullptr;
        return &av[idx];
    }

    bool fail(const std::string& m) const { *err = m; return false; }

    bool load_buffers() {
        buffers.clear();
        if (!root->contains("buffers")) return true;
        const auto& av = (*root)["buffers"].get<crude_json::array>();
        for (size_t i = 0; i < av.size(); ++i) {
            const value& b = av[i];
            std::string uri = str(b, "uri");
            if (uri.empty()) {
                buffers.push_back(bin);    
                continue;
            }
            if (uri.rfind("data:", 0) == 0) {
                size_t comma = uri.find(',');
                if (comma == std::string::npos)
                    return fail("bad data: URI in buffer");
                std::vector<uint8_t> bytes;
                if (!base64_decode(uri.substr(comma + 1), bytes))
                    return fail("bad base64 in buffer URI");
                buffers.push_back(std::move(bytes));
                continue;
            }
            std::ifstream f(base_dir / uri, std::ios::binary);
            if (!f) return fail("external buffer not found: " + uri);
            buffers.emplace_back((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
        }
        return true;
    }

    
    bool accessor_view(const value& acc, const uint8_t*& data,
                       size_t& stride, size_t& count, int& comp_type,
                       int& ncomp, bool& normalized) const {
        static const struct { const char* t; int n; } kTypes[] = {
            {"SCALAR", 1}, {"VEC2", 2}, {"VEC3", 3}, {"VEC4", 4},
            {"MAT4", 16}, {"MAT3", 9}, {"MAT2", 4},
        };
        comp_type = inum(acc, "componentType", 0);
        count = (size_t)inum(acc, "count", 0);
        normalized = boolean(acc, "normalized", false);
        std::string t = str(acc, "type");
        ncomp = 0;
        for (const auto& k : kTypes) if (t == k.t) { ncomp = k.n; break; }
        if (!ncomp || !count) return false;

        int csz = 4;
        switch (comp_type) {
            case 5120: case 5121: csz = 1; break;
            case 5122: case 5123: csz = 2; break;
            case 5125: case 5126: csz = 4; break;
            default: return false;
        }
        const size_t elem = (size_t)csz * ncomp;

        data = nullptr; stride = elem;
        int bv_idx = inum(acc, "bufferView", -1);
        if (bv_idx >= 0) {
            const value* bv = arr_item("bufferViews", bv_idx);
            if (!bv) return false;
            int buf_idx = inum(*bv, "buffer", 0);
            if (buf_idx < 0 || (size_t)buf_idx >= buffers.size()) return false;
            const auto& buf = buffers[buf_idx];
            size_t bv_off = (size_t)num(*bv, "byteOffset", 0);
            size_t bv_len = (size_t)num(*bv, "byteLength", 0);
            size_t bs = (size_t)num(*bv, "byteStride", 0);
            size_t acc_off = (size_t)num(acc, "byteOffset", 0);
            stride = bs ? bs : elem;
            if (bv_off + bv_len > buf.size()) return false;
            if (acc_off + (count - 1) * stride + elem > bv_len) return false;
            data = buf.data() + bv_off + acc_off;
        }
        return true;
    }

    
    bool read_floats(int acc_idx, int want_comp, std::vector<float>& out_f) const {
        const value* acc = arr_item("accessors", acc_idx);
        if (!acc) return false;
        const uint8_t* data = nullptr;
        size_t stride = 0, count = 0;
        int ct = 0, nc = 0;
        bool norm = false;
        if (!accessor_view(*acc, data, stride, count, ct, nc, norm)) return false;
        if (nc < want_comp) return false;

        out_f.assign(count * want_comp, 0.0f);
        auto decode_at = [&](const uint8_t* base, size_t elem_idx, size_t st) {
            const uint8_t* p = base + elem_idx * st;
            for (int c = 0; c < want_comp; ++c) {
                float v = 0.0f;
                switch (ct) {
                    case 5126: { float f; std::memcpy(&f, p + c * 4, 4); v = f; break; }
                    case 5123: { uint16_t u; std::memcpy(&u, p + c * 2, 2);
                                 v = norm ? u / 65535.0f : (float)u; break; }
                    case 5121: { uint8_t u = p[c];
                                 v = norm ? u / 255.0f : (float)u; break; }
                    case 5122: { int16_t s; std::memcpy(&s, p + c * 2, 2);
                                 v = norm ? std::max(s / 32767.0f, -1.0f) : (float)s; break; }
                    case 5120: { int8_t s = (int8_t)p[c];
                                 v = norm ? std::max(s / 127.0f, -1.0f) : (float)s; break; }
                    case 5125: { uint32_t u; std::memcpy(&u, p + c * 4, 4); v = (float)u; break; }
                }
                out_f[elem_idx * want_comp + c] = v;
            }
        };
        if (data) {
            for (size_t i = 0; i < count; ++i) decode_at(data, i, stride);
        }

        
        if (acc->contains("sparse")) {
            const value& sp = (*acc)["sparse"];
            int scount = inum(sp, "count", 0);
            if (scount > 0 && sp.contains("indices") && sp.contains("values")) {
                const value& si = sp["indices"];
                const value& sv = sp["values"];
                value fake_i(crude_json::type_t::object);
                fake_i["bufferView"] = si.contains("bufferView") ? si["bufferView"] : value();
                fake_i["byteOffset"] = si.contains("byteOffset") ? si["byteOffset"] : value(0.0);
                fake_i["componentType"] = si.contains("componentType") ? si["componentType"] : value(5125.0);
                fake_i["count"] = value((double)scount);
                fake_i["type"] = value(std::string("SCALAR"));
                const uint8_t* idata = nullptr; size_t istride = 0, icount = 0;
                int ict = 0, inc = 0; bool inorm = false;
                if (!accessor_view(fake_i, idata, istride, icount, ict, inc, inorm) || !idata)
                    return false;

                value fake_v(crude_json::type_t::object);
                fake_v["bufferView"] = sv.contains("bufferView") ? sv["bufferView"] : value();
                fake_v["byteOffset"] = sv.contains("byteOffset") ? sv["byteOffset"] : value(0.0);
                fake_v["componentType"] = (*acc)["componentType"];
                fake_v["count"] = value((double)scount);
                fake_v["type"] = (*acc)["type"];
                if (acc->contains("normalized")) fake_v["normalized"] = (*acc)["normalized"];
                const uint8_t* vdata = nullptr; size_t vstride = 0, vcount = 0;
                int vct = 0, vnc = 0; bool vnorm = false;
                if (!accessor_view(fake_v, vdata, vstride, vcount, vct, vnc, vnorm) || !vdata)
                    return false;

                for (size_t k = 0; k < (size_t)scount; ++k) {
                    size_t tgt = 0;
                    const uint8_t* ip = idata + k * istride;
                    switch (ict) {
                        case 5121: tgt = ip[0]; break;
                        case 5123: { uint16_t u; std::memcpy(&u, ip, 2); tgt = u; break; }
                        default:   { uint32_t u; std::memcpy(&u, ip, 4); tgt = u; break; }
                    }
                    if (tgt >= count) continue;
                    const uint8_t* vp = vdata + k * vstride;
                    for (int c = 0; c < want_comp; ++c) {
                        float v = 0.0f;
                        switch (vct) {
                            case 5126: { float f; std::memcpy(&f, vp + c * 4, 4); v = f; break; }
                            case 5123: { uint16_t u; std::memcpy(&u, vp + c * 2, 2);
                                         v = vnorm ? u / 65535.0f : (float)u; break; }
                            case 5121: { uint8_t u = vp[c];
                                         v = vnorm ? u / 255.0f : (float)u; break; }
                            case 5122: { int16_t s; std::memcpy(&s, vp + c * 2, 2);
                                         v = vnorm ? std::max(s / 32767.0f, -1.0f) : (float)s; break; }
                            case 5120: { int8_t s = (int8_t)vp[c];
                                         v = vnorm ? std::max(s / 127.0f, -1.0f) : (float)s; break; }
                        }
                        out_f[tgt * want_comp + c] = v;
                    }
                }
            }
        }
        return true;
    }

    bool read_indices(int acc_idx, std::vector<uint32_t>& out_i) const {
        const value* acc = arr_item("accessors", acc_idx);
        if (!acc) return false;
        const uint8_t* data = nullptr;
        size_t stride = 0, count = 0;
        int ct = 0, nc = 0;
        bool norm = false;
        if (!accessor_view(*acc, data, stride, count, ct, nc, norm) || !data)
            return false;
        out_i.resize(count);
        for (size_t i = 0; i < count; ++i) {
            const uint8_t* p = data + i * stride;
            switch (ct) {
                case 5121: out_i[i] = p[0]; break;
                case 5123: { uint16_t u; std::memcpy(&u, p, 2); out_i[i] = u; break; }
                case 5125: { uint32_t u; std::memcpy(&u, p, 4); out_i[i] = u; break; }
                default: return false;
            }
        }
        return true;
    }

    int image_for_texture(int tex_idx) const {
        const value* tex = arr_item("textures", tex_idx);
        if (!tex) return -1;
        int src = inum(*tex, "source", -1);
        if (src < 0 && tex->contains("extensions")) {
            
            const value& ex = (*tex)["extensions"];
            if (ex.is_object()) {
                const auto& obj = ex.get<crude_json::object>();
                for (const auto& kv : obj) {
                    if (kv.second.is_object() && kv.second.contains("source"))
                        src = inum(kv.second, "source", -1);
                }
            }
        }
        return src;
    }

    int tex_index_of(const value& mat_like, const char* key) const {
        if (!mat_like.contains(key) || !mat_like[key].is_object()) return -1;
        return image_for_texture(inum(mat_like[key], "index", -1));
    }

    bool load_images() {
        if (!root->contains("images")) return true;
        const auto& av = (*root)["images"].get<crude_json::array>();
        for (size_t i = 0; i < av.size(); ++i) {
            const value& im = av[i];
            Image img;
            img.name = str(im, "name");
            img.mime = str(im, "mimeType");
            std::string uri = str(im, "uri");
            if (!uri.empty()) {
                if (uri.rfind("data:", 0) == 0) {
                    size_t comma = uri.find(',');
                    if (comma != std::string::npos)
                        base64_decode(uri.substr(comma + 1), img.bytes);
                } else {
                    std::ifstream f(base_dir / uri, std::ios::binary);
                    if (f) img.bytes.assign(
                        (std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
                    if (img.name.empty())
                        img.name = std::filesystem::path(uri).stem().string();
                }
            } else {
                int bv_idx = inum(im, "bufferView", -1);
                const value* bv = arr_item("bufferViews", bv_idx);
                if (bv) {
                    int buf_idx = inum(*bv, "buffer", 0);
                    size_t off = (size_t)num(*bv, "byteOffset", 0);
                    size_t len = (size_t)num(*bv, "byteLength", 0);
                    if (buf_idx >= 0 && (size_t)buf_idx < buffers.size() &&
                        off + len <= buffers[buf_idx].size()) {
                        img.bytes.assign(buffers[buf_idx].begin() + off,
                                         buffers[buf_idx].begin() + off + len);
                    }
                }
            }
            if (img.name.empty()) img.name = "image_" + std::to_string(i);
            out->images.push_back(std::move(img));
        }
        return true;
    }

    void load_materials() {
        if (!root->contains("materials")) return;
        const auto& av = (*root)["materials"].get<crude_json::array>();
        for (size_t i = 0; i < av.size(); ++i) {
            const value& m = av[i];
            Material mat;
            mat.name = str(m, "name");
            if (mat.name.empty()) mat.name = "material_" + std::to_string(i);
            mat.double_sided = boolean(m, "doubleSided", false);
            mat.alpha_blend = str(m, "alphaMode") == "BLEND" ||
                              str(m, "alphaMode") == "MASK";
            if (m.contains("pbrMetallicRoughness") &&
                m["pbrMetallicRoughness"].is_object()) {
                const value& pbr = m["pbrMetallicRoughness"];
                mat.base_color = tex_index_of(pbr, "baseColorTexture");
                mat.metallic_rough = tex_index_of(pbr, "metallicRoughnessTexture");
                if (pbr.contains("baseColorFactor") &&
                    pbr["baseColorFactor"].is_array()) {
                    const auto& f = pbr["baseColorFactor"].get<crude_json::array>();
                    for (size_t k = 0; k < f.size() && k < 4; ++k)
                        if (f[k].is_number())
                            mat.base_color_factor[k] =
                                (float)f[k].get<crude_json::number>();
                }
            }
            mat.normal = tex_index_of(m, "normalTexture");
            mat.occlusion = tex_index_of(m, "occlusionTexture");
            mat.emissive = tex_index_of(m, "emissiveTexture");
            out->materials.push_back(std::move(mat));
        }
    }

    void node_local(const value& node, Mat4& local) const {
        if (node.contains("matrix") && node["matrix"].is_array()) {
            const auto& a = node["matrix"].get<crude_json::array>();
            for (size_t i = 0; i < 16 && i < a.size(); ++i)
                if (a[i].is_number())
                    local.m[i] = (float)a[i].get<crude_json::number>();
            return;
        }
        float t[3] = {0, 0, 0}, q[4] = {0, 0, 0, 1}, s[3] = {1, 1, 1};
        auto vec = [&](const char* key, float* dst, int n) {
            if (node.contains(key) && node[key].is_array()) {
                const auto& a = node[key].get<crude_json::array>();
                for (int i = 0; i < n && (size_t)i < a.size(); ++i)
                    if (a[i].is_number())
                        dst[i] = (float)a[i].get<crude_json::number>();
            }
        };
        vec("translation", t, 3);
        vec("rotation", q, 4);
        vec("scale", s, 3);
        local = Mat4::trs(t, q, s);
    }

    void emit_primitive(const value& prim, const Mat4& world,
                        const std::string& mesh_name) {
        if (!prim.contains("attributes") || !prim["attributes"].is_object())
            return;
        int mode = inum(prim, "mode", 4);
        if (mode != 4 && mode != 5) return;   

        const value& attrs = prim["attributes"];
        int pos_acc = inum(attrs, "POSITION", -1);
        if (pos_acc < 0) return;

        std::vector<float> pos, nrm, uv;
        if (!read_floats(pos_acc, 3, pos) || pos.empty()) return;
        const size_t vcount = pos.size() / 3;

        int nrm_acc = inum(attrs, "NORMAL", -1);
        bool have_normals = nrm_acc >= 0 && read_floats(nrm_acc, 3, nrm) &&
                            nrm.size() == pos.size();
        int uv_acc = inum(attrs, "TEXCOORD_0", -1);
        if (uv_acc < 0 || !read_floats(uv_acc, 2, uv) || uv.size() != vcount * 2)
            uv.assign(vcount * 2, 0.0f);

        std::vector<uint32_t> idx;
        int idx_acc = inum(prim, "indices", -1);
        if (idx_acc >= 0) {
            if (!read_indices(idx_acc, idx)) return;
        } else {
            idx.resize(vcount);
            for (size_t i = 0; i < vcount; ++i) idx[i] = (uint32_t)i;
        }
        if (mode == 5) {                        
            std::vector<uint32_t> list;
            for (size_t i = 2; i < idx.size(); ++i) {
                uint32_t a = idx[i - 2], b = idx[i - 1], c = idx[i];
                if (a == b || b == c || a == c) continue;
                if (i & 1) { list.push_back(b); list.push_back(a); list.push_back(c); }
                else       { list.push_back(a); list.push_back(b); list.push_back(c); }
            }
            idx.swap(list);
        }
        if (idx.size() < 3) return;

        Prim p;
        p.name = mesh_name;
        p.material = inum(prim, "material", -1);
        p.positions.resize(vcount * 3);
        p.normals.resize(vcount * 3);
        p.uvs = uv;

        
        
        for (size_t v = 0; v < vcount; ++v) {
            float in[3] = {pos[v*3+0], pos[v*3+1], pos[v*3+2]};
            float w[3];
            world.xform_point(in, w);
            p.positions[v*3+0] = w[0];
            p.positions[v*3+1] = -w[2];
            p.positions[v*3+2] = w[1];
        }
        if (have_normals) {
            for (size_t v = 0; v < vcount; ++v) {
                float in[3] = {nrm[v*3+0], nrm[v*3+1], nrm[v*3+2]};
                float w[3];
                world.xform_dir(in, w);
                float fx = w[0], fy = -w[2], fz = w[1];
                float l = std::sqrt(fx*fx + fy*fy + fz*fz);
                if (l < 1e-12f) { fx = 0; fy = 0; fz = 1; l = 1; }
                p.normals[v*3+0] = fx / l;
                p.normals[v*3+1] = fy / l;
                p.normals[v*3+2] = fz / l;
            }
        } else {
            
            std::fill(p.normals.begin(), p.normals.end(), 0.0f);
            for (size_t t = 0; t + 2 < idx.size(); t += 3) {
                uint32_t a = idx[t], b = idx[t+1], c = idx[t+2];
                if (a >= vcount || b >= vcount || c >= vcount) continue;
                const float* A = &p.positions[a*3];
                const float* B = &p.positions[b*3];
                const float* C = &p.positions[c*3];
                float u[3] = {B[0]-A[0], B[1]-A[1], B[2]-A[2]};
                float w[3] = {C[0]-A[0], C[1]-A[1], C[2]-A[2]};
                float n[3] = {u[1]*w[2]-u[2]*w[1], u[2]*w[0]-u[0]*w[2], u[0]*w[1]-u[1]*w[0]};
                for (uint32_t vi : {a, b, c}) {
                    p.normals[vi*3+0] += n[0];
                    p.normals[vi*3+1] += n[1];
                    p.normals[vi*3+2] += n[2];
                }
            }
            for (size_t v = 0; v < vcount; ++v) {
                float* n = &p.normals[v*3];
                float l = std::sqrt(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
                if (l < 1e-12f) { n[0] = 0; n[1] = 0; n[2] = 1; }
                else { n[0] /= l; n[1] /= l; n[2] /= l; }
            }
        }

        
        
        p.indices.resize(idx.size());
        for (size_t t = 0; t + 2 < idx.size(); t += 3) {
            p.indices[t + 0] = idx[t + 0];
            p.indices[t + 1] = idx[t + 2];
            p.indices[t + 2] = idx[t + 1];
        }

        bool in_range = true;
        for (uint32_t iv : p.indices)
            if (iv >= vcount) { in_range = false; break; }
        if (!in_range) return;

        out->prims.push_back(std::move(p));
    }

    void walk_node(int node_idx, const Mat4& parent, int depth) {
        if (depth > 64) return;
        const value* node = arr_item("nodes", node_idx);
        if (!node) return;
        Mat4 local;
        node_local(*node, local);
        Mat4 world = parent * local;

        int mesh_idx = inum(*node, "mesh", -1);
        if (mesh_idx >= 0) {
            const value* mesh = arr_item("meshes", mesh_idx);
            if (mesh && mesh->contains("primitives") &&
                (*mesh)["primitives"].is_array()) {
                std::string mesh_name = str(*mesh, "name");
                if (mesh_name.empty()) mesh_name = str(*node, "name");
                if (mesh_name.empty())
                    mesh_name = "mesh_" + std::to_string(mesh_idx);
                for (const value& prim :
                     (*mesh)["primitives"].get<crude_json::array>()) {
                    emit_primitive(prim, world, mesh_name);
                }
            }
        }
        if (node->contains("children") && (*node)["children"].is_array()) {
            for (const value& c : (*node)["children"].get<crude_json::array>()) {
                if (c.is_number())
                    walk_node((int)c.get<crude_json::number>(), world, depth + 1);
            }
        }
    }
};

}  

bool load_glb(const std::string& path, Scene& out, std::string& err)
{
    out = Scene{};

    std::ifstream f(path, std::ios::binary);
    if (!f) { err = "could not open " + path; return false; }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
    if (data.size() < 12 || rd32le(data.data()) != 0x46546C67u) {
        err = "not a .glb file (bad magic)";
        return false;
    }
    uint32_t version = rd32le(data.data() + 4);
    if (version != 2) { err = "unsupported glTF version"; return false; }

    std::string json_text;
    std::vector<uint8_t> bin;
    size_t off = 12;
    while (off + 8 <= data.size()) {
        uint32_t clen = rd32le(data.data() + off);
        uint32_t ctype = rd32le(data.data() + off + 4);
        off += 8;
        if (off + clen > data.size()) break;
        if (ctype == 0x4E4F534Au) {          
            json_text.assign((const char*)data.data() + off, clen);
        } else if (ctype == 0x004E4942u) {   
            bin.assign(data.begin() + off, data.begin() + off + clen);
        }
        off += clen;
    }
    if (json_text.empty()) { err = "GLB has no JSON chunk"; return false; }

    const value root = value::parse(json_text);
    if (!root.is_object()) { err = "could not parse glTF JSON"; return false; }

    Loader ld;
    ld.root = &root;
    ld.bin = std::move(bin);
    ld.base_dir = std::filesystem::path(path).parent_path();
    ld.out = &out;
    ld.err = &err;

    if (!ld.load_buffers()) return false;
    if (!ld.load_images()) return false;
    ld.load_materials();

    
    
    
    
    int scene_idx = inum(root, "scene", 0);
    const value* scene = ld.arr_item("scenes", scene_idx);
    if (!scene && root.contains("scenes") && root["scenes"].is_array() &&
        !root["scenes"].get<crude_json::array>().empty()) {
        scene = &root["scenes"].get<crude_json::array>()[0];
    }
    if (scene && scene->contains("nodes") && (*scene)["nodes"].is_array()) {
        for (const value& n : (*scene)["nodes"].get<crude_json::array>()) {
            if (n.is_number())
                ld.walk_node((int)n.get<crude_json::number>(),
                             Mat4::identity(), 0);
        }
    } else if (root.contains("nodes") && root["nodes"].is_array()) {
        const auto& nodes = root["nodes"].get<crude_json::array>();
        for (size_t i = 0; i < nodes.size(); ++i)
            ld.walk_node((int)i, Mat4::identity(), 0);
    }

    if (out.prims.empty()) {
        err = "no triangle geometry found in " +
              std::filesystem::path(path).filename().string();
        return false;
    }
    return true;
}

}
