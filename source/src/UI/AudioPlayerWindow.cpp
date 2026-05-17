#include "AudioPlayerWindow.h"
#include "../Audio/AudioPlayer.h"
#include "../Splashscreen/IconFont.h"
#include "IconButton.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "IconsFontAwesome6.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

extern void show_error_box(const std::string&);

namespace UI {

namespace {

bool g_window_visible = false;

constexpr float kWindowW = 560.0f;
constexpr float kWindowH = 340.0f;

void format_time(double sec, char* out, size_t cap) {
    if (sec < 0.0) sec = 0.0;
    int total_ms = (int)(sec * 1000.0);
    int m  = total_ms / 60000;
    int s  = (total_ms / 1000) % 60;
    int cs = (total_ms % 1000) / 10;
    std::snprintf(out, cap, "%02d:%02d.%02d", m, s, cs);
}

void draw_waveform(ImVec2 size, float progress) {
    ImGui::InvisibleButton("##waveform", size);
    ImVec2 rmin = ImGui::GetItemRectMin();
    ImVec2 rmax = ImGui::GetItemRectMax();
    bool active = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(rmin, rmax, IM_COL32(20, 22, 28, 255), 6.0f);

    static thread_local std::vector<float> lo, hi;
    lo.resize(1024);
    hi.resize(1024);
    int n = AudioPlayer::get_waveform_peaks(lo.data(), hi.data(), 1024);
    if (n > 0) {
        float w  = rmax.x - rmin.x;
        float h  = rmax.y - rmin.y;
        float cy = rmin.y + h * 0.5f;
        float playhead_x = rmin.x + w * progress;
        const ImU32 col_played   = IM_COL32(120, 200, 255, 235);
        const ImU32 col_unplayed = IM_COL32( 90, 100, 120, 200);
        int cols = (int)w; if (cols < 4) cols = 4;
        for (int x = 0; x < cols; ++x) {
            int slot = (int)((float)x * (float)n / (float)cols);
            if (slot >= n) slot = n - 1;
            float l = lo[slot], u = hi[slot];
            float top = cy - u * (h * 0.45f);
            float bot = cy - l * (h * 0.45f);
            if (bot < top) std::swap(top, bot);
            if (bot - top < 1.5f) {
                float m = 0.5f * (top + bot); top = m - 0.75f; bot = m + 0.75f;
            }
            float px = rmin.x + (float)x;
            ImU32 col = (px <= playhead_x) ? col_played : col_unplayed;
            dl->AddLine(ImVec2(px, top), ImVec2(px, bot), col, 1.5f);
        }
        dl->AddLine(ImVec2(playhead_x, rmin.y + 2),
                    ImVec2(playhead_x, rmax.y - 2),
                    IM_COL32(255, 255, 255, 230), 1.5f);
    }
    if (active) {
        float mx = ImGui::GetIO().MousePos.x;
        float t = (mx - rmin.x) / (rmax.x - rmin.x);
        if (t < 0) t = 0; if (t > 1) t = 1;
        AudioPlayer::seek_fraction(t);
    }
}

float draw_seek_bar(float frac, float width) {
    const float bar_h    = 6.0f;
    const float thumb_r  = 6.0f;
    const float row_h    = thumb_r * 2.0f + 4.0f;
    ImGui::InvisibleButton("##seek", ImVec2(width, row_h));
    ImVec2 rmin = ImGui::GetItemRectMin();
    ImVec2 rmax = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    bool hovered = ImGui::IsItemHovered();
    bool active  = ImGui::IsItemActive();
    float bar_y = rmin.y + (row_h - bar_h) * 0.5f;
    ImVec2 bar_min(rmin.x, bar_y);
    ImVec2 bar_max(rmax.x, bar_y + bar_h);
    float fill_x = rmin.x + (rmax.x - rmin.x) * frac;
    dl->AddRectFilled(bar_min, bar_max, IM_COL32(50, 55, 65, 255), 3.0f);
    dl->AddRectFilled(bar_min, ImVec2(fill_x, bar_max.y),
                      IM_COL32(120, 200, 255, 255), 3.0f);
    if (hovered || active) {
        dl->AddCircleFilled(ImVec2(fill_x, bar_y + bar_h * 0.5f),
                            thumb_r, IM_COL32(255, 255, 255, 240));
    }
    if (active) {
        float mx = ImGui::GetIO().MousePos.x;
        float t = (mx - rmin.x) / (rmax.x - rmin.x);
        if (t < 0) t = 0; if (t > 1) t = 1;
        return t;
    }
    return -1.0f;
}

void draw_volume_control(float total_w) {
    static float last_nonzero_volume = 0.85f;
    float vol = AudioPlayer::get_volume();
    if (vol > 0.001f) last_nonzero_volume = vol;

    const char* spk_icon = (vol < 0.01f) ? ICON_FA_VOLUME_XMARK : ICON_FA_VOLUME_HIGH;

    ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(255, 255, 255, 25));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(255, 255, 255, 50));
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
    if (ImGui::Button(spk_icon, ImVec2(28, 24))) {
        AudioPlayer::set_volume(vol < 0.01f ? last_nonzero_volume : 0.0f);
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);

    ImGui::SameLine(0, 6);

    ImGui::PushStyleColor(ImGuiCol_FrameBg,        IM_COL32(40, 44, 52, 255));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(50, 54, 62, 255));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  IM_COL32(60, 64, 72, 255));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab,        IM_COL32(220, 225, 235, 255));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,  IM_COL32(255, 255, 255, 255));
    ImGui::SetNextItemWidth(total_w - 28 - 6);
    if (ImGui::SliderFloat("##vol", &vol, 0.0f, 1.0f, "")) {
        AudioPlayer::set_volume(vol);
    }
    ImGui::PopStyleColor(5);
}

}

bool open_audio_player_for(const std::string& display_name,
                           const std::vector<unsigned char>& bytes) {
    AudioPlayer::start_load_bytes_async(
        std::vector<uint8_t>(bytes.begin(), bytes.end()),
        display_name);
    g_window_visible = true;
    return true;
}

void draw_audio_player_window() {
    if (!g_window_visible) return;
    if (!AudioPlayer::has_source() && !AudioPlayer::is_loading()) {
        g_window_visible = false;
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(kWindowW, kWindowH), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,  ImVec2(16, 14));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(28, 30, 36, 250));

    bool open = true;
    if (ImGui::Begin("Now Playing###audio_player_window", &open,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize
                     | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
                     | ImGuiWindowFlags_NoSavedSettings)) {

        const float content_w = ImGui::GetContentRegionAvail().x;

        std::string name = AudioPlayer::get_display_name();
        if (name.empty()) name = "(audio)";
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(235, 240, 250, 255));
        ImGui::TextUnformatted(name.c_str());
        ImGui::PopStyleColor();

        int sr = AudioPlayer::get_sample_rate();
        int ch = AudioPlayer::get_channels();
        const char* ch_label = (ch == 1) ? "mono" : (ch == 2 ? "stereo" : "");
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(140, 150, 165, 255));
        if (AudioPlayer::is_loading() && !AudioPlayer::has_source()) {
            ImGui::TextUnformatted("Decoding XMA \xe2\x80\xa6");
        } else {
            ImGui::Text("%d Hz \xc2\xb7 %s%s%d ch \xc2\xb7 XMA2",
                        sr, ch_label, ch_label[0] ? " \xc2\xb7 " : "", ch);
        }
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0, 6));

        double dur = AudioPlayer::get_duration_sec();
        double now = AudioPlayer::get_time_sec();
        float frac = (dur > 0.0) ? (float)(now / dur) : 0.0f;
        if (frac < 0) frac = 0; if (frac > 1) frac = 1;
        const float wf_h = 110.0f;
        draw_waveform(ImVec2(content_w, wf_h), frac);

        ImGui::Dummy(ImVec2(0, 6));

        float seek_in = draw_seek_bar(frac, content_w);
        if (seek_in >= 0.0f) AudioPlayer::seek_fraction(seek_in);

        char t_now[16], t_dur[16];
        format_time(now, t_now, sizeof(t_now));
        format_time(dur, t_dur, sizeof(t_dur));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(160, 170, 185, 255));
        ImGui::TextUnformatted(t_now);
        ImVec2 dur_sz = ImGui::CalcTextSize(t_dur);
        ImGui::SameLine(content_w - dur_sz.x);
        ImGui::TextUnformatted(t_dur);
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0, 6));

        const float btn_lg   = 44.0f;
        const float btn_sm   = 32.0f;
        const float gap      = 14.0f;
        const float vol_w    = 170.0f;
        const float row_y    = ImGui::GetCursorPosY();
        const float group_w  = btn_sm + gap + btn_lg + gap + btn_sm;
        const float group_x  = (content_w - group_w) * 0.5f;
        const float sm_y     = row_y + (btn_lg - btn_sm) * 0.5f;
        const float vol_y    = row_y + (btn_lg - 24.0f) * 0.5f;

        ImGui::SetCursorPos(ImVec2(group_x, sm_y));
        if (icon_button("##stop", ICON_FA_STOP, btn_sm, false)) {
            AudioPlayer::stop();
        }

        ImGui::SetCursorPos(ImVec2(group_x + btn_sm + gap, row_y));
        bool playing = AudioPlayer::is_playing();
        const char* play_glyph = playing ? ICON_FA_PAUSE : ICON_FA_PLAY;
        float play_dx = playing ? 0.0f : 0.17f;
        if (icon_button("##play", play_glyph, btn_lg, true, false, play_dx)) {
            AudioPlayer::toggle_play();
        }

        ImGui::SetCursorPos(ImVec2(group_x + btn_sm + gap + btn_lg + gap, sm_y));
        bool loop = AudioPlayer::get_loop();
        if (icon_button("##loop", ICON_FA_REPEAT, btn_sm, false, loop)) {
            AudioPlayer::set_loop(!loop);
        }

        ImGui::SetCursorPos(ImVec2(content_w - vol_w, vol_y));
        draw_volume_control(vol_w);
    }
    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    if (!open) {
        AudioPlayer::stop();
        g_window_visible = false;
    }
}

}
