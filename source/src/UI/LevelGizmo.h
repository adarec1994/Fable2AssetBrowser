#pragma once

#include "imgui.h"

struct FlyCam;

namespace LevelGizmo {

struct Result {
    bool hovered  = false;
    bool dragging = false;
    bool moved    = false;
    float step[3] = {0, 0, 0};
};

Result DrawAndHandle(const FlyCam& cam,
                     const ImVec2& origin,
                     const ImVec2& region,
                     const float engine_pos[3],
                     bool editable);

bool WantsMouse();

void CancelDrag();

}
