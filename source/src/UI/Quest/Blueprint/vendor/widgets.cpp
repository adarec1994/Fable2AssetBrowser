# define IMGUI_DEFINE_MATH_OPERATORS
# include "widgets.h"
# include <imgui_internal.h>

void ax::Widgets::Icon(const ImVec2& size, IconType type, bool filled, const ImVec4& color, const ImVec4& innerColor)
{
    if (ImGui::IsRectVisible(size))
    {
        auto cursorPos = ImGui::GetCursorScreenPos();
        auto drawList  = ImGui::GetWindowDrawList();
        ax::Drawing::DrawIcon(drawList, cursorPos, cursorPos + size, type, filled, ImColor(color), ImColor(innerColor));
    }

    ImGui::Dummy(size);
}

