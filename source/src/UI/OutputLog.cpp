#include "OutputLog.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "IconsFontAwesome6.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <vector>

namespace OutputLog {

namespace {

struct Entry {
    Level       lvl;
    std::string msg;
    char        time_str[12];
};

std::vector<Entry> g_entries;
std::mutex         g_mutex;
constexpr size_t   kMaxEntries = 500;

bool   g_open      = false;
bool   g_locked    = false;

float  g_anim_h    = 0.0f;
constexpr float kBarHeight   = 26.0f;
constexpr float kPanelHeight = 220.0f;

void format_time(char* out, size_t n) {
    using namespace std::chrono;
    auto now = system_clock::to_time_t(system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    std::snprintf(out, n, "%02d:%02d:%02d",
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
}

ImVec4 colour_for(Level lvl) {
    switch (lvl) {
        case Level::Success: return ImVec4(0.55f, 1.00f, 0.55f, 1.00f);
        case Level::Warn:    return ImVec4(1.00f, 0.85f, 0.40f, 1.00f);
        case Level::Error:   return ImVec4(1.00f, 0.50f, 0.50f, 1.00f);
        case Level::Info:
        default:             return ImVec4(0.85f, 0.88f, 0.92f, 1.00f);
    }
}

}

void log(Level lvl, std::string msg) {
    Entry e;
    e.lvl = lvl;
    e.msg = std::move(msg);
    format_time(e.time_str, sizeof(e.time_str));

    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_entries.size() >= kMaxEntries) {

        const size_t drop = g_entries.size() - kMaxEntries + 1;
        g_entries.erase(g_entries.begin(), g_entries.begin() + (std::ptrdiff_t)drop);
    }
    g_entries.push_back(std::move(e));
}

float reserved_bottom_height() {

    return g_anim_h > kBarHeight ? g_anim_h : kBarHeight;
}

void draw() {

    const float target = g_open ? (kBarHeight + kPanelHeight) : kBarHeight;
    g_anim_h += (target - g_anim_h) * 0.20f;
    if (std::fabs(g_anim_h - target) < 0.5f) g_anim_h = target;
    if (g_anim_h < kBarHeight) g_anim_h = kBarHeight;

    ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 win_pos (vp->WorkPos.x,
                          vp->WorkPos.y + vp->WorkSize.y - g_anim_h);
    const ImVec2 win_size(vp->WorkSize.x, g_anim_h);

    ImGui::SetNextWindowPos (win_pos);
    ImGui::SetNextWindowSize(win_size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,  ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    ImGuiWindowFlags fl = ImGuiWindowFlags_NoTitleBar
                        | ImGuiWindowFlags_NoResize
                        | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_NoCollapse
                        | ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("##output_log", nullptr, fl)) {

        const float bar_h          = kBarHeight - 1.0f;
        const float icon_w         = 32.0f;
        const float lock_w         = 32.0f;
        const float right_margin   = 14.0f;
        const float preview_w =
            win_size.x - icon_w - lock_w - right_margin;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 bar_p0 = ImGui::GetCursorScreenPos();
        ImVec2 bar_p1 = ImVec2(bar_p0.x + win_size.x, bar_p0.y + bar_h);
        dl->AddRectFilled(bar_p0, bar_p1, IM_COL32(14, 16, 20, 255));

        std::string preview_msg;
        Level       preview_lvl   = Level::Info;
        const char* preview_time  = nullptr;
        size_t      entry_count   = 0;
        Entry       last_copy{};
        {
            std::lock_guard<std::mutex> lk(g_mutex);
            entry_count = g_entries.size();
            if (entry_count > 0) {
                last_copy   = g_entries.back();
                preview_msg = last_copy.msg;
                preview_lvl = last_copy.lvl;
                preview_time = last_copy.time_str;
            }
        }

        auto draw_centred_glyph = [&](float x0, float w,
                                      const char* glyph,
                                      ImU32 colour) {
            ImVec2 ts = ImGui::CalcTextSize(glyph);
            float gx = x0 + (w - ts.x) * 0.5f;
            float gy = bar_p0.y + (bar_h - ts.y) * 0.5f;
            dl->AddText(ImVec2(gx, gy), colour, glyph);
        };

        auto draw_btn_bg = [&](float x0, float w, bool hovered, bool active) {
            ImU32 bg = hovered ? (active ? IM_COL32(48, 56, 66, 255)
                                         : IM_COL32(34, 40, 48, 255))
                               : 0;
            if (bg) {
                dl->AddRectFilled(ImVec2(x0, bar_p0.y),
                                  ImVec2(x0 + w, bar_p0.y + bar_h),
                                  bg);
            }
        };

        const float chevron_x0 = bar_p0.x;
        ImGui::SetCursorScreenPos(ImVec2(chevron_x0, bar_p0.y));
        if (ImGui::InvisibleButton("##ol_chevron", ImVec2(icon_w, bar_h))) {
            if (!g_locked) g_open = !g_open;
        }
        if (!g_locked) {
            draw_btn_bg(chevron_x0, icon_w,
                        ImGui::IsItemHovered(), ImGui::IsItemActive());
        }
        draw_centred_glyph(chevron_x0, icon_w,
                           g_open ? ICON_FA_CHEVRON_DOWN : ICON_FA_CHEVRON_UP,
                           g_locked ? IM_COL32(110, 120, 135, 255)
                                    : IM_COL32(200, 215, 230, 255));

        const float prev_x0 = chevron_x0 + icon_w;
        ImGui::SetCursorScreenPos(ImVec2(prev_x0, bar_p0.y));
        if (ImGui::InvisibleButton("##ol_preview", ImVec2(preview_w, bar_h))) {
            if (!g_locked) g_open = !g_open;
        }

        if (!g_locked && ImGui::IsItemHovered()) {
            dl->AddRectFilled(ImVec2(prev_x0, bar_p0.y),
                              ImVec2(prev_x0 + preview_w, bar_p0.y + bar_h),
                              ImGui::IsItemActive()
                                  ? IM_COL32(255, 255, 255, 24)
                                  : IM_COL32(255, 255, 255, 12));
        }

        const float pad_x  = 10.0f;
        const float text_y = bar_p0.y + (bar_h - ImGui::GetTextLineHeight()) * 0.5f;
        if (g_open) {
            dl->AddText(ImVec2(prev_x0 + pad_x, text_y),
                        IM_COL32(150, 160, 175, 255), "Output Log");
        } else if (entry_count == 0) {
            dl->AddText(ImVec2(prev_x0 + pad_x, text_y),
                        IM_COL32(120, 130, 145, 255), "Output Log");
        } else {

            char count_str[32];
            std::snprintf(count_str, sizeof(count_str), "(%zu)", entry_count);
            float right_w = ImGui::CalcTextSize(count_str).x;

            char ts[16];
            std::snprintf(ts, sizeof(ts), "[%s]", preview_time ? preview_time : "");
            float ts_w = ImGui::CalcTextSize(ts).x;

            float cursor_x = prev_x0 + pad_x;
            dl->AddText(ImVec2(cursor_x, text_y),
                        IM_COL32(130, 140, 155, 255), ts);
            cursor_x += ts_w + 8.0f;

            ImU32  col = ImGui::ColorConvertFloat4ToU32(colour_for(preview_lvl));
            float msg_max_x = prev_x0 + preview_w - pad_x - right_w - 8.0f;
            ImVec4 clip(cursor_x, text_y, msg_max_x,
                        text_y + ImGui::GetTextLineHeight());
            dl->AddText(nullptr, 0.0f,
                        ImVec2(cursor_x, text_y), col,
                        preview_msg.c_str(), nullptr, 0.0f, &clip);

            dl->AddText(ImVec2(prev_x0 + preview_w - pad_x - right_w, text_y),
                        IM_COL32(120, 130, 145, 255), count_str);
        }

        const float lock_x0 = prev_x0 + preview_w;
        ImGui::SetCursorScreenPos(ImVec2(lock_x0, bar_p0.y));
        if (ImGui::InvisibleButton("##ol_lock", ImVec2(lock_w, bar_h))) {
            g_locked = !g_locked;
        }
        bool lock_hovered = ImGui::IsItemHovered();
        draw_btn_bg(lock_x0, lock_w, lock_hovered, ImGui::IsItemActive());

        ImU32 lock_colour = g_locked ? IM_COL32(255, 210, 90, 255)
                                     : IM_COL32(200, 215, 230, 255);
        draw_centred_glyph(lock_x0, lock_w,
                           g_locked ? ICON_FA_LOCK : ICON_FA_LOCK_OPEN,
                           lock_colour);

        ImGui::SetCursorScreenPos(ImVec2(bar_p0.x, bar_p0.y + bar_h));

        if (g_anim_h > kBarHeight + 1.0f) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg,
                                  ImVec4(0.06f, 0.07f, 0.09f, 1.0f));
            ImGui::BeginChild("##log_content",
                              ImVec2(0, g_anim_h - kBarHeight),
                              false,
                              ImGuiWindowFlags_HorizontalScrollbar);

            /* Toolbar: "Copy All" + "Clear" so the user can grab the
               whole log in one shot, since per-row right-click only
               copies one line at a time. */
            {
                std::lock_guard<std::mutex> lk(g_mutex);
                if (ImGui::SmallButton("Copy All")) {
                    std::string blob;
                    blob.reserve(g_entries.size() * 64);
                    for (const auto& e : g_entries) {
                        blob.append("[");
                        blob.append(e.time_str);
                        blob.append("] ");
                        blob.append(e.msg);
                        blob.push_back('\n');
                    }
                    ImGui::SetClipboardText(blob.c_str());
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear")) {
                    g_entries.clear();
                }
            }
            ImGui::Separator();

            std::lock_guard<std::mutex> lk(g_mutex);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 2));
            /* Render each entry as a Selectable so the user can
               left-click to highlight, drag to select a range, and
               right-click for a Copy popup.  The colour-on-text is
               kept via PushStyleColor + Selectable's label-text. */
            for (size_t i = 0; i < g_entries.size(); ++i) {
                const auto& e = g_entries[i];
                ImGui::PushID((int)i);

                /* Build the visible line: "[hh:mm:ss] message". */
                std::string line = "[";
                line.append(e.time_str);
                line.append("] ");
                line.append(e.msg);

                ImGui::PushStyleColor(ImGuiCol_Text, colour_for(e.lvl));
                ImGui::Selectable(line.c_str(), false,
                                  ImGuiSelectableFlags_AllowDoubleClick);
                ImGui::PopStyleColor();

                /* Double-click anywhere on the line copies it. */
                if (ImGui::IsItemHovered() &&
                    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    ImGui::SetClipboardText(line.c_str());
                }

                /* Right-click context menu: "Copy line" / "Copy message". */
                if (ImGui::BeginPopupContextItem("##log_ctx")) {
                    if (ImGui::MenuItem("Copy line")) {
                        ImGui::SetClipboardText(line.c_str());
                    }
                    if (ImGui::MenuItem("Copy message only")) {
                        ImGui::SetClipboardText(e.msg.c_str());
                    }
                    ImGui::EndPopup();
                }

                ImGui::PopID();
            }
            ImGui::PopStyleVar();

            const bool near_bottom =
                ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 24.0f;
            if (near_bottom) ImGui::SetScrollHereY(1.0f);

            ImGui::EndChild();
            ImGui::PopStyleColor();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(3);

    if (g_open && !g_locked && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const ImVec2 mp = ImGui::GetIO().MousePos;
        const bool inside = mp.x >= win_pos.x && mp.x < win_pos.x + win_size.x &&
                            mp.y >= win_pos.y && mp.y < win_pos.y + win_size.y;
        if (!inside) {
            g_open = false;
        }
    }
}

}
