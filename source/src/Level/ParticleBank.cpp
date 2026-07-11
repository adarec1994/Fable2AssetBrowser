#include "ParticleBank.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace Fx {

namespace {

constexpr uint32_t kMarkVer   = 0x4AD78C59u;  // timeline version sentinel
constexpr uint32_t kBaad      = 0xBAADF00Du;  // struct version sentinel
constexpr uint32_t kChildMark = 0x37E185B9u;  // optional sec3 child marker
constexpr size_t   kSec4Fixed = 0x1C917E;     // retail sec4 offset (fallback)

// flt_8331D8D0: default scalar values used by PSE_EvalGenericParam when a
// generic-param timeline is absent. Indices 0..27 are referenced by LoadBin.
constexpr float kGenericDefaults[] = {
    0.95f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
    0.0f,  0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 15.0f,
    1.0f, 64.0f, 50.0f, 0.05f, 0.05f, 0.1f, 0.2f, 0.1f,
    0.785398185f, 0.0f, 0.0f, 0.0f
};

struct Reader {
    const uint8_t* p;
    size_t n;
    size_t o = 0;
    bool bad = false;

    bool have(size_t k) const { return o + k <= n; }
    uint32_t u32() {
        if (!have(4)) { bad = true; return 0; }
        uint32_t v = (uint32_t(p[o]) << 24) | (uint32_t(p[o + 1]) << 16) |
                     (uint32_t(p[o + 2]) << 8) | uint32_t(p[o + 3]);
        o += 4; return v;
    }
    float f32() {
        uint32_t v = u32(); float f; std::memcpy(&f, &v, 4); return f;
    }
    uint8_t u8() {
        if (!have(1)) { bad = true; return 0; }
        return p[o++];
    }
    void skip(size_t k) { o += k; if (o > n) { o = n; bad = true; } }
};

uint32_t peek_u32(const Reader& r, size_t at) {
    if (at + 4 > r.n) return 0;
    return (uint32_t(r.p[at]) << 24) | (uint32_t(r.p[at + 1]) << 16) |
           (uint32_t(r.p[at + 2]) << 8) | uint32_t(r.p[at + 3]);
}

void opt_baad(Reader& r) {
    if (peek_u32(r, r.o) == kBaad) { r.skip(4); r.u32(); }
}

float generic_default(int idx) {
    return (idx >= 0 && idx < int(sizeof(kGenericDefaults) / sizeof(kGenericDefaults[0])))
        ? kGenericDefaults[idx]
        : 0.0f;
}

// Scalar generic-param timeline: [0x4AD78C59 + u32 ver]?, u32 count,
// count x { f32 time, f32 value }. The default value mirrors flt_8331D8D0.
Timeline read_stl(Reader& r, int idx) {
    Timeline t;
    t.default_value = generic_default(idx);
    if (peek_u32(r, r.o) == kMarkVer) { r.skip(4); r.u32(); }
    uint32_t cnt = r.u32();
    if (cnt > 100000) { r.bad = true; return t; }
    t.keys.reserve(cnt);
    for (uint32_t i = 0; i < cnt && !r.bad; ++i) {
        float time = r.f32();
        float val  = r.f32();
        t.keys.emplace_back(time, val);
    }
    return t;
}

void skip_stl(Reader& r, int idx) {
    (void)read_stl(r, idx);
}

// Vector timeline: count x { f32 time, vec3 } = 16 bytes/key. Values unused,
// only exact byte consumption.
void skip_vtl(Reader& r) {
    if (peek_u32(r, r.o) == kMarkVer) { r.skip(4); r.u32(); }
    uint32_t cnt = r.u32();
    if (cnt > 100000) { r.bad = true; return; }
    r.skip(size_t(cnt) * 16);
}

// PSE_EntityType_LoadBin base (45 bytes when present): u32 flag, vec3, 4 f32,
// u8 useLife, u32 lifeFrames, f32 procRadius, u32 startTimeInFrames.
void read_base(Reader& r, Emitter& e) {
    r.u32();                                        // flag
    e.pos = { r.f32(), r.f32(), r.f32() };          // sub_82A1BEA8 vec3
    e.quat = { r.f32(), r.f32(), r.f32(), r.f32() };
    r.u8();                                         // useLife
    r.u32();                                        // lifetime frames
    r.f32();                                        // process radius
    uint32_t start = r.u32();                       // StartTimeInFrames
    // Huge values are sentinels/garbage in the bank; treat as "start now".
    e.start_time_secs = (start < 30 * 3600) ? float(start) / 30.0f : 0.0f;
}

// PSE_EmitterType_LoadBin non-base + base.
void read_emitter_tail(Reader& r, Emitter& e) {
    r.u8();                          // positional interactable
    e.rate        = read_stl(r, 2);
    e.rate_spread = read_stl(r, 3);
    e.max_active  = static_cast<int32_t>(r.u32());
    e.initial_num = r.f32();
    e.fades = { r.f32(), r.f32(), r.f32(), r.f32() };
    r.u8(); r.u8(); r.u8();          // attach, sort, forceFullSize
    read_base(r, e);
}

void read_standard(Reader& r, Emitter& e) {
    opt_baad(r);
    if (e.sub_type == 1 || e.sub_type == 2) { r.f32(); r.u8(); r.u8(); }
    else if (e.sub_type == 3) { r.u8(); }
    e.life        = read_stl(r, 4);
    e.life_spread = read_stl(r, 5);
    skip_vtl(r);                     // vec 0
    e.acc_x = read_stl(r, 6);        // force/wind scalar x
    e.acc_y = read_stl(r, 7);        // force/wind scalar y
    e.acc_z = read_stl(r, 8);        // force/wind scalar z
    e.vel_x = read_stl(r, 9);
    e.vel_y = read_stl(r, 10);
    e.vel_z = read_stl(r, 11);
    skip_vtl(r);                     // vec 1
    e.size = read_stl(r, 12);
    skip_stl(r, 13);
    skip_vtl(r);                     // vec 2
    skip_stl(r, 14);
    r.u8();                          // releaseTrail
    r.u8(); r.f32();                 // ver>=3: globalCollider + restitution
    read_emitter_tail(r, e);
    e.has_motion = true;
}

void read_light(Reader& r, Emitter& e) {         // type 7
    r.u8(); r.u32();
    skip_vtl(r);
    for (int idx : { 15, 16, 17, 18, 19, 20, 21, 22, 23, 0, 1 })
        skip_stl(r, idx);
    read_base(r, e);
}

void read_light8(Reader& r, Emitter& e) {        // type 8
    r.u8(); r.u32(); r.u32(); r.u8();
    skip_vtl(r);
    for (int idx : { 15, 24, 16, 17, 18, 19, 20, 21, 22, 23, 0, 1 })
        skip_stl(r, idx);
    read_base(r, e);
}

void read_spline_object(Reader& r) {
    r.u32();                          // version
    uint32_t np = r.u32();
    if (np > 4096) { r.bad = true; return; }
    r.skip(size_t(np) * 12);          // np x vec3
    r.u8(); r.u8(); r.f32(); r.skip(4);
}

void read_spline_entity(Reader& r, Emitter& e) { // type 5
    r.u32(); r.u8();                  // refId, flag
    read_spline_object(r);
    skip_stl(r, 0); skip_stl(r, 1);
    read_base(r, e);
}

void read_spline_emitter(Reader& r, Emitter& e) { // type 9
    read_emitter_tail(r, e);
    r.u32(); r.u32(); r.u8(); r.u8(); r.f32(); r.skip(4);  // CSplineObject (18B)
    r.f32(); r.f32(); r.f32(); r.f32(); r.f32();
    r.u8(); r.u8();
    e.has_motion = true;
}

void read_type3(Reader& r, Emitter& e) {
    r.u8(); skip_stl(r, 25); r.f32(); skip_stl(r, 0); skip_stl(r, 1);
    read_base(r, e);
}
void read_attractor(Reader& r, Emitter& e) {     // type 4
    r.u8(); skip_stl(r, 26); skip_stl(r, 27); r.u8();
    skip_stl(r, 0); skip_stl(r, 1);
    read_base(r, e);
}

bool read_entity(Reader& r, Emitter& e) {
    e.type_id = r.u32();
    e.entity_id = r.u32();
    e.sub_type = r.u32();
    switch (e.type_id) {
        case 0: read_standard(r, e); break;
        case 3: read_type3(r, e); break;
        case 4: read_attractor(r, e); break;
        case 5: read_spline_entity(r, e); break;
        case 6: read_base(r, e); break;
        case 7: read_light(r, e); break;
        case 8: read_light8(r, e); break;
        case 9: read_spline_emitter(r, e); break;
        default: return false;
    }
    return !r.bad;
}

// ---- section 2/3 property-bag node walker (validated on all 5073 + 6221
// inline nodes). Component = [u32 tag][fixed-size value]; tags ascending.
// Only material fields the renderer needs are captured; the rest is skipped
// byte-exact.
bool walk_node(Reader& r, Visual* out_vis, bool* out_textured) {
    if (out_textured) *out_textured = false;
    uint32_t nid = r.u32();
    r.u32();                          // hdr
    uint32_t cnt = r.u32();
    if (r.bad || cnt > 64) { r.bad = true; return false; }
    bool has_mesh = false;
    for (uint32_t i = 0; i < cnt && !r.bad; ++i) {
        uint32_t tag = r.u32();
        switch (tag) {
            case 0x1: break;
            case 0x2: {                       // CMaterialParamAlpha
                float av = r.f32();
                if (out_vis) {
                    out_vis->alpha = av;
                    out_vis->has_alpha = true;
                }
                break;
            }
            case 0x400: case 0x8000: r.skip(4); break;
            case 0x4: {
                float cr = r.f32(), cg = r.f32(), cb = r.f32();
                if (out_vis) {
                    out_vis->color = { cr, cg, cb };
                    out_vis->has_color = true;
                }
                break;
            }
            case 0x8: {                       // texture material component
                r.u32();                      // path hash
                uint32_t ln = r.u32();
                if (r.bad || ln > 512 || !r.have(ln + 1)) { r.bad = true; break; }
                std::string path(reinterpret_cast<const char*>(r.p + r.o), ln);
                r.skip(ln + 1);
                uint32_t nx = r.u32();
                uint32_t ny = r.u32();
                uint8_t rif = r.u8();
                float spd = r.f32();
                uint8_t disp = r.u8();
                r.u8();                       // saturation mask
                r.f32();                      // soft particle depth contrast
                if (out_vis) {
                    out_vis->texture = std::move(path);
                    out_vis->tex_cols = (nx >= 1 && nx <= 16) ? int(nx) : 1;
                    out_vis->tex_rows = (ny >= 1 && ny <= 16) ? int(ny) : 1;
                    out_vis->random_frame = rif != 0;
                    out_vis->frame_speed = spd;
                    out_vis->displacement = disp != 0;
                }
                if (out_textured) *out_textured = true;
                break;
            }
            case 0x10: {
                float sx = r.f32(), sy = r.f32();
                if (out_vis) {
                    out_vis->size_x = sx; out_vis->size_y = sy;
                    out_vis->has_size = true;
                }
                break;
            }
            case 0x10000: r.skip(8); break;
            case 0x20: case 0x800: r.skip(4); break;
            case 0x200: {                     // CMaterialParamRotationAngle
                float speed = r.f32();
                float spread = r.f32();
                uint8_t init_rand = r.u8();
                if (out_vis) {
                    out_vis->has_rotation = true;
                    out_vis->rot_speed = speed;
                    out_vis->rot_spread = spread;
                    out_vis->rot_initial_random = init_rand != 0;
                }
                break;
            }
            case 0x20000: {                   // CMaterialParamAlignmentQuat
                float qx = r.f32(), qy = r.f32(), qz = r.f32(),
                      qw = r.f32();
                if (out_vis) {
                    out_vis->has_align_quat = true;
                    out_vis->align_quat[0] = qx;
                    out_vis->align_quat[1] = qy;
                    out_vis->align_quat[2] = qz;
                    out_vis->align_quat[3] = qw;
                }
                break;
            }
            case 0x40: {                      // CMaterialParamSrcBlendMode
                uint32_t b = r.u32();
                if (out_vis) out_vis->src_blend = int(b);
                break;
            }
            case 0x80: {                      // CMaterialParamDestBlendMode
                uint32_t b = r.u32();
                if (out_vis) out_vis->dst_blend = int(b);
                break;
            }
            case 0x100: {                     // CMaterialParamBlendOp
                uint32_t b = r.u32();
                if (out_vis) out_vis->blend_op = int(b);
                break;
            }
            case 0x1000: {                    // mesh component
                r.u32();
                uint32_t ln = r.u32();
                if (r.bad || ln > 512) { r.bad = true; break; }
                r.skip(ln + 1 + 1);
                has_mesh = true;
                break;
            }
            case 0x2000: r.skip(has_mesh ? 12 : 21); break;
            case 0x4000: r.skip(22); break;
            default: r.bad = true; break;
        }
    }
    uint32_t nid2 = r.u32();
    if (r.bad || nid2 != nid) { r.bad = true; return false; }
    return true;
}

// Collect every pfx texture path in the bank (category fallback matching).
void scan_textures(const uint8_t* p, size_t n, std::vector<std::string>& out) {
    static const char kNeedle[] = "art\\fx\\pfx_textures\\";
    const size_t need = sizeof(kNeedle) - 1;
    for (size_t i = 0; i + need < n; ++i) {
        if (std::memcmp(p + i, kNeedle, need) != 0) continue;
        size_t j = i;
        while (j < n && p[j] != 0 && p[j] >= 0x20 && p[j] < 0x7f) ++j;
        std::string s(reinterpret_cast<const char*>(p + i), j - i);
        if (s.size() > need && std::find(out.begin(), out.end(), s) == out.end())
            out.push_back(std::move(s));
        i = j;
    }
}

}  // namespace

uint32_t Fnv1(const std::string& s) {
    uint32_t h = 0x811C9DC5u;
    for (unsigned char c : s) { h = uint32_t(h * 0x01000193u); h ^= c; }
    return h;
}

uint32_t Fnv1Lower(const std::string& s) {
    uint32_t h = 0x811C9DC5u;
    for (unsigned char c : s) {
        h = uint32_t(h * 0x01000193u);
        h ^= uint8_t(std::tolower(c));
    }
    return h;
}

const Effect* Bank::find_by_hash(uint32_t h) const {
    for (const auto& e : effects) if (e.name_hash == h) return &e;
    return nullptr;
}
const Effect* Bank::find_by_name(const std::string& name) const {
    return find_by_hash(Fnv1Lower(name));
}

bool ParseParticleBank(const std::vector<uint8_t>& bytes, Bank& out) {
    out = Bank{};
    Reader r{ bytes.data(), bytes.size() };

    static const char kMagic[] = "ParticleBankFile";
    if (r.n < 0x2C || std::memcmp(r.p, kMagic, 16) != 0) {
        out.error = "bad magic"; return false;
    }
    r.o = 16;
    if (peek_u32(r, r.o) == kBaad) { r.skip(4); }
    out.version = r.u32();

    scan_textures(r.p, r.n, out.textures);

    // ---- section 2: leaf nodes (walked for exact offsets only) ----
    r.o = 0x20;
    r.u32();                        // param (=1)
    uint32_t c2 = r.u32();
    bool sections_ok = (c2 > 0 && c2 < 200000);
    for (uint32_t i = 0; sections_ok && i < c2; ++i) {
        if (!walk_node(r, nullptr, nullptr)) { sections_ok = false; break; }
    }

    // ---- section 3: appearance systems ----
    if (sections_ok) {
        r.u32();                    // param (=1)
        uint32_t c3 = r.u32();
        if (c3 == 0 || c3 > 200000) sections_ok = false;
        for (uint32_t i = 0; sections_ok && i < c3; ++i) {
            SystemDef sys;
            sys.id = r.u32();
            r.u32();                // flag
            uint32_t cc = r.u32();
            if (r.bad || cc > 32) { sections_ok = false; break; }
            for (uint32_t j = 0; j < cc; ++j) {
                if (peek_u32(r, r.o) == kChildMark) { r.skip(8); }
                float delay = r.f32();
                Visual vis;
                vis.delay = delay;
                bool textured = false;
                if (!walk_node(r, &vis, &textured)) { sections_ok = false; break; }
                if (textured) sys.visuals.push_back(std::move(vis));
            }
            if (!sections_ok) break;
            r.skip(12);             // transform (3 floats)
            uint32_t id2 = r.u32();
            if (r.bad || id2 != sys.id) { sections_ok = false; break; }
            out.systems.push_back(std::move(sys));
        }
    }

    // ---- section 4: the emitter/motion entities ----
    size_t sec4_at = sections_ok ? r.o : kSec4Fixed;
    if (!sections_ok) {
        out.systems.clear();
        out.error = "sec2/3 walk failed; effects unavailable";
    }
    if (sec4_at + 8 > r.n) { out.error = "no section 4"; return false; }
    r.o = sec4_at;
    r.bad = false;
    r.u32();                        // flag (=1)
    uint32_t count = r.u32();
    if (count == 0 || count > 200000) { out.error = "bad entity count"; return false; }

    out.emitters.reserve(count);
    for (uint32_t k = 0; k < count; ++k) {
        Emitter e;
        size_t start = r.o;
        if (!read_entity(r, e) || r.o <= start) {
            out.error = "entity parse stopped at #" + std::to_string(k);
            break;
        }
        out.emitters.push_back(std::move(e));
    }

    // ---- trailing effect map ----
    // record = [u32 key][u32][u32][u8 n][n x item][u32 key]
    // item   = [u32 group][u32 m][m x (u32 sec4_eid, u32 sys_id, u32 flags)]
    if (sections_ok && out.emitters.size() == count && r.o + 8 <= r.n) {
        std::unordered_map<uint32_t, int> eid_to_idx;
        for (size_t i = 0; i < out.emitters.size(); ++i)
            eid_to_idx[out.emitters[i].entity_id] = int(i);
        std::unordered_map<uint32_t, int> sys_to_idx;
        for (size_t i = 0; i < out.systems.size(); ++i)
            sys_to_idx[out.systems[i].id] = int(i);

        r.u32();                    // 3
        r.u32();                    // record count hint
        while (r.o + 17 <= r.n && !r.bad) {
            size_t rec_start = r.o;
            uint32_t key = r.u32();
            r.u32(); r.u32();
            uint8_t cnt = r.u8();
            if (cnt > 64) { r.o = rec_start; break; }
            Effect eff;
            eff.name_hash = key;
            bool rec_ok = true;
            for (uint8_t i = 0; i < cnt && rec_ok; ++i) {
                uint32_t group = r.u32();
                uint32_t m = r.u32();
                if (r.bad || m > 16) { rec_ok = false; break; }
                for (uint32_t j = 0; j < m; ++j) {
                    uint32_t eid = r.u32();
                    uint32_t sid = r.u32();
                    r.u32();        // flags (usually 0)
                    EffectPart part;
                    part.group = group;
                    auto ei = eid_to_idx.find(eid);
                    part.emitter = ei != eid_to_idx.end() ? ei->second : -1;
                    auto si = sys_to_idx.find(sid);
                    part.system = si != sys_to_idx.end() ? si->second : -1;
                    if (part.emitter >= 0) eff.parts.push_back(part);
                }
            }
            uint32_t key2 = r.u32();
            if (!rec_ok || r.bad || key2 != key) { r.o = rec_start; break; }
            out.effects.push_back(std::move(eff));
        }
    }

    out.ok = !out.emitters.empty();
    if (!out.ok && out.error.empty()) out.error = "no emitters parsed";
    return out.ok;
}

}  // namespace Fx
