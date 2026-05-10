#include "IconButton.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <cstring>

namespace UI {

bool icon_button(const char* id, const char* icon_glyph, float diameter,
                 bool primary, bool active,
                 float optical_dx_pct) {
    ImVec2 sz(diameter, diameter);
    ImGui::InvisibleButton(id, sz);
    ImVec2 rmin = ImGui::GetItemRectMin();
    ImVec2 rmax = ImGui::GetItemRectMax();
    bool hovered = ImGui::IsItemHovered();
    bool down    = ImGui::IsItemActive();
    bool clicked = ImGui::IsItemClicked();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c((rmin.x + rmax.x) * 0.5f, (rmin.y + rmax.y) * 0.5f);

    ImU32 bg;
    ImU32 fg;
    if (primary) {
        bg = down    ? IM_COL32( 90, 170, 230, 255)
           : hovered ? IM_COL32(140, 210, 255, 255)
                     : IM_COL32(120, 200, 255, 255);
        fg = IM_COL32(15, 20, 25, 255);
    } else {
        bg = down    ? IM_COL32( 60,  64,  74, 255)
           : hovered ? IM_COL32( 80,  84,  94, 255)
                     : IM_COL32( 50,  54,  62, 255);
        fg = active ? IM_COL32(120, 200, 255, 255)
                    : IM_COL32(220, 225, 235, 255);
    }

    dl->AddCircleFilled(c, diameter * 0.5f, bg, 36);

    // Render the icon at a size proportional to the button. Use the
    // glyph's *visible* bounding box (not its advance width) so glyphs
    // with asymmetric whitespace land their actual ink at the centre.
    ImFont* font = ImGui::GetFont();
    float icon_size = diameter * 0.46f;
    ImVec2 advance_sz = font->CalcTextSizeA(icon_size, FLT_MAX, 0.0f, icon_glyph);

    unsigned int cp = 0;
    ImTextCharFromUtf8(&cp, icon_glyph, icon_glyph + std::strlen(icon_glyph));
    const ImFontGlyph* g = font->FindGlyph((ImWchar)cp);

    float pos_x;
    if (g) {
        float scale = icon_size / font->FontSize;
        float vx0 = g->X0 * scale;
        float vx1 = g->X1 * scale;
        float visible_centre = 0.5f * (vx0 + vx1);
        pos_x = c.x - visible_centre;
    } else {
        pos_x = c.x - advance_sz.x * 0.5f;
    }
    pos_x += advance_sz.x * optical_dx_pct;

    float pos_y;
    if (g) {
        float scale = icon_size / font->FontSize;
        float vy0 = g->Y0 * scale;
        float vy1 = g->Y1 * scale;
        pos_y = c.y - 0.5f * (vy0 + vy1);
    } else {
        pos_y = c.y - advance_sz.y * 0.5f;
    }

    dl->AddText(font, icon_size, ImVec2(pos_x, pos_y), fg, icon_glyph);
    return clicked;
}

} // namespace UI
