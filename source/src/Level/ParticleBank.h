#pragma once

// Fable 2 particle system (FX) — parsed byte-exact from
// Data\Art\Particles\particle_bank.bnk (the custom ParticleBankFile blob).
//
// The bank has four sections plus a trailing effect map (all validated
// byte-exact against the retail file):
//   header  : "ParticleBankFile", u32 ver(=2), BAADF00D, flag, global factor
//   sec2    : 5073 property-bag leaf nodes (materials etc.)
//   sec3    : 2231 appearance "systems": id + textured children (material
//             nodes carrying texture/colour/blend/uv-grid/quad-size)
//   sec4    : 2751 PSE emitter entities (MOTION timelines) — instruction-exact
//             transcription of the XEX LoadBin chain (PSE_*_LoadBin)
//   tail    : 868 effect records:
//               [u32 key][u32][u32][u8 n][n x item][u32 key]
//               item = [u32 group][u32 m][m x (u32 sec4_eid, u32 sys_id, u32)]
//             key = FNV1 of the LOWERCASED effect name. Each triplet pairs a
//             motion emitter (sec4) with an appearance system (sec3).
//
// Motion timelines are keyframed "generic parameters" addressed by index:
//   idx 2/3  = emission rate (base / spread), particles per FRAME (x30 => /s)
//   idx 4/5  = lifetime seconds (base / spread)
//   idx 6/7/8  = WIND-RESPONSE gains (combined with g_WindDirectionVector
//                and the gust vectors, x dt^2 — NOT acceleration, NOT colour)
//   idx 9/10/11 = initial velocity (9 = radial*cos(phi), 10 = radial*sin(phi),
//                 11 = axial; phi = random azimuth; game axes: z = up)
//   idx 12/13   = render size
// Values verified: all 2751 entities parse byte-exact; the tail's 3132
// triplets all reference valid sec4 entity ids.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Fx {

// One scalar keyframe timeline (time in seconds, value). Empty => use default.
struct Timeline {
    std::vector<std::pair<float, float>> keys;
    float default_value = 0.0f;

    bool empty() const { return keys.empty(); }
    // Linear interpolation over particle age; clamps to end keys.
    float eval(float t) const {
        if (keys.empty()) return default_value;
        if (t <= keys.front().first)  return keys.front().second;
        if (t >= keys.back().first)   return keys.back().second;
        for (size_t i = 1; i < keys.size(); ++i) {
            if (t <= keys[i].first) {
                const auto& a = keys[i - 1];
                const auto& b = keys[i];
                const float span = b.first - a.first;
                const float u = span > 1e-6f ? (t - a.first) / span : 0.0f;
                return a.second + (b.second - a.second) * u;
            }
        }
        return keys.back().second;
    }
    // First-keyframe (spawn-time) value, or default.
    float first_or(float fallback) const {
        return keys.empty() ? fallback : keys.front().second;
    }
    float max_value() const {
        float m = default_value;
        for (const auto& k : keys) m = k.second > m ? k.second : m;
        return m;
    }
};

enum class EmitterKind : uint8_t {
    Standard = 0, Type3 = 3, Attractor = 4, SplineEntity = 5,
    Type6 = 6, Light = 7, Light8 = 8, SplineEmitter = 9,
};

// A single emitter entity from section 4, with the timelines that drive it.
struct Emitter {
    uint32_t type_id = 0;
    uint32_t entity_id = 0;
    uint32_t sub_type = 0;

    // Local transform (relative to the effect / placement origin, z = up).
    std::array<float, 3> pos{ {0, 0, 0} };
    std::array<float, 4> quat{ {0, 0, 0, 1} };

    // Emitter controls (CParticleEmitterType).
    float max_active = 0.0f;      // NaN in bank = "unlimited"
    float initial_num = 0.0f;
    std::array<float, 4> fades{ {0, 0, 0, 0} };  // start/end fade in/out dist
    float start_time_secs = 0.0f;

    // Motion timelines (indices per header; corrected against the XEX).
    Timeline rate, rate_spread;   // 2, 3 (emission rate base/spread)
    Timeline life, life_spread;   // 4, 5 (lifetime seconds base/spread)
    Timeline acc_x, acc_y, acc_z; // 6, 7, 8 (acceleration / wind-force, z up)
    Timeline vel_x, vel_y, vel_z; // 9=radial·cos, 10=radial·sin, 11=axial (z up)
    Timeline size;                // 12 (render size scale over age; default 1)
    bool has_motion = false;      // true for Standard/SplineEmitter
};

// One textured child (material node) of a sec3 appearance system.
struct Visual {
    float delay = 0.0f;           // child start offset, seconds
    std::string texture;          // art\fx\pfx_textures\*.tex
    int   tex_cols = 1, tex_rows = 1;   // UV sprite-sheet grid
    bool  random_frame = false;
    float frame_speed = 0.0f;
    bool  displacement = false;
    bool  has_color = false;
    std::array<float, 3> color{ {1, 1, 1} };
    // CMaterialParamAlpha (tag 0x2). Runtime merge default = 1.0
    // (sub_83233268: record+16).
    bool  has_alpha = false;
    float alpha = 1.0f;
    // CMaterialParamRotationAngle (tag 0x200): authored billboard roll.
    // Absent => the particle does not spin.
    bool  has_rotation = false;
    float rot_speed = 0.0f;        // rad/s
    float rot_spread = 0.0f;
    bool  rot_initial_random = false;
    // CMaterialParamAlignmentQuat (tag 0x20000): world-space quad
    // orientation (waterfall sheets, ground ripples...). Quad axes =
    // quat-rotated X (width) / Y (height) in the emitter's game space,
    // instead of camera-facing.
    bool  has_align_quat = false;
    float align_quat[4] = { 0, 0, 0, 1 };   // x,y,z,w
    // CMaterialParam blend factors. src=tag 0x40 (SrcBlendMode),
    // dst=tag 0x80 (DestBlendMode), op=tag 0x100 (BlendOp). Values are the
    // engine's own BlendFactor enum (0=Zero 1=One 2=SrcColor 3=InvSrcColor
    // 4=SrcAlpha 5=InvSrcAlpha 6=DestAlpha 7=InvDestAlpha 8=DestColor
    // 9=InvDestColor) == D3D11_BLEND-1. -1 = param absent -> engine default
    // (alpha blend: SrcAlpha, InvSrcAlpha), used by smoke/dust/water.
    int   src_blend = -1;         // tag 0x40 SrcBlendMode
    int   dst_blend = -1;         // tag 0x80 DestBlendMode
    int   blend_op  = 0;          // tag 0x100 BlendOp (0 = Add)
    bool  has_size = false;
    float size_x = 0.0f, size_y = 0.0f;  // tag 0x10 quad size
};

// A sec3 appearance system: ordered textured children.
struct SystemDef {
    uint32_t id = 0;
    std::vector<Visual> visuals;
};

// One (motion, appearance) pair of an effect.
struct EffectPart {
    int emitter = -1;             // index into Bank::emitters
    int system  = -1;             // index into Bank::systems (-1 = none)
    uint32_t group = 0;           // tail item "A" value (grouping/slot)
};

// A named effect from the trailing map. Addressed by the FNV1 hash of the
// LOWERCASED effect name (this is how the engine, GDB and levels reference it).
struct Effect {
    uint32_t name_hash = 0;       // Fnv1Lower(name)
    std::string name;             // filled only when a source name is known
    std::vector<EffectPart> parts;
};

struct Bank {
    bool ok = false;
    std::string error;
    uint32_t version = 0;

    std::vector<Emitter>   emitters;  // all section-4 emitters, in order
    std::vector<SystemDef> systems;   // all section-3 appearance systems
    std::vector<Effect>    effects;   // trailing map records, sorted by hash

    // All pfx texture paths present in the bank (used for category fallback).
    std::vector<std::string> textures;

    const Effect* find_by_hash(uint32_t h) const;
    const Effect* find_by_name(const std::string& name) const;
};

// FNV-1 (multiply-then-xor), exact-case.
uint32_t Fnv1(const std::string& s);
// FNV-1 of the lowercased string — the effect-name key used by the bank map.
uint32_t Fnv1Lower(const std::string& s);

bool ParseParticleBank(const std::vector<uint8_t>& bytes, Bank& out);

}
