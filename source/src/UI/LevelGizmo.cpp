#include "LevelGizmo.h"

#include <algorithm>
#include <cmath>

#include "ModelPreview.h"

namespace LevelGizmo {
namespace {

constexpr float kFov = 60.0f * 3.14159265f / 180.0f;
constexpr float kHitPx = 10.0f;
constexpr int kCircleSegs = 48;

const float kAxisPreview[3][3] = {
    { 1.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f },
    { 0.0f, 1.0f, 0.0f },
};

const float kPlaneB1[3][3] = {
    { 0.0f, 1.0f, 0.0f },
    { 1.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f },
};
const float kPlaneB2[3][3] = {
    { 0.0f, 0.0f, 1.0f },
    { 0.0f, 1.0f, 0.0f },
    { 1.0f, 0.0f, 0.0f },
};

struct DragState {
    bool active = false;
    int axis = -1;
    int hover_axis = -1;
    Mode mode = Mode::Translate;
    float t_prev = 0.0f;
    float angle_prev = 0.0f;
    float scale_applied = 1.0f;
    float t_start = 0.0f;
    float axis_origin[3] = {0, 0, 0};
};

DragState& drag() {
    static DragState d;
    return d;
}

Mode& mode_ref() {
    static Mode m = Mode::Translate;
    return m;
}

struct CamBasis {
    float r[3], u[3], f[3];
};

CamBasis basis_of(const FlyCam& cam) {
    const float cy = std::cos(cam.yaw), sy = std::sin(cam.yaw);
    const float cp = std::cos(cam.pitch), sp = std::sin(cam.pitch);
    CamBasis b;
    b.f[0] = sy * cp;  b.f[1] = sp;  b.f[2] = cy * cp;
    b.r[0] = cy;       b.r[1] = 0;   b.r[2] = -sy;
    b.u[0] = -sp * sy; b.u[1] = cp;  b.u[2] = -sp * cy;
    return b;
}

bool project(const FlyCam& cam, const CamBasis& b,
             const ImVec2& origin, const ImVec2& region,
             const float wp[3], ImVec2& out) {
    const float fw = std::max(1.0f, region.x);
    const float fh = std::max(1.0f, region.y);
    const float aspect = fw / fh;
    const float tan_half = std::tan(0.5f * kFov);

    const float rel[3] = { wp[0] - cam.pos[0],
                           wp[1] - cam.pos[1],
                           wp[2] - cam.pos[2] };
    const float vx = rel[0]*b.r[0] + rel[1]*b.r[1] + rel[2]*b.r[2];
    const float vy = rel[0]*b.u[0] + rel[1]*b.u[1] + rel[2]*b.u[2];
    const float vz = rel[0]*b.f[0] + rel[1]*b.f[1] + rel[2]*b.f[2];
    if (vz <= 0.01f) return false;

    out.x = origin.x + (vx / (vz * tan_half * aspect) * 0.5f + 0.5f) * fw;
    out.y = origin.y + (0.5f - vy / (vz * tan_half) * 0.5f) * fh;
    return true;
}

void mouse_ray(const FlyCam& cam, const CamBasis& b,
               const ImVec2& mouse, const ImVec2& origin,
               const ImVec2& region, float out_dir[3]) {
    const float fw = std::max(1.0f, region.x);
    const float fh = std::max(1.0f, region.y);
    const float aspect = fw / fh;
    const float tan_half = std::tan(0.5f * kFov);
    const float mx = mouse.x - origin.x;
    const float my = mouse.y - origin.y;
    const float u_view = (2.0f * mx / fw - 1.0f) * aspect * tan_half;
    const float v_view = (1.0f - 2.0f * my / fh) * tan_half;
    float d[3] = {
        b.r[0] * u_view + b.u[0] * v_view + b.f[0],
        b.r[1] * u_view + b.u[1] * v_view + b.f[1],
        b.r[2] * u_view + b.u[2] * v_view + b.f[2],
    };
    const float len = std::sqrt(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
    out_dir[0] = d[0] / len;
    out_dir[1] = d[1] / len;
    out_dir[2] = d[2] / len;
}

bool closest_axis_param(const float p0[3], const float a[3],
                        const float o[3], const float d[3], float& t) {
    const float w0[3] = { p0[0]-o[0], p0[1]-o[1], p0[2]-o[2] };
    const float B = a[0]*d[0] + a[1]*d[1] + a[2]*d[2];
    const float D = a[0]*w0[0] + a[1]*w0[1] + a[2]*w0[2];
    const float E = d[0]*w0[0] + d[1]*w0[1] + d[2]*w0[2];
    const float denom = 1.0f - B * B;
    if (std::fabs(denom) < 1e-5f) return false;
    t = (B * E - D) / denom;
    return true;
}

bool plane_angle(const float c[3], int axis,
                 const float o[3], const float d[3], float& angle) {
    const float* n = kAxisPreview[axis];
    const float dn = d[0]*n[0] + d[1]*n[1] + d[2]*n[2];
    if (std::fabs(dn) < 1e-5f) return false;
    const float w[3] = { c[0]-o[0], c[1]-o[1], c[2]-o[2] };
    const float t = (w[0]*n[0] + w[1]*n[1] + w[2]*n[2]) / dn;
    if (t <= 0.0f) return false;
    const float hit[3] = { o[0]+d[0]*t - c[0],
                           o[1]+d[1]*t - c[1],
                           o[2]+d[2]*t - c[2] };
    const float* b1 = kPlaneB1[axis];
    const float* b2 = kPlaneB2[axis];
    const float x = hit[0]*b1[0] + hit[1]*b1[1] + hit[2]*b1[2];
    const float y = hit[0]*b2[0] + hit[1]*b2[1] + hit[2]*b2[2];
    if (std::fabs(x) < 1e-6f && std::fabs(y) < 1e-6f) return false;
    angle = std::atan2(y, x);
    return true;
}

float dist_point_segment(const ImVec2& p, const ImVec2& a, const ImVec2& b) {
    const float abx = b.x - a.x, aby = b.y - a.y;
    const float apx = p.x - a.x, apy = p.y - a.y;
    const float ab2 = abx*abx + aby*aby;
    float t = ab2 > 1e-6f ? (apx*abx + apy*aby) / ab2 : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    const float dx = apx - abx * t, dy = apy - aby * t;
    return std::sqrt(dx*dx + dy*dy);
}

const ImU32 kCol[3] = {
    IM_COL32(226, 61, 61, 255),
    IM_COL32(96, 200, 96, 255),
    IM_COL32(66, 133, 244, 255),
};
const ImU32 kColHot[3] = {
    IM_COL32(255, 120, 120, 255),
    IM_COL32(150, 255, 150, 255),
    IM_COL32(130, 190, 255, 255),
};
const ImU32 kGrey = IM_COL32(150, 150, 150, 160);

}

Mode GetMode() { return mode_ref(); }

void SetMode(Mode m) {
    if (mode_ref() != m) {
        mode_ref() = m;
        drag().active = false;
        drag().axis = -1;
        drag().hover_axis = -1;
    }
}

bool WantsMouse() {
    return drag().active || drag().hover_axis >= 0;
}

void CancelDrag() {
    drag().active = false;
    drag().axis = -1;
    drag().hover_axis = -1;
}

Result DrawAndHandle(const FlyCam& cam,
                     const ImVec2& origin,
                     const ImVec2& region,
                     const float engine_pos[3],
                     bool editable) {
    Result res;
    DragState& d = drag();
    const Mode mode = mode_ref();
    const CamBasis b = basis_of(cam);

    const float wp[3] = { engine_pos[0], engine_pos[2], engine_pos[1] };

    const float dxc = wp[0] - cam.pos[0];
    const float dyc = wp[1] - cam.pos[1];
    const float dzc = wp[2] - cam.pos[2];
    const float dist = std::sqrt(dxc*dxc + dyc*dyc + dzc*dzc);
    const float axis_len = std::max(0.3f, dist * 0.16f);

    ImVec2 base_px;
    if (!project(cam, b, origin, region, wp, base_px)) {
        d.hover_axis = -1;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) d.active = false;
        return res;
    }

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImVec2 tip_px[3];
    bool   tip_ok[3] = {false, false, false};
    ImVec2 circ_px[3][kCircleSegs];
    bool   circ_ok[3] = {false, false, false};

    if (mode == Mode::Rotate) {
        for (int i = 0; i < 3; ++i) {
            circ_ok[i] = true;
            for (int s = 0; s < kCircleSegs; ++s) {
                const float a = (float)s / kCircleSegs * 6.2831853f;
                const float ca = std::cos(a), sa = std::sin(a);
                const float p[3] = {
                    wp[0] + axis_len * (kPlaneB1[i][0]*ca + kPlaneB2[i][0]*sa),
                    wp[1] + axis_len * (kPlaneB1[i][1]*ca + kPlaneB2[i][1]*sa),
                    wp[2] + axis_len * (kPlaneB1[i][2]*ca + kPlaneB2[i][2]*sa),
                };
                if (!project(cam, b, origin, region, p, circ_px[i][s])) {
                    circ_ok[i] = false;
                    break;
                }
            }
        }
    } else {
        for (int i = 0; i < 3; ++i) {
            const float tip[3] = {
                wp[0] + kAxisPreview[i][0] * axis_len,
                wp[1] + kAxisPreview[i][1] * axis_len,
                wp[2] + kAxisPreview[i][2] * axis_len,
            };
            tip_ok[i] = project(cam, b, origin, region, tip, tip_px[i]);
        }
    }

    int hover = -1;
    if (editable && !d.active) {
        float best = kHitPx;
        if (mode == Mode::Rotate) {
            for (int i = 0; i < 3; ++i) {
                if (!circ_ok[i]) continue;
                for (int s = 0; s < kCircleSegs; ++s) {
                    const ImVec2& a = circ_px[i][s];
                    const ImVec2& c = circ_px[i][(s + 1) % kCircleSegs];
                    const float dd = dist_point_segment(mouse, a, c);
                    if (dd < best) { best = dd; hover = i; }
                }
            }
        } else {
            for (int i = 0; i < 3; ++i) {
                if (!tip_ok[i]) continue;
                const float dd = dist_point_segment(mouse, base_px,
                                                    tip_px[i]);
                if (dd < best) { best = dd; hover = i; }
            }
        }
    }
    d.hover_axis = d.active ? d.axis : hover;

    if (editable && !d.active && hover >= 0 &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        float dir[3];
        mouse_ray(cam, b, mouse, origin, region, dir);
        if (mode == Mode::Rotate) {
            float ang = 0.0f;
            if (plane_angle(wp, hover, cam.pos, dir, ang)) {
                d.active = true;
                d.axis = hover;
                d.mode = mode;
                d.angle_prev = ang;
                d.axis_origin[0] = wp[0];
                d.axis_origin[1] = wp[1];
                d.axis_origin[2] = wp[2];
            }
        } else {
            float t = 0.0f;
            if (closest_axis_param(wp, kAxisPreview[hover],
                                   cam.pos, dir, t)) {
                d.active = true;
                d.axis = hover;
                d.mode = mode;
                d.t_prev = t;
                d.t_start = t;
                d.scale_applied = 1.0f;
                d.axis_origin[0] = wp[0];
                d.axis_origin[1] = wp[1];
                d.axis_origin[2] = wp[2];
            }
        }
    } else if (d.active) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || d.mode != mode) {
            d.active = false;
            d.axis = -1;
        } else {
            float dir[3];
            mouse_ray(cam, b, mouse, origin, region, dir);
            if (mode == Mode::Rotate) {
                float ang = 0.0f;
                if (plane_angle(d.axis_origin, d.axis, cam.pos, dir, ang)) {
                    float delta = ang - d.angle_prev;
                    while (delta > 3.14159265f)  delta -= 6.2831853f;
                    while (delta < -3.14159265f) delta += 6.2831853f;
                    if (delta != 0.0f) {
                        d.angle_prev = ang;
                        res.rot_step_deg[d.axis] =
                            delta * 57.29577951f;
                        res.moved = true;
                    }
                }
            } else if (mode == Mode::Scale) {
                float t = 0.0f;
                if (closest_axis_param(d.axis_origin,
                                       kAxisPreview[d.axis],
                                       cam.pos, dir, t)) {
                    float target = 1.0f + (t - d.t_start) / axis_len;
                    target = std::clamp(target, 0.05f, 40.0f);
                    if (target != d.scale_applied &&
                        d.scale_applied > 1e-6f) {
                        res.scale_step = target / d.scale_applied;
                        d.scale_applied = target;
                        res.moved = true;
                    }
                }
            } else {
                float t = 0.0f;
                if (closest_axis_param(d.axis_origin,
                                       kAxisPreview[d.axis],
                                       cam.pos, dir, t)) {
                    float step = t - d.t_prev;
                    step = std::clamp(step, -200.0f, 200.0f);
                    if (step != 0.0f) {
                        d.t_prev = t;
                        const float* a = kAxisPreview[d.axis];
                        res.step[0] = a[0] * step;
                        res.step[1] = a[2] * step;
                        res.step[2] = a[1] * step;
                        res.moved = true;
                    }
                }
            }
            res.dragging = true;
        }
    }
    res.hovered = d.hover_axis >= 0;

    if (mode == Mode::Rotate) {
        for (int i = 0; i < 3; ++i) {
            if (!circ_ok[i]) continue;
            const bool hot = editable && d.hover_axis == i;
            const ImU32 col = !editable ? kGrey
                                        : (hot ? kColHot[i] : kCol[i]);
            for (int s = 0; s < kCircleSegs; ++s) {
                dl->AddLine(circ_px[i][s],
                            circ_px[i][(s + 1) % kCircleSegs],
                            col, hot ? 3.5f : 2.0f);
            }
        }
    } else {
        for (int i = 0; i < 3; ++i) {
            if (!tip_ok[i]) continue;
            const bool hot = editable && d.hover_axis == i;
            const ImU32 col = !editable ? kGrey
                                        : (hot ? kColHot[i] : kCol[i]);
            dl->AddLine(base_px, tip_px[i], col, hot ? 4.5f : 3.0f);

            ImVec2 dir2 = ImVec2(tip_px[i].x - base_px.x,
                                 tip_px[i].y - base_px.y);
            const float len = std::sqrt(dir2.x*dir2.x + dir2.y*dir2.y);
            if (len > 1e-3f) {
                dir2.x /= len; dir2.y /= len;
                const ImVec2 n(-dir2.y, dir2.x);
                if (mode == Mode::Scale) {
                    const float hs = hot ? 7.0f : 5.5f;
                    const ImVec2& t2 = tip_px[i];
                    dl->AddRectFilled(ImVec2(t2.x - hs, t2.y - hs),
                                      ImVec2(t2.x + hs, t2.y + hs), col);
                } else {
                    const float ah = hot ? 13.0f : 10.0f;
                    const float aw = hot ? 6.5f : 5.0f;
                    const ImVec2 t2(tip_px[i].x + dir2.x * ah,
                                    tip_px[i].y + dir2.y * ah);
                    const ImVec2 wl(tip_px[i].x + n.x * aw,
                                    tip_px[i].y + n.y * aw);
                    const ImVec2 wr(tip_px[i].x - n.x * aw,
                                    tip_px[i].y - n.y * aw);
                    dl->AddTriangleFilled(t2, wl, wr, col);
                }
            }
        }
    }
    dl->AddCircleFilled(base_px, 4.0f,
                        editable ? IM_COL32(255, 255, 160, 255) : kGrey);

    return res;
}

}
