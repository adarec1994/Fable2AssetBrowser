#pragma once

#include "imgui.h"

struct FlyCam;

namespace LevelGizmo {

enum class Mode { Translate, Rotate, Scale };

struct Result {
    bool hovered  = false;
    bool dragging = false;
    bool moved    = false;
    float step[3] = {0, 0, 0};
    float rot_step_deg[3] = {0, 0, 0};
    float scale_step = 1.0f;
};

Mode GetMode();
void SetMode(Mode m);

Result DrawAndHandle(const FlyCam& cam,
                     const ImVec2& origin,
                     const ImVec2& region,
                     const float engine_pos[3],
                     bool editable);

bool WantsMouse();

void CancelDrag();

}
