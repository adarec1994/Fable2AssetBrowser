#include "UI_Panels.h"
#include "State.h"
#include "Utils.h"
#include "Operations.h"
#include "HexView.h"
#include "UI_Main.h"
#include "TexParser.h"
#include "ModelParser.h"
#include "ModelPreview.h"
#include "BNKCore.cpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include "ImGuiFileDialog.h"
#include <filesystem>
#include <algorithm>
#include <thread>
#include <atomic>
#include "Progress.h"
#include "files.h"

static std::vector<GlobalHit> g_global_hits;
static std::atomic<bool> g_global_busy(false);
static std::atomic<bool> g_cancel_search(false);
static std::string g_last_global_search;
static int g_selected_global = -1;

void draw_left_panel() {
    ImGui::BeginChild("left_panel", ImVec2(360, 0), true);
    ImGui::SetNextItemWidth(-1);
    if (!S.bnk_paths.empty()) {
        ImGui::InputTextWithHint("##bnk_filter", "Filter", &S.bnk_filter);
    }
    ImGui::BeginChild("bnk_list", ImVec2(0, 0), false);
    auto paths = filtered_bnk_paths();
    for (auto &p: paths) {
        std::string label = std::filesystem::path(p).filename().string();
        if (ImGui::Selectable(label.c_str(), p == S.selected_bnk, ImGuiSelectableFlags_SpanAllColumns)) {
            S.global_search.clear();
            pick_bnk(p);
        }
        if (!S.hide_tooltips && ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(p.c_str());
            ImGui::EndTooltip();
        }
    }
    ImGui::EndChild();
    ImGui::EndChild();
}

void draw_file_table() {
    std::vector<int> vis;
    vis.reserve(S.files.size());
    for (size_t i = 0; i < S.files.size(); ++i)
        if (name_matches_filter(S.files[i].name, S.file_filter)) vis.push_back((int) i);

    ImGuiTable *tbl_ptr = nullptr;
    if (ImGui::BeginTable("files_table", 2,
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter |
                          ImGuiTableFlags_SizingStretchProp)) {
        tbl_ptr = ImGui::GetCurrentTable();
        ImGui::TableSetupColumn("File");
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin((int) vis.size());
        while (clipper.Step()) {
            for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
                int i = vis[r];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                bool selected = (i == S.selected_file_index);
                std::string base = std::filesystem::path(S.files[i].name).filename().string();
                if (ImGui::Selectable(base.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
                    S.selected_file_index = i;
                if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(S.files[i].name.c_str());
                    ImGui::EndTooltip();
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%u", S.files[i].size);
            }
        }
        clipper.End();
        ImGui::EndTable();
    }
    if (tbl_ptr) {
        ImRect r = tbl_ptr->OuterRect;
        ImU32 col = ImGui::GetColorU32(ImGui::GetStyleColorVec4(ImGuiCol_Border));
        ImGui::GetWindowDrawList()->AddRect(r.Min, r.Max, col, 8.0f, 0, 1.0f);
    }
}

void draw_global_results_table() {
    if (g_global_busy) {
        ImGui::TextUnformatted("Searching all BNKs...");
        return;
    }

    std::vector<int> vis;
    vis.reserve(g_global_hits.size());
    for (size_t i = 0; i < g_global_hits.size(); ++i) {
        if (name_matches_filter(g_global_hits[i].file_name, S.file_filter)) {
            vis.push_back((int)i);
        }
    }

    ImGuiTable *tbl_ptr = nullptr;
    if (ImGui::BeginTable("global_results_table", 3,
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter |
                          ImGuiTableFlags_SizingStretchProp)) {
        tbl_ptr = ImGui::GetCurrentTable();
        ImGui::TableSetupColumn("File");
        ImGui::TableSetupColumn("BNK", ImGuiTableColumnFlags_WidthFixed, 200.0f);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin((int)vis.size());
        while (clipper.Step()) {
            for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
                int i = vis[r];
                const auto& hit = g_global_hits[i];

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);

                bool selected = (i == g_selected_global);
                std::string base = std::filesystem::path(hit.file_name).filename().string();

                if (ImGui::Selectable((base + "##globalrow" + std::to_string(i)).c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
                    g_selected_global = i;
                    pick_bnk(hit.bnk_path);
                    for (size_t j = 0; j < S.files.size(); ++j) {
                        if (S.files[j].index == hit.index) {
                            S.selected_file_index = (int)j;
                            break;
                        }
                    }
                }

                if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(hit.file_name.c_str());
                    ImGui::EndTooltip();
                }

                ImGui::TableSetColumnIndex(1);
                std::string bnk_name = std::filesystem::path(hit.bnk_path).filename().string();
                ImGui::TextUnformatted(bnk_name.c_str());

                if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(hit.bnk_path.c_str());
                    ImGui::EndTooltip();
                }

                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%u", hit.size);
            }
        }
        clipper.End();
        ImGui::EndTable();
    }
    if (tbl_ptr) {
        ImRect r = tbl_ptr->OuterRect;
        ImU32 col = ImGui::GetColorU32(ImGui::GetStyleColorVec4(ImGuiCol_Border));
        ImGui::GetWindowDrawList()->AddRect(r.Min, r.Max, col, 8.0f, 0, 1.0f);
    }
}

void draw_folder_dialog() {
    ImVec2 vp = ImGui::GetMainViewport()->WorkSize;
    ImVec2 minSize(680, 440);
    ImVec2 maxSize(vp.x * 0.9f, vp.y * 0.9f);
    if (ImGuiFileDialog::Instance()->Display("PickDir", ImGuiWindowFlags_NoCollapse, minSize, maxSize)) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            std::string sel = ImGuiFileDialog::Instance()->GetCurrentPath();
            open_folder_logic(sel);
        }
        ImGuiFileDialog::Instance()->Close();
    }
}

void draw_right_panel(ID3D11Device* device) {
    ImGui::BeginChild("right_panel", ImVec2(0, 0), false);

    ImGui::BeginChild("extract_box", ImVec2(0, 100), true, ImGuiWindowFlags_NoScrollbar);
    ImGui::BeginGroup();
    ImGui::PushItemWidth(-1);

    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 0));

    ImGui::BeginGroup();
    if (ImGui::Button("Dump All Files")) {
        ImGui::OpenPopup("progress_win");
        if (!S.global_search.empty()) {
            on_dump_all_global(g_global_hits);
        } else {
            on_dump_all_raw();
        }
    }
    if (!S.hide_tooltips && ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        if (!S.global_search.empty()) {
            ImGui::TextUnformatted("DUMPS ALL FILTERED GLOBAL RESULTS");
        } else {
            ImGui::TextUnformatted("DUMPS ALL FILES IN THE CURRENT BANK");
        }
        ImGui::EndTooltip();
    }

    ImGui::SameLine();
    bool has_selection = (S.selected_file_index >= 0 && S.selected_file_index < (int)S.files.size());
    if (!has_selection) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Dump File")) {
        ImGui::OpenPopup("progress_win");
        on_extract_selected_raw();
    }
    if (!S.hide_tooltips && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted("Dump the selected file raw");
        ImGui::EndTooltip();
    }
    if (!has_selection) {
        ImGui::EndDisabled();
    }

    bool has_wav_files = false;
    if (!S.global_search.empty()) {
        for (const auto& h : g_global_hits) {
            if (is_audio_file(h.file_name)) {
                has_wav_files = true;
                break;
            }
        }
    } else {
        has_wav_files = any_wav_in_bnk();
    }

    ImGui::SameLine();
    if (has_wav_files) {
        if (ImGui::Button("Export WAV's")) {
            ImGui::OpenPopup("progress_win");
            if (!S.global_search.empty()) {
                on_export_wavs_global(g_global_hits);
            } else {
                on_export_wavs();
            }
        }
    }
    if (has_wav_files && !S.hide_tooltips && ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted("Convert and export only the .wav files");
        ImGui::EndTooltip();
    }

    bool has_tex_files = false;
    if (!S.global_search.empty()) {
        for (const auto& h : g_global_hits) {
            if (is_tex_file(h.file_name)) {
                has_tex_files = true;
                break;
            }
        }
    } else {
        has_tex_files = is_texture_bnk_selected() && any_tex_in_bnk();
    }

    if (has_tex_files) {
        ImGui::SameLine();
        if (ImGui::Button("Rebuild and Extract All (.tex)")) {
            ImGui::OpenPopup("progress_win");
            if (!S.global_search.empty()) {
                on_rebuild_and_extract_global_tex(g_global_hits);
            } else {
                on_rebuild_and_extract();
            }
        }
        if (!S.hide_tooltips && ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Rebuilds every .tex file bitstream");
            ImGui::EndTooltip();
        }
    }

    bool has_mdl_files = false;
    if (!S.global_search.empty()) {
        for (const auto& h : g_global_hits) {
            if (is_mdl_file(h.file_name)) {
                has_mdl_files = true;
                break;
            }
        }
    } else {
        has_mdl_files = is_model_bnk_selected() && any_mdl_in_bnk();
    }

    if (has_mdl_files) {
        ImGui::SameLine();
        if (ImGui::Button("Rebuild and Extract All (.mdl)")) {
            ImGui::OpenPopup("progress_win");
            if (!S.global_search.empty()) {
                on_rebuild_and_extract_global_mdl(g_global_hits);
            } else {
                on_rebuild_and_extract_models();
            }
        }
        if (!S.hide_tooltips && ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Rebuilds every .mdl file bitstream");
            ImGui::EndTooltip();
        }
    }

    if (S.selected_file_index >= 0) {
        bool can_wav = false;
        if (S.selected_file_index >= 0 && S.selected_file_index < (int) S.files.size()) {
            std::string n = S.files[(size_t) S.selected_file_index].name;
            std::string l = n;
            std::transform(l.begin(), l.end(), l.begin(), ::tolower);
            can_wav = l.size() >= 4 && l.rfind(".wav") == l.size() - 4;
        }
        if (can_wav) {
            ImGui::SameLine();
            if (ImGui::Button("Extract WAV")) {
                ImGui::OpenPopup("progress_win");
                on_extract_selected_wav();
            }
        }
        bool can_tex = false, can_mdl = false;
        if (S.selected_file_index >= 0 && S.selected_file_index < (int) S.files.size()) {
            std::string n = S.files[(size_t) S.selected_file_index].name;
            std::string l = n;
            std::transform(l.begin(), l.end(), l.begin(), ::tolower);
            can_tex = l.size() >= 4 && l.rfind(".tex") == l.size() - 4;
            can_mdl = l.size() >= 4 && l.rfind(".mdl") == l.size() - 4;
        }

        if (can_tex && is_texture_bnk_selected()) {
            ImGui::SameLine();
            if (ImGui::Button("Rebuild and Extract (.tex)")) {
                auto name = S.files[(size_t) S.selected_file_index].name;
                ImGui::OpenPopup("progress_win");
                on_rebuild_and_extract_one(name);
            }
            if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted("Rebuilds the .tex file bitstreams");
                ImGui::EndTooltip();
            }
        }

        if (can_mdl && is_model_bnk_selected()) {
            ImGui::SameLine();
            if (ImGui::Button("Rebuild and Extract (.mdl)")) {
                auto name = S.files[(size_t) S.selected_file_index].name;
                ImGui::OpenPopup("progress_win");
                on_rebuild_and_extract_one_mdl(name);
            }
            if (!S.hide_tooltips && ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted("Rebuilds the .mdl file bitstreams");
                ImGui::EndTooltip();
            }
        }
    }
    ImGui::EndGroup();

    ImGui::Dummy(ImVec2(0, 8));

    if (!has_selection) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Hex View")) {
        ImGui::OpenPopup("progress_win");
        open_hex_for_selected();
    }
    if (!has_selection) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();

    bool can_preview = false;
    bool can_tex = false, can_mdl = false;
    if (has_selection) {
        std::string n = S.files[(size_t)S.selected_file_index].name;
        std::string l = n;
        std::transform(l.begin(), l.end(), l.begin(), ::tolower);
        can_tex = l.size() >= 4 && l.rfind(".tex") == l.size() - 4;
        can_mdl = l.size() >= 4 && l.rfind(".mdl") == l.size() - 4;
        can_preview = can_tex || can_mdl;
    }

    if (!can_preview) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Preview")) {
        auto item = S.files[(size_t)S.selected_file_index];
        auto name = item.name;

        progress_open(0, "Loading preview...");

        std::thread([device, item, name, can_tex, can_mdl]() {
            std::vector<unsigned char> buf;
            bool ok = false;
            try {
                if (can_tex) {
                    ok = build_tex_buffer_for_name(name, buf);
                } else if (can_mdl) {
                    ok = build_mdl_buffer_for_name(name, buf);
                }

                if (!ok) {
                    auto tmpdir = std::filesystem::temp_directory_path() / "f2_hex_view";
                    std::error_code ec;
                    std::filesystem::create_directories(tmpdir, ec);
                    auto tmp_file = tmpdir / ("hex_" + std::to_string(std::hash<std::string>{}(name)) + ".bin");
                    extract_one(S.selected_bnk, item.index, tmp_file.string());
                    buf = read_all_bytes(tmp_file);
                    ok = !buf.empty();
                    std::filesystem::remove(tmp_file, ec);
                }
            } catch (...) { ok = false; }

            if (ok) {
                S.hex_data = buf;

                if (can_tex) {
                    S.tex_info_ok = parse_tex_info(S.hex_data, S.tex_info);
                    if (S.tex_info_ok && !S.tex_info.Mips.empty()) {
                        S.preview_mip_index = 0;
                        S.show_preview_popup = true;
                    }
                } else if (can_mdl) {
                    S.mdl_info_ok = parse_mdl_info(S.hex_data, S.mdl_info);
                    if (S.mdl_info_ok) {
                        S.mdl_meshes.clear();
                        parse_mdl_geometry(S.hex_data, S.mdl_info, S.mdl_meshes);
                        extern ModelPreview g_mp;
                        MP_Release(g_mp);
                        MP_Init(device, g_mp, 800, 520);
                        MP_Build(device, S.mdl_meshes, S.mdl_info, g_mp);
                        S.cam_yaw = 0.0f; S.cam_pitch = 0.2f; S.cam_dist = 3.0f;
                        S.show_model_preview = true;
                    }
                }
            }

            progress_done();
            if (!ok) show_error_box("Failed to load preview.");
        }).detach();
    }
    if (!can_preview) {
        ImGui::EndDisabled();
    }

    ImGui::PopStyleVar();
    ImGui::EndGroup();

    static bool hide_tt = false;
    if (ImGui::Checkbox("Hide Paths Tooltip", &hide_tt)) { S.hide_tooltips = hide_tt; }

    int visible = count_visible_files();
    ImGui::Text("Files found: %d/%d", visible, (int) S.files.size());

    ImGui::PopItemWidth();
    ImGui::EndGroup();
    ImGui::EndChild();

    float available_width = ImGui::GetContentRegionAvail().x;
    float field_width = (available_width - 8.0f) * 0.5f;

    ImGui::SetNextItemWidth(field_width);
    ImGui::InputTextWithHint("##file_filter", "Filter Current BNK", &S.file_filter);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(field_width);
    bool search_changed = ImGui::InputTextWithHint("##global_search", "Search All BNKs", &S.global_search);

    if (S.global_search != g_last_global_search) {
        g_last_global_search = S.global_search;
        g_global_hits.clear();
        g_selected_global = -1;

        if (!S.global_search.empty()) {
            if (!g_global_busy) {
                g_global_busy = true;
                std::string search_term = S.global_search;

                std::thread([search_term]() {
                    std::vector<GlobalHit> local_hits;
                    std::string needle = search_term;
                    std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);

                    try {
                        for (const auto& bnk_path : S.bnk_paths) {
                            BNKReader reader(bnk_path);
                            const auto& files = reader.list_files();

                            for (size_t i = 0; i < files.size(); ++i) {
                                std::string fname = files[i].name;
                                std::string fname_lower = fname;
                                std::transform(fname_lower.begin(), fname_lower.end(), fname_lower.begin(), ::tolower);

                                if (fname_lower.find(needle) != std::string::npos) {
                                    local_hits.push_back({
                                        bnk_path,
                                        fname,
                                        (int)i,
                                        files[i].uncompressed_size
                                    });
                                }
                            }
                        }
                    } catch (...) {}

                    g_global_hits = std::move(local_hits);
                    g_global_busy = false;
                }).detach();
            }
        }
    }

    if (!S.hide_tooltips && ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted("Type to search across all BNK files");
        ImGui::EndTooltip();
    }

    ImGui::BeginChild("right_table_container", ImVec2(0, 0), false);
    if (!S.global_search.empty()) {
        draw_global_results_table();
    } else {
        draw_file_table();
    }
    ImGui::EndChild();

    if(S.show_preview_popup){
        ImGui::OpenPopup("Mip Preview");
        S.show_preview_popup = false;
    }

    if(ImGui::BeginPopupModal("Mip Preview", nullptr, ImGuiWindowFlags_None)){
        if(S.preview_mip_index >= 0 && S.preview_mip_index < (int)S.tex_info.Mips.size()){
            const auto& m = S.tex_info.Mips[S.preview_mip_index];
            if(!S.preview_srv){
                uint32_t base_w = S.tex_info.TextureWidth;
                uint32_t base_h = S.tex_info.TextureHeight;
                uint32_t w = m.HasWH ? (uint32_t)std::max(1,(int)m.MipWidth)  : std::max(1u, base_w >> S.preview_mip_index);
                uint32_t h = m.HasWH ? (uint32_t)std::max(1,(int)m.MipHeight) : std::max(1u, base_h >> S.preview_mip_index);
                if(m.MipDataOffset < S.hex_data.size() && m.MipDataOffset + m.MipDataSizeParsed <= S.hex_data.size()){
                    const uint8_t* src = S.hex_data.data() + m.MipDataOffset;
                    size_t src_sz = m.MipDataSizeParsed;

                    DXGI_FORMAT fmt = DXGI_FORMAT_BC1_UNORM;
                    if(S.tex_info.PixelFormat == 39) fmt = DXGI_FORMAT_BC3_UNORM;
                    else if(S.tex_info.PixelFormat == 40) fmt = DXGI_FORMAT_BC5_UNORM;

                    size_t blocks_x = (w + 3) / 4;
                    std::vector<uint8_t> payload(src, src + src_sz);

                    for(size_t i = 0; i + 8 <= payload.size(); i += 8) {
                        uint16_t c0 = (payload[i+0] << 8) | payload[i+1];
                        uint16_t c1 = (payload[i+2] << 8) | payload[i+3];
                        uint32_t idx = (payload[i+4] << 24) | (payload[i+5] << 16) | (payload[i+6] << 8) | payload[i+7];

                        payload[i+0] = c0 & 0xFF;
                        payload[i+1] = (c0 >> 8) & 0xFF;
                        payload[i+2] = c1 & 0xFF;
                        payload[i+3] = (c1 >> 8) & 0xFF;
                        payload[i+4] = idx & 0xFF;
                        payload[i+5] = (idx >> 8) & 0xFF;
                        payload[i+6] = (idx >> 16) & 0xFF;
                        payload[i+7] = (idx >> 24) & 0xFF;
                    }

                    D3D11_TEXTURE2D_DESC td{};
                    td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1; td.Format = fmt;
                    td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                    D3D11_SUBRESOURCE_DATA sd{}; sd.pSysMem = payload.data(); sd.SysMemPitch = (UINT)(blocks_x * 8);
                    ID3D11Texture2D* tex = nullptr;
                    if(device->CreateTexture2D(&td, &sd, &tex) == S_OK){
                        D3D11_SHADER_RESOURCE_VIEW_DESC svd{};
                        svd.Format = td.Format; svd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; svd.Texture2D.MipLevels = 1;
                        device->CreateShaderResourceView(tex, &svd, &S.preview_srv); tex->Release();
                    }
                }
            }
            if(S.preview_srv) ImGui::Image((ImTextureID)S.preview_srv, ImVec2(512, 512));
            else ImGui::TextUnformatted("Preview unsupported or failed.");
        }else{
            ImGui::TextUnformatted("No mip selected");
        }
        if(ImGui::Button("Close", ImVec2(-1,0))) {
            if(S.preview_srv) { S.preview_srv->Release(); S.preview_srv = nullptr; }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    {
        if(S.show_model_preview){ ImGui::OpenPopup("Model Preview"); S.show_model_preview = false; }

        const ImVec2 canvas(960, 640);
        const ImVec2 win_size(canvas.x + 32.0f, canvas.y + 110.0f);

        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowSize(win_size, ImGuiCond_Always);
        ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

        if(ImGui::BeginPopupModal("Model Preview", nullptr, ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings))
        {
            extern ModelPreview g_mp;
            MP_Render(device, g_mp, S.cam_yaw, S.cam_pitch, S.cam_dist);

            ImVec2 pos = ImGui::GetCursorScreenPos();
            if(g_mp.srv) ImGui::GetWindowDrawList()->AddImage((ImTextureID)g_mp.srv, pos, ImVec2(pos.x + canvas.x, pos.y + canvas.y));
            ImGui::InvisibleButton("model_canvas", canvas);

            float dt = ImGui::GetIO().DeltaTime;
            S.cam_yaw += dt * 0.6f;
            if(S.cam_yaw > 6.2831853f) S.cam_yaw -= 6.2831853f;

            if(ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)){
                float wheel = ImGui::GetIO().MouseWheel;
                if(fabsf(wheel) > 0.0001f) S.cam_dist *= (wheel > 0.f ? 0.9f : 1.1f);
            }

            if(ImGui::Button("Zoom -", ImVec2(90,0))) S.cam_dist *= 1.1f;
            ImGui::SameLine();
            if(ImGui::Button("Zoom +", ImVec2(90,0))) S.cam_dist *= 0.9f;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(220);
            ImGui::SliderFloat("##zoom", &S.cam_dist, 0.3f, 50.0f, "Dist %.2f");
            if(S.cam_dist < 0.3f)  S.cam_dist = 0.3f;
            if(S.cam_dist > 50.0f) S.cam_dist = 50.0f;

            ImGui::Dummy(ImVec2(0,6));
            if(ImGui::Button("Close", ImVec2(-1,0))) {
                MP_Release(g_mp);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    ImGui::EndChild();
}