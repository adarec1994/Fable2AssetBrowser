#pragma once

// Fable 2 FX runtime — CPU particle simulation + billboard generation.
// Self-contained (no D3D): consumes real effects from the parsed ParticleBank
// (motion emitters + appearance systems, linked via the bank's effect map) and
// produces camera-facing textured quads. The renderer (ModelPreview) uploads
// the vertices and draws them per texture/blend batch.
//
// Simulation follows the reversed PSE update (see memory: emitter-motion):
//   emission rate  = timeline idx 2/3  (per emitter age; scaled to /s)
//   lifetime       = timeline idx 4/5  (seconds)
//   acceleration   = timelines idx 6/7/8 (game axes, z up) — NOT colour
//   init velocity  = idx 9=radial*cos, 10=radial*sin, 11=axial + random cone
//   render size    = idx 12 (default 1) * per-layer MATERIAL quad size
// Per-particle COLOUR + quad size come from the sec3 material layers, not the
// motion layer. A resolved effect instantiates one runtime emitter per bank
// part; each particle is assigned one of that part's material layers so the
// layers vary across the particle set (matching the game's material timeline)
// instead of over-stacking. Positions are game space (z up) mapped to the
// renderer's y-up world.

#include <cstdint>
#include <string>
#include <vector>

#include "ParticleBank.h"

namespace Fx {

// One placed effect in the world (from a level FX entity / GDB placement).
// pos is in game space (x, y horizontal; z up) exactly as authored.
struct Placement {
    float pos[3] = { 0, 0, 0 };
    float yaw = 0.0f;          // radians about the game up axis (+Z)
    float scale = 1.0f;
    // Full object orientation for tilted/wall-mounted props. When has_rot is
    // set, rot is the game-space (pre Y/Z-swap) 3x3 rotation matrix, row-major,
    // exactly as the mesh builds it (fill_gdb_rotation_matrix's `game[]`), and
    // the socket/emitter offsets are rotated by it instead of by yaw. Without
    // it the big lateral FX_Particle_DummyObject offset lands in the wrong spot.
    bool  has_rot = false;
    float rot[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    // Model-space offset from the object origin to the FX socket, read from the
    // object's model "FX_Particle_DummyObject" bone (the socket the engine
    // attaches the effect to). Zero => spawn at the object origin.
    float socket[3] = { 0, 0, 0 };
    std::string effect_name;   // e.g. "fxenv_water_fall_main" / "FX_Camp_Fire_01"
    uint32_t effect_hash = 0;  // Fnv1Lower(effect_name) — the bank map key
    bool resolved = false;     // filled by build(): matched a real bank effect
};

// A single billboard vertex the renderer consumes (POSITION/TEXCOORD/COLOR).
struct FxVertex {
    float x, y, z;
    float u, v;
    float r, g, b, a;
};

// A draw batch: all quads sharing one texture + blend mode.
struct FxBatch {
    std::string texture;        // .tex virtual path (empty => default sprite)
    int src_blend = 5;          // D3D11_BLEND enum
    int dst_blend = 6;
    std::vector<FxVertex> verts; // 6 verts per quad (two triangles)
};

class System {
public:
    // Build runtime instances from level placements against a parsed bank.
    // Marks each placement resolved/unresolved in place.
    void build(const Bank& bank, std::vector<Placement>& placements);
    void clear();
    bool empty() const { return instances_.empty(); }
    size_t instance_count() const { return instances_.size(); }
    size_t resolved_count() const { return resolved_count_; }
    size_t live_particle_count() const;

    // Advance the simulation by dt seconds.
    void update(float dt);

    // Emit camera-facing quads into per-(texture,blend) batches.
    // right/up are the camera basis vectors in world (render) space; eye is
    // the camera position (quads are sorted back-to-front per batch, as the
    // retail submit does — XEX sub_83221FC8 sorts by distance before the
    // render callback).
    void build_batches(const float cam_right[3], const float cam_up[3],
                       const float cam_eye[3],
                       std::vector<FxBatch>& out) const;

    // Distinct textures referenced (so the renderer can preload SRVs).
    std::vector<std::string> textures() const;

private:
    struct Particle {
        float pos[3];      // render space (y up)
        float vel[3];      // render space
        float age;
        float life;
        float rot;         // billboard roll
        float spin;        // roll speed (rad/s)
        float seed;        // [0,1) per-particle randomness
        int   layer;       // index into EmitterRT::visuals
    };
    // One appearance layer (a textured material child of the effect's system).
    struct VisualRT {
        std::string texture;
        int   cols = 1, rows = 1;
        float frame_speed = 0.0f;
        bool  random_frame = false;
        int   src_blend = 5, dst_blend = 6;   // D3D blend enums
        float color[3] = { 1, 1, 1 };
        float alpha = 1.0f;                   // CMaterialParamAlpha (tag 0x2)
        float half_x = 0.5f, half_y = 0.5f;   // billboard half-extents (world)
        float delay = 0.0f;                   // material-timeline StartTime (s)
        // Authored roll (tag 0x200); absent => no spin, angle 0.
        bool  has_rotation = false;
        float rot_speed = 0.0f, rot_spread = 0.0f;
        bool  rot_initial_random = false;
        // World-aligned quad (tag 0x20000): render-space axes precomputed at
        // build (placement rotation * alignment quat applied to X/Y).
        bool  aligned = false;
        float axis_r[3] = { 1, 0, 0 };
        float axis_u[3] = { 0, 1, 0 };
    };
    struct EmitterRT {
        const Emitter* def = nullptr;
        std::vector<VisualRT> visuals;     // material layers (>=1)
        float accum = 0.0f;                // fractional spawn accumulator
        float age = 0.0f;                  // seconds since this emitter started
        float origin[3] = { 0, 0, 0 };     // render-space spawn origin
        float yaw_cos = 1.0f, yaw_sin = 0.0f;
        // Full game-space placement rotation (row-major 3x3) when the
        // placement carries one; identity otherwise. Velocities and emitter
        // offsets rotate by this, not just yaw.
        bool  has_rot = false;
        float rot_game[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
        float max_visual_delay = 0.0f;     // material timeline end (seconds)
        float start_delay = 0.0f;          // emitter start time (s)
        float scale = 1.0f;
        float accel[3] = { 0, 0, 0 };      // render-space acceleration
        float gravity = 0.0f;              // extra downward accel (fallers)
        float size_scale = 1.0f;           // idx-12 first value (usually 1)
        bool  emitted_initial = false;
        float spawn_radius = 0.0f;         // small spawn jitter
        // fallback (unresolved effect, def == nullptr) motion:
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
    float frand();               // [0,1)
    float srand2();              // [-1,1)
};

}  // namespace Fx
