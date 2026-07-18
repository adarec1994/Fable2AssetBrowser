void draw_exec_pin_triangle(const ImVec2& min, const ImVec2& max,
                            bool hovered) {
    const bool held = hovered &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const ImU32 colour = held ? IM_COL32(255, 235, 130, 255)
        : hovered ? IM_COL32(255, 210, 95, 255)
                  : IM_COL32(205, 225, 240, 255);
    const float inset_x = (max.x - min.x - 18.0f) * 0.5f;
    const float inset_y = (max.y - min.y - 18.0f) * 0.5f;
    ImGui::GetWindowDrawList()->AddTriangleFilled(
        ImVec2(min.x + inset_x + 4.0f, min.y + inset_y + 3.0f),
        ImVec2(max.x - inset_x - 3.0f, (min.y + max.y) * 0.5f),
        ImVec2(min.x + inset_x + 4.0f, max.y - inset_y - 3.0f),
        colour);
}

constexpr float kExecPinSize = 26.0f;

void draw_input_pin(int id, float content_x) {
    ImGui::SetCursorPosX(content_x);
    ImGui::Dummy(ImVec2(kExecPinSize, kExecPinSize));
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const ImVec2 pivot(min.x, (min.y + max.y) * 0.5f);
    ed::BeginPin(input_pin_id(id), ed::PinKind::Input);
    ed::PinRect(min, max);
    ed::PinPivotRect(pivot, pivot);
    ed::EndPin();
    const bool hovered = ImGui::IsMouseHoveringRect(min, max);
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    draw_exec_pin_triangle(min, max, hovered);
}

void draw_output_pin(int id, float content_x) {
    ImGui::SameLine();
    ImGui::SetCursorPosX(content_x + kNodeContentWidth - kExecPinSize);
    ImGui::Dummy(ImVec2(kExecPinSize, kExecPinSize));
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const ImVec2 pivot(max.x, (min.y + max.y) * 0.5f);
    ed::BeginPin(output_pin_id(id), ed::PinKind::Output);
    ed::PinRect(min, max);
    ed::PinPivotRect(pivot, pivot);
    ed::EndPin();
    const bool hovered = ImGui::IsMouseHoveringRect(min, max);
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    draw_exec_pin_triangle(min, max, hovered);
}
