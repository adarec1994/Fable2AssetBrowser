#pragma once
// UE5-style translate gizmo for the level viewer: three axis arrows drawn
// over the render image, draggable per axis. Axes are ENGINE axes (Z up),
// matching the selection transform overlay: X red, Y green, Z blue.

#include "imgui.h"

struct FlyCam;

namespace LevelGizmo {

struct Result {
    bool hovered  = false;   // mouse is over an axis handle
    bool dragging = false;   // an axis drag is in progress
    bool moved    = false;   // step[] holds a movement for this frame
    float step[3] = {0, 0, 0};   // engine-space delta for this frame
};

// Draw the gizmo at `engine_pos` (engine axes, Z up) and handle mouse
// interaction. `editable == false` renders it grey and inert.
Result DrawAndHandle(const FlyCam& cam,
                     const ImVec2& origin,
                     const ImVec2& region,
                     const float engine_pos[3],
                     bool editable);

// True while the gizmo wants the mouse (hover as of the last frame, or an
// active drag) — the caller suppresses click-picking and camera input.
bool WantsMouse();

// Drop any in-progress drag (selection changed / edit mode left).
void CancelDrag();

}  // namespace LevelGizmo
