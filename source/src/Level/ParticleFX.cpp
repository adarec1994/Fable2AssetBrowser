#include "ParticleFX.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace Fx {

namespace {

// Emission-rate scale — EXACT (XEX sub_8322F808 frame-factor init): the
// engine runs particles at 30 fps internally; factor arrays are
// {15 fps, x2 rate} / {30 fps, x1 rate}, so the effective emission is
// rate * 30 per second in both modes.
constexpr float kRateToPerSec = 30.0f;
// Tiny floor only to avoid div-by-zero; retail lifetimes go as short as
// 0.1 s (campfire flame flicker) and must not be clamped up.
constexpr float kMinLife = 0.02f;
// Stand-in downward accel (world u/s^2) for "falling" effects (water / dust),
// approximating the attractors we don't simulate.
constexpr float kFallGravity = 3.0f;

// A "falling" effect (its particles should stream downward) inferred from the
// effect name, since the real fall comes from attractors we don't model.
bool name_is_faller(const std::string& n) {
    std::string s;
    for (char c : n) s.push_back((char)std::tolower((unsigned char)c));
    auto has = [&](const char* k) { return s.find(k) != std::string::npos; };
    return has("water") || has("fall") || has("splash") || has("dust") ||
           has("rain") || has("drip") || has("pour");
}

std::string to_lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Coarse category from an effect name — fallback when an effect can't be
// resolved against the bank (so unnamed / code-spawned FX still show up).
struct Category {
    const char* tex_key;
    float vy;              // upward velocity bias
    float spread;
    float life;
    float size;            // half-extent
    float rate;            // particles/s
    float r, g, b;
    int   dst_blend;       // 2 = additive, 6 = alpha
    bool  faller;
};

Category categorize(const std::string& name) {
    const std::string n = to_lower(name);
    auto has = [&](const char* k) { return n.find(k) != std::string::npos; };
    if (has("waterfall") || has("water_fall") || has("falls"))
        return { "water_fall", 0.0f, 1.2f, 1.2f, 0.4f, 40.f, 0.8f,0.85f,0.95f, 6, true };
    if (has("splash") || has("watermill") || has("fountain") || has("water_flow"))
        return { "water",      0.6f, 1.0f, 0.9f, 0.35f,30.f, 0.85f,0.9f,0.96f, 6, true };
    if (has("steam") || has("hot_pool") || has("mist"))
        return { "smokepuff",  1.4f, 0.5f, 2.2f, 0.8f, 14.f, 0.9f,0.92f,0.95f, 6, false };
    if (has("chimney") || has("smoke"))
        return { "smokepuff",  1.4f, 0.5f, 2.5f, 0.9f, 12.f, 0.6f,0.6f,0.62f, 6, false };
    if (has("fire") || has("flame") || has("torch") || has("brazier") ||
        has("campfire") || has("bonfire") || has("candle") || has("lamp"))
        return { "flames",     2.0f, 0.35f,0.5f, 0.5f, 45.f, 1.0f,0.7f,0.35f, 2, false };
    if (has("dust") || has("sand"))
        return { "dust",       0.2f, 0.7f, 1.6f, 0.4f, 12.f, 0.55f,0.45f,0.3f, 6, true };
    // No keyword hit: null tex_key = "no category", the caller skips the
    // placement. Guessing a generic smoke puff here painted fake smoke over
    // every window-light placement (ESA_*Win* et al) the bank can't resolve.
    return { nullptr,          0.0f, 0.0f, 0.0f, 0.0f, 0.f, 0.0f,0.0f,0.0f, 6, false };
}

const std::string* pick_texture(const Bank& bank, const char* key) {
    for (const auto& t : bank.textures)
        if (to_lower(t).find(key) != std::string::npos) return &t;
    return nullptr;
}

// Game space (x,y horizontal; z up) -> render space (y up).
inline void game_to_render(const float g[3], float r[3]) {
    r[0] = g[0]; r[1] = g[2]; r[2] = g[1];
}

// Engine BlendFactor enum -> D3D11_BLEND (== enum + 1 for 0..9). A value of
// -1 means the material had no Src/DestBlendMode param; the engine defaults
// are (One, InvSrcAlpha) — PREMULTIPLIED alpha (renderer ctor sub_8323FF48
// stores 1/5 at +0x24/+0x28; the retail particle PS premultiplies vertex
// alpha into rgb, matching the src=One factor).
inline int map_blend(int e, int dflt) {
    return (e >= 0 && e <= 9) ? e + 1 : dflt;
}
void blend_from_material(const Visual& vis, int& src, int& dst) {
    src = map_blend(vis.src_blend, 2 /*D3D11_BLEND_ONE*/);
    dst = map_blend(vis.dst_blend, 6 /*D3D11_BLEND_INV_SRC_ALPHA*/);
}

}  // namespace

float System::frand() {
    rng_ = rng_ * 1664525u + 1013904223u;
    return float((rng_ >> 8) & 0xFFFFFF) / float(0x1000000);
}
float System::srand2() { return frand() * 2.0f - 1.0f; }

void System::clear() { instances_.clear(); resolved_count_ = 0; }

void System::build(const Bank& bank, std::vector<Placement>& places) {
    instances_.clear();
    resolved_count_ = 0;
    instances_.reserve(places.size());

    for (auto& p : places) {
        if (!p.effect_hash) p.effect_hash = Fnv1Lower(p.effect_name);
        Instance inst;
        inst.place = p;

        const float sc = (p.scale > 0.0001f) ? p.scale : 1.0f;
        const float cy = std::cos(p.yaw), sy = std::sin(p.yaw);
        (void)name_is_faller;   // fallback categories carry their own flag

        // Rotate a model-space offset into world game space exactly as the prop
        // mesh is transformed: full euler matrix for tilted/wall props, else a
        // yaw spin about the game up axis.
        auto rot_game = [&](const float v[3], float out[3]) {
            if (p.has_rot) {
                out[0] = p.rot[0]*v[0] + p.rot[1]*v[1] + p.rot[2]*v[2];
                out[1] = p.rot[3]*v[0] + p.rot[4]*v[1] + p.rot[5]*v[2];
                out[2] = p.rot[6]*v[0] + p.rot[7]*v[1] + p.rot[8]*v[2];
            } else {
                out[0] =  v[0] * cy + v[1] * sy;
                out[1] = -v[0] * sy + v[1] * cy;
                out[2] =  v[2];
            }
        };
        float rsk[3];
        rot_game(p.socket, rsk);
        const float sock_x = p.pos[0] + sc * rsk[0];
        const float sock_y = p.pos[1] + sc * rsk[1];
        const float sock_z = p.pos[2] + sc * rsk[2];

        const Effect* eff = bank.find_by_hash(p.effect_hash);
        if (eff) {
            for (const auto& part : eff->parts) {
                if (part.emitter < 0 ||
                    part.emitter >= (int)bank.emitters.size()) continue;
                const Emitter& em = bank.emitters[part.emitter];
                if (!em.has_motion) continue;   // lights/attractors don't billboard

                EmitterRT rt;
                rt.def = &em;
                rt.scale = sc;
                rt.start_delay = em.start_time_secs;
                rt.yaw_cos = cy; rt.yaw_sin = sy;
                rt.has_rot = p.has_rot;
                if (p.has_rot)
                    std::memcpy(rt.rot_game, p.rot, sizeof(rt.rot_game));

                // spawn origin = FX socket + rotated local emitter offset.
                float lg[3];
                if (p.has_rot) {
                    rot_game(em.pos.data(), lg);
                } else {
                    lg[0] = em.pos[0] * cy - em.pos[1] * sy;
                    lg[1] = em.pos[0] * sy + em.pos[1] * cy;
                    lg[2] = em.pos[2];
                }
                float g[3] = { sock_x + lg[0] * sc,
                               sock_y + lg[1] * sc,
                               sock_z + lg[2] * sc };
                game_to_render(g, rt.origin);

                // Timelines 6/7/8 are WIND-RESPONSE gains, not acceleration:
                // the XEX update (ParticleEmitter_ApplyWindAndUpdate,
                // 0x83229DC0) combines them with g_WindDirectionVector and
                // the gust vectors, scaled by dt^2. The preview simulates a
                // calm scene, so they contribute nothing here; treating them
                // as accelerations hurled authored FX (waterfall gain = 5.0)
                // sideways.
                rt.accel[0] = rt.accel[1] = rt.accel[2] = 0.0f;

                // Resolved effects carry their motion in the authored
                // timelines; the gravity stand-in is for name-category
                // fallbacks only.
                rt.gravity = 0.0f;

                rt.size_scale = em.size.first_or(1.0f);
                if (rt.size_scale <= 0.0f) rt.size_scale = 1.0f;
                rt.spawn_radius = 0.12f * sc;

                // appearance layers from the linked system (skip displacement).
                if (part.system >= 0 && part.system < (int)bank.systems.size()) {
                    for (const auto& vis : bank.systems[part.system].visuals) {
                        if (vis.texture.empty() || vis.displacement) continue;
                        VisualRT v;
                        v.texture = vis.texture;
                        v.cols = vis.tex_cols; v.rows = vis.tex_rows;
                        v.frame_speed = vis.frame_speed;
                        v.random_frame = vis.random_frame;
                        v.delay = vis.delay;
                        blend_from_material(vis, v.src_blend, v.dst_blend);
                        if (vis.has_color) {
                            v.color[0] = vis.color[0];
                            v.color[1] = vis.color[1];
                            v.color[2] = vis.color[2];
                        }
                        // Runtime material merge defaults alpha to 1
                        // (XEX sub_83233268, record+16).
                        v.alpha = vis.has_alpha ? vis.alpha : 1.0f;
                        // Authored roll (tag 0x200); absent => no spin.
                        v.has_rotation = vis.has_rotation;
                        v.rot_speed = vis.rot_speed;
                        v.rot_spread = vis.rot_spread;
                        v.rot_initial_random = vis.rot_initial_random;
                        // World-aligned quad (tag 0x20000 AlignmentQuat):
                        // base plane = game ground (X width, Y height),
                        // rotated by the quat, then the placement rotation.
                        // Identity => flat ground quad (pool ripples);
                        // waterfall sheets carry a quat standing the plane
                        // upright.
                        if (vis.has_align_quat) {
                            auto qrot = [&](const float q[4], const float in[3],
                                            float out[3]) {
                                const float qx = q[0], qy = q[1], qz = q[2],
                                            qw = q[3];
                                float cx = qy * in[2] - qz * in[1];
                                float cy2 = qz * in[0] - qx * in[2];
                                float cz = qx * in[1] - qy * in[0];
                                cx += qw * in[0];
                                cy2 += qw * in[1];
                                cz += qw * in[2];
                                out[0] = in[0] + 2.0f * (qy * cz - qz * cy2);
                                out[1] = in[1] + 2.0f * (qz * cx - qx * cz);
                                out[2] = in[2] + 2.0f * (qx * cy2 - qy * cx);
                            };
                            const float bx[3] = { 1, 0, 0 };
                            const float by[3] = { 0, 1, 0 };
                            float rg[3], ug[3], rg2[3], ug2[3];
                            qrot(vis.align_quat, bx, rg);
                            qrot(vis.align_quat, by, ug);
                            rot_game(rg, rg2);
                            rot_game(ug, ug2);
                            game_to_render(rg2, v.axis_r);
                            game_to_render(ug2, v.axis_u);
                            v.aligned = true;
                        }
                        if (v.delay > rt.max_visual_delay)
                            rt.max_visual_delay = v.delay;
                        // CMaterialParamSize "Dimensions" (tag 0x10) used directly
                        // as the per-particle half-extent (matches the working
                        // look; the IDA cull radius (sx^2+sy^2)/2 is edge-bound so
                        // it doesn't force a /2).
                        float hx = vis.has_size ? vis.size_x : 0.5f;
                        float hy = vis.has_size ? vis.size_y : 0.5f;
                        if (hx <= 0.0f) hx = 0.5f;
                        if (hy <= 0.0f) hy = 0.5f;
                        v.half_x = hx * sc;
                        v.half_y = hy * sc;
                        rt.visuals.push_back(std::move(v));
                    }
                }
                if (rt.visuals.empty()) continue;   // all-displacement/no sprite

                inst.emitters.push_back(std::move(rt));
            }
        }

        if (!inst.emitters.empty()) {
            inst.place.resolved = true;
            p.resolved = true;
            ++resolved_count_;
        } else {
            // ---- category fallback ----
            Category cat = categorize(p.effect_name);
            if (!cat.tex_key) continue;   // unknown effect: don't invent smoke
            inst.fallback = true;
            EmitterRT rt;
            rt.scale = sc;
            rt.yaw_cos = cy; rt.yaw_sin = sy;
            rt.def = nullptr;
            rt.gravity = cat.faller ? kFallGravity : 0.0f;
            rt.spawn_radius = cat.spread * 0.3f * sc;
            rt.size_scale = 1.0f;
            rt.fb_axial = cat.vy * sc;
            rt.fb_spread = cat.spread * sc;
            rt.fb_rate = cat.rate;
            rt.fb_life = cat.life;
            float g[3] = { sock_x, sock_y, sock_z };
            game_to_render(g, rt.origin);
            VisualRT v;
            if (const std::string* t = pick_texture(bank, cat.tex_key))
                v.texture = *t;
            v.color[0] = cat.r; v.color[1] = cat.g; v.color[2] = cat.b;
            v.src_blend = 5; v.dst_blend = cat.dst_blend;
            v.half_x = v.half_y = cat.size * sc;
            rt.visuals.push_back(std::move(v));
            inst.emitters.push_back(std::move(rt));
        }

        instances_.push_back(std::move(inst));
    }
}

void System::spawn_one(EmitterRT& rt) {
    const Emitter* d = rt.def;
    Particle pt{};

    // spawn position: origin + small area jitter
    pt.pos[0] = rt.origin[0] + srand2() * rt.spawn_radius;
    pt.pos[1] = rt.origin[1] + srand2() * rt.spawn_radius * 0.3f;
    pt.pos[2] = rt.origin[2] + srand2() * rt.spawn_radius;

    // velocity (game space), byte-faithful to PSE_ComputeInitialVelocity_dir:
    //   vx = Eval(9)*cos(phi), vy = Eval(10)*sin(phi), vz = Eval(11)
    // with phi = random azimuth over [0,2pi). The spread/cone is entirely the
    // radial magnitude (idx9/10) — there is NO extra perturbation in the engine.
    float velx = d ? d->vel_x.eval(rt.age) : rt.fb_spread;
    float vely = d ? d->vel_y.eval(rt.age) : rt.fb_spread;
    float velz = d ? d->vel_z.eval(rt.age) : rt.fb_axial;
    const float phi = frand() * 6.2831853f;
    float gx = velx * std::cos(phi);
    float gy = vely * std::sin(phi);
    float gz = velz;

    // rotate into world game space (full placement rotation when present —
    // yaw alone misdirects authored FX entities), scale, game -> render.
    float vg[3];
    if (rt.has_rot) {
        const float* m = rt.rot_game;
        vg[0] = (m[0] * gx + m[1] * gy + m[2] * gz) * rt.scale;
        vg[1] = (m[3] * gx + m[4] * gy + m[5] * gz) * rt.scale;
        vg[2] = (m[6] * gx + m[7] * gy + m[8] * gz) * rt.scale;
    } else {
        vg[0] = (gx * rt.yaw_cos - gy * rt.yaw_sin) * rt.scale;
        vg[1] = (gx * rt.yaw_sin + gy * rt.yaw_cos) * rt.scale;
        vg[2] = gz * rt.scale;
    }
    game_to_render(vg, pt.vel);

    pt.age = 0.0f;
    // EXACT life law (PSE_SpawnParticle 0x8226A650):
    //   life = Eval(idx4)@emitterAge + rand01 * Eval(idx5)@emitterAge
    // No randomisation beyond the authored spread. life <= 0 (e.g. the
    // waterfall sheet has no life keys) = long-lived in retail; stand in
    // with the material timeline end so the fade cycle completes.
    float life0;
    if (d) {
        life0 = d->life.eval(rt.age) + frand() * d->life_spread.eval(rt.age);
        if (life0 <= 0.0f) {
            life0 = rt.max_visual_delay > 0.2f ? rt.max_visual_delay : 1.4f;
        }
    } else {
        life0 = rt.fb_life;
        if (rt.gravity > 0.0f) life0 = std::max(life0, 1.0f) * 2.2f;
        life0 *= 0.85f + 0.3f * frand();
    }
    pt.life = std::max(kMinLife, life0);
    // Roll comes from the material (tag 0x200 RotationAngle); no authored
    // rotation param => the billboard does not spin (the old random spin
    // visibly rotated waterfall sheets and ground ripples).
    const VisualRT* v0 = rt.visuals.empty() ? nullptr : &rt.visuals[0];
    if (v0 && v0->has_rotation) {
        pt.rot = v0->rot_initial_random ? frand() * 6.2831853f : 0.0f;
        // Bank angular speeds are authored per 30 Hz frame (same convention
        // as the emission rate).
        pt.spin = (v0->rot_speed + v0->rot_spread * srand2()) * 30.0f;
    } else {
        pt.rot = 0.0f;
        pt.spin = 0.0f;
    }
    pt.seed = frand();
    pt.layer = 0;   // layer is chosen at render from the material timeline (age)
    rt.particles.push_back(std::move(pt));
}

size_t System::live_particle_count() const {
    size_t n = 0;
    for (const auto& inst : instances_)
        for (const auto& e : inst.emitters) n += e.particles.size();
    return n;
}

void System::update(float dt) {
    if (dt <= 0.0f) dt = 1.0f / 60.0f;
    dt = std::min(dt, 0.1f);
    for (auto& inst : instances_) {
        inst.time += dt;
        for (auto& rt : inst.emitters) {
            const Emitter* d = rt.def;
            if (inst.time < rt.start_delay) continue;
            rt.age = inst.time - rt.start_delay;   // emitter age for timeline eval

            // one-shot initial burst
            if (!rt.emitted_initial) {
                rt.emitted_initial = true;
                int init = d ? int(std::max(0.0f, d->initial_num)) : 1;
                init = std::min(init, 64);
                for (int i = 0; i < init; ++i) spawn_one(rt);
            }

            // EXACT emission law (PSE_EmitterUpdate_EmissionRate 0x82271E28):
            // acc += (Eval(2)@age + rand01*Eval(3)@age) * 30Hz * dt.
            float rate = d ? (d->rate.eval(rt.age) +
                              frand() * d->rate_spread.eval(rt.age)) *
                                 kRateToPerSec
                           : rt.fb_rate;
            float cap = d ? d->max_active : 500.0f;
            if (!(cap > 0.0f) || cap != cap) cap = 500.0f;   // NaN => unlimited
            cap = std::min(cap, 2500.0f);

            rt.accum += std::max(rate, 0.0f) * dt;
            int guard = 600;
            while (rt.accum >= 1.0f && guard-- > 0) {
                rt.accum -= 1.0f;
                if (rt.particles.size() >= (size_t)cap) { rt.accum = 0.0f; break; }
                spawn_one(rt);
            }

            // integrate: v += (accel - gravity)*dt ; pos += v*dt
            for (auto& pt : rt.particles) {
                pt.age += dt;
                pt.vel[0] += rt.accel[0] * dt;
                pt.vel[1] += (rt.accel[1] - rt.gravity) * dt;
                pt.vel[2] += rt.accel[2] * dt;
                pt.pos[0] += pt.vel[0] * dt;
                pt.pos[1] += pt.vel[1] * dt;
                pt.pos[2] += pt.vel[2] * dt;
                pt.rot += pt.spin * dt;
            }
            rt.particles.erase(
                std::remove_if(rt.particles.begin(), rt.particles.end(),
                    [](const Particle& p){ return p.age >= p.life; }),
                rt.particles.end());
        }
    }
}

void System::build_batches(const float cr[3], const float cu[3],
                           const float ce[3],
                           std::vector<FxBatch>& out) const {
    out.clear();
    auto get_batch = [&](const std::string& tex, int sb, int db) -> FxBatch& {
        for (auto& b : out)
            if (b.texture == tex && b.src_blend == sb && b.dst_blend == db)
                return b;
        out.push_back(FxBatch{}); out.back().texture = tex;
        out.back().src_blend = sb; out.back().dst_blend = db;
        return out.back();
    };
    for (const auto& inst : instances_) {
        for (const auto& rt : inst.emitters) {
            const Emitter* d = rt.def;
            for (const auto& pt : rt.particles) {
                if (rt.visuals.empty()) continue;
                // MATERIAL TIMELINE (retail semantics): the system's material
                // children are KEYFRAMES of one material over the particle's
                // age, keyed by StartTime (delay). The retail pixel shaders
                // (entries 244-259) cross-fade up to 4 node TEXTURES by a
                // per-vertex age coordinate against per-node thresholds
                // (ParticleParams_Blend c31/c46); scalar params merge through
                // the runtime record (XEX sub_83233268 with defaults colour=1,
                // alpha=1). Host: lerp colour/alpha/size across the node
                // segment and cross-fade the two node textures with
                // complementary quads (the exact per-node fade rate constant
                // lives in the not-yet-located constant uploader; the
                // full-segment lerp hits every node's authored value at its
                // authored time).
                // Material time base — EXACT (PSE_ParticleIntegrate_kernel
                // 0x8321BF20): per-particle attributes lerp between
                // consecutive material nodes by the particle's RAW age
                // against the node time table (w = (age - t_i) * invSpan).
                // Short-lived particles sample only the early ramp (the
                // campfire flame's 0.1 s particles ride node 0's alpha
                // ramp — that is the authored soft flicker).
                const float t_life = pt.life > 0 ? pt.age / pt.life : 1.0f;
                const float mat_t = pt.age;
                int seg = 0;
                for (int i = 0; i < (int)rt.visuals.size(); ++i)
                    if (rt.visuals[i].delay <= mat_t) seg = i;
                const VisualRT& va = rt.visuals[seg];
                const VisualRT* vb = (seg + 1 < (int)rt.visuals.size())
                                         ? &rt.visuals[seg + 1] : nullptr;
                float w = 0.0f;
                if (vb) {
                    const float span = vb->delay - va.delay;
                    w = span > 1e-4f
                        ? std::clamp((mat_t - va.delay) / span, 0.0f, 1.0f)
                        : 1.0f;
                }
                auto lerpf = [](float x, float y, float k) {
                    return x + (y - x) * k;
                };
                const float t = t_life;

                // size over life: material half-extents (node-lerped)
                // * motion idx-12 curve.
                float sfac = rt.size_scale;
                if (d && !d->size.empty()) sfac = d->size.eval(pt.age);
                if (sfac <= 0.0f) sfac = 1.0f;
                const float hx =
                    (vb ? lerpf(va.half_x, vb->half_x, w) : va.half_x) * sfac;
                const float hy =
                    (vb ? lerpf(va.half_y, vb->half_y, w) : va.half_y) * sfac;
                if (hx <= 0.0f || hy <= 0.0f) continue;

                // Colour/alpha = the node-lerped material values. With the
                // lifetime-mapped material time the authored cycles land on 0
                // alpha at death themselves (flame/flare/waterfall all key
                // their last node to 0) — no host fade needed.
                float col[3] = {
                    vb ? lerpf(va.color[0], vb->color[0], w) : va.color[0],
                    vb ? lerpf(va.color[1], vb->color[1], w) : va.color[1],
                    vb ? lerpf(va.color[2], vb->color[2], w) : va.color[2],
                };
                float a = vb ? lerpf(va.alpha, vb->alpha, w) : va.alpha;
                if (a <= 0.003f) continue;

                const float c = std::cos(pt.rot), s = std::sin(pt.rot);
                float rx = cr[0] * c + cu[0] * s;
                float ry = cr[1] * c + cu[1] * s;
                float rz = cr[2] * c + cu[2] * s;
                float ux = -cr[0] * s + cu[0] * c;
                float uy = -cr[1] * s + cu[1] * c;
                float uz = -cr[2] * s + cu[2] * c;
                float px = pt.pos[0], py = pt.pos[1], pz = pt.pos[2];

                // Emit one textured quad for a material node.
                auto emit_quad = [&](const VisualRT& vis, float node_a) {
                    if (vis.texture.empty() || node_a <= 0.003f) return;
                    const int cols = std::max(1, vis.cols);
                    const int rows = std::max(1, vis.rows);
                    const int frames = cols * rows;
                    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
                    if (frames > 1) {
                        // Retail computes the frame in the PS from a
                        // per-vertex frame float (entry 245: floor/frac by
                        // divisions); host selects the frame CPU-side —
                        // identical UV result. Frame speed is authored in
                        // bank frame units (30 Hz), like the emission rate.
                        int f;
                        if (vis.frame_speed > 0.0f)
                            f = int(pt.age * vis.frame_speed * 30.0f);
                        else
                            f = int(t * frames);
                        if (vis.random_frame) f += int(pt.seed * frames);
                        f %= frames;
                        const int fx = f % cols, fy = f / cols;
                        u0 = float(fx) / cols;   u1 = float(fx + 1) / cols;
                        v0 = float(fy) / rows;   v1 = float(fy + 1) / rows;
                    }
                    FxBatch& batch =
                        get_batch(vis.texture, vis.src_blend, vis.dst_blend);
                    // World-aligned materials use their authored plane;
                    // everything else is a camera-facing billboard with roll.
                    const float Rx = vis.aligned ? vis.axis_r[0] : rx;
                    const float Ry = vis.aligned ? vis.axis_r[1] : ry;
                    const float Rz = vis.aligned ? vis.axis_r[2] : rz;
                    const float Ux = vis.aligned ? vis.axis_u[0] : ux;
                    const float Uy = vis.aligned ? vis.axis_u[1] : uy;
                    const float Uz = vis.aligned ? vis.axis_u[2] : uz;
                    auto V = [&](float sx, float sy, float u, float v) {
                        FxVertex fv;
                        fv.x = px + (Rx * sx) * hx + (Ux * sy) * hy;
                        fv.y = py + (Ry * sx) * hx + (Uy * sy) * hy;
                        fv.z = pz + (Rz * sx) * hx + (Uz * sy) * hy;
                        fv.u = u; fv.v = v;
                        fv.r = col[0]; fv.g = col[1];
                        fv.b = col[2]; fv.a = node_a;
                        return fv;
                    };
                    FxVertex v00 = V(-1, -1, u0, v1), v10 = V(1, -1, u1, v1);
                    FxVertex v01 = V(-1, 1, u0, v0), v11 = V(1, 1, u1, v0);
                    batch.verts.push_back(v00); batch.verts.push_back(v11);
                    batch.verts.push_back(v10);
                    batch.verts.push_back(v00); batch.verts.push_back(v01);
                    batch.verts.push_back(v11);
                };

                if (vb && vb->texture != va.texture) {
                    // Texture cross-fade between material nodes: retail blends
                    // the node textures inside one draw (PS entries 248-259);
                    // host draws two complementary quads.
                    emit_quad(va, a * (1.0f - w));
                    emit_quad(*vb, a * w);
                } else {
                    emit_quad(va, a);
                }
            }
        }
    }

    // Back-to-front quad sort within each batch (retail: sub_83221FC8
    // builds a distance-sorted index array before submitting when the
    // emitter's SortParticles flag is set; sorting always is the safe host
    // equivalent for alpha blending).
    for (auto& b : out) {
        const size_t quads = b.verts.size() / 6;
        if (quads < 2) continue;
        std::vector<std::pair<float, uint32_t>> order(quads);
        for (size_t q = 0; q < quads; ++q) {
            // v00 and v11 are opposite corners: their midpoint = centre.
            const FxVertex& p0 = b.verts[q * 6 + 0];
            const FxVertex& p1 = b.verts[q * 6 + 1];
            const float cx = 0.5f * (p0.x + p1.x) - ce[0];
            const float cy = 0.5f * (p0.y + p1.y) - ce[1];
            const float cz = 0.5f * (p0.z + p1.z) - ce[2];
            order[q] = { cx * cx + cy * cy + cz * cz, (uint32_t)q };
        }
        std::sort(order.begin(), order.end(),
                  [](const auto& l, const auto& r) { return l.first > r.first; });
        std::vector<FxVertex> sorted;
        sorted.reserve(b.verts.size());
        for (const auto& [d, q] : order)
            sorted.insert(sorted.end(), b.verts.begin() + q * 6,
                          b.verts.begin() + q * 6 + 6);
        b.verts = std::move(sorted);
    }
}

std::vector<std::string> System::textures() const {
    std::vector<std::string> out;
    for (const auto& inst : instances_)
        for (const auto& rt : inst.emitters)
            for (const auto& vis : rt.visuals) {
                if (vis.texture.empty()) continue;
                if (std::find(out.begin(), out.end(), vis.texture) == out.end())
                    out.push_back(vis.texture);
            }
    return out;
}

}  // namespace Fx
