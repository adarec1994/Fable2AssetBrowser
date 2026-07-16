#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Fx {

struct Timeline {
    std::vector<std::pair<float, float>> keys;
    float default_value = 0.0f;

    bool empty() const { return keys.empty(); }

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

struct Emitter {
    uint32_t type_id = 0;
    uint32_t entity_id = 0;
    uint32_t sub_type = 0;

    std::array<float, 3> pos{ {0, 0, 0} };
    std::array<float, 4> quat{ {0, 0, 0, 1} };

    float max_active = 0.0f;
    float initial_num = 0.0f;
    std::array<float, 4> fades{ {0, 0, 0, 0} };
    float start_time_secs = 0.0f;

    Timeline rate, rate_spread;
    Timeline life, life_spread;
    Timeline acc_x, acc_y, acc_z;
    Timeline vel_x, vel_y, vel_z;
    Timeline size;
    bool has_motion = false;
};

struct Visual {
    float delay = 0.0f;
    std::string texture;
    int   tex_cols = 1, tex_rows = 1;
    bool  random_frame = false;
    float frame_speed = 0.0f;
    bool  displacement = false;
    bool  has_color = false;
    std::array<float, 3> color{ {1, 1, 1} };

    bool  has_alpha = false;
    float alpha = 1.0f;

    bool  has_rotation = false;
    float rot_speed = 0.0f;
    float rot_spread = 0.0f;
    bool  rot_initial_random = false;

    bool  has_align_quat = false;
    float align_quat[4] = { 0, 0, 0, 1 };

    int   src_blend = -1;
    int   dst_blend = -1;
    int   blend_op  = 0;
    bool  has_size = false;
    float size_x = 0.0f, size_y = 0.0f;
};

struct SystemDef {
    uint32_t id = 0;
    std::vector<Visual> visuals;
};

struct EffectPart {
    int emitter = -1;
    int system  = -1;
    uint32_t group = 0;
};

struct Effect {
    uint32_t name_hash = 0;
    std::string name;
    std::vector<EffectPart> parts;
};

struct Bank {
    bool ok = false;
    std::string error;
    uint32_t version = 0;

    std::vector<Emitter>   emitters;
    std::vector<SystemDef> systems;
    std::vector<Effect>    effects;

    std::vector<std::string> textures;

    const Effect* find_by_hash(uint32_t h) const;
    const Effect* find_by_name(const std::string& name) const;
};

uint32_t Fnv1(const std::string& s);

uint32_t Fnv1Lower(const std::string& s);

bool ParseParticleBank(const std::vector<uint8_t>& bytes, Bank& out);

}
