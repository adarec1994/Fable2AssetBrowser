#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ParticleBank.h"

namespace Fx {

struct Placement {
    float pos[3] = { 0, 0, 0 };
    float yaw = 0.0f;
    float scale = 1.0f;

    bool  has_rot = false;
    float rot[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };

    float socket[3] = { 0, 0, 0 };
    std::string effect_name;
    uint32_t effect_hash = 0;
    bool resolved = false;
};

struct FxVertex {
    float x, y, z;
    float u, v;
    float r, g, b, a;
};

struct FxBatch {
    std::string texture;
    int src_blend = 5;
    int dst_blend = 6;
    std::vector<FxVertex> verts;
};

class System {
public:

    void build(const Bank& bank, std::vector<Placement>& placements);
    void clear();
    bool empty() const { return instances_.empty(); }
    size_t instance_count() const { return instances_.size(); }
    size_t resolved_count() const { return resolved_count_; }
    size_t live_particle_count() const;

    void update(float dt);

    void build_batches(const float cam_right[3], const float cam_up[3],
                       const float cam_eye[3],
                       std::vector<FxBatch>& out) const;

    std::vector<std::string> textures() const;

private:
    struct Particle {
        float pos[3];
        float vel[3];
        float age;
        float life;
        float rot;
        float spin;
        float seed;
        int   layer;
    };

    struct VisualRT {
        std::string texture;
        int   cols = 1, rows = 1;
        float frame_speed = 0.0f;
        bool  random_frame = false;
        int   src_blend = 5, dst_blend = 6;
        float color[3] = { 1, 1, 1 };
        float alpha = 1.0f;
        float half_x = 0.5f, half_y = 0.5f;
        float delay = 0.0f;

        bool  has_rotation = false;
        float rot_speed = 0.0f, rot_spread = 0.0f;
        bool  rot_initial_random = false;

        bool  aligned = false;
        float axis_r[3] = { 1, 0, 0 };
        float axis_u[3] = { 0, 1, 0 };
    };
    struct EmitterRT {
        const Emitter* def = nullptr;
        std::vector<VisualRT> visuals;
        float accum = 0.0f;
        float age = 0.0f;
        float origin[3] = { 0, 0, 0 };
        float yaw_cos = 1.0f, yaw_sin = 0.0f;

        bool  has_rot = false;
        float rot_game[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
        float max_visual_delay = 0.0f;
        float start_delay = 0.0f;
        float scale = 1.0f;
        float accel[3] = { 0, 0, 0 };
        float gravity = 0.0f;
        float size_scale = 1.0f;
        bool  emitted_initial = false;
        float spawn_radius = 0.0f;

        float fb_axial = 0.0f, fb_spread = 0.0f, fb_rate = 0.0f, fb_life = 0.0f;
        std::vector<Particle> particles;
    };
    struct Instance {
        Placement place;
        float time = 0.0f;
        bool  fallback = false;
        std::vector<EmitterRT> emitters;
    };

    void spawn_one(EmitterRT& rt);

    std::vector<Instance> instances_;
    size_t resolved_count_ = 0;
    uint32_t rng_ = 0x1234567u;
    float frand();
    float srand2();
};

}
