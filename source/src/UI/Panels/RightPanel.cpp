#include "../UI_Panels.h"
#include "PanelInternal.h"
#include "../UI_Main.h"
#include "../HexView.h"
#include "../ModelPreview.h"
#include "../../textures/TexParser.h"
#include "../../textures/LhTexCodec.h"
#include "../../MDL/ModelParser.h"
#include "../../MDL/mdl_converter.h"
#include "../../Utilities/Utils.h"
#include "../../Utilities/Files.h"
#include "../../Utilities/operations.h"
#include "../../Utilities/Progress.h"
#include "../../BNKCore.cpp"
#include "../../Lua.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include <filesystem>
#include <algorithm>
#include <atomic>
#include <thread>
#include <ctime>
#include <cstring>

#ifdef _WIN32
void draw_right_panel(ID3D11Device* device) {
#else
void draw_right_panel() {
#endif
    ImGui::BeginChild("right_panel", ImVec2(0, 0), false);

    ImGui::BeginChild("extract_box", ImVec2(0, 100), true, ImGuiWindowFlags_NoScrollbar);
    ImGui::BeginGroup();
    ImGui::PushItemWidth(-1);

    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 0));

    ImGui::BeginGroup();

    if (!S.viewing_adb && !S.viewing_lua) {
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
                if (ImGui::Button("Play")) {
                    open_audio_player_for_selected(S.selected_file_index);
                }
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
    } else if (S.viewing_adb) {
        if (ImGui::Button("Extract All Uncompressed")) {
            ImGui::OpenPopup("progress_win");
            on_extract_all_adb();
        }
        if (!S.hide_tooltips && ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Extract all ADB files uncompressed to /extracted/audio_database/");
            ImGui::EndTooltip();
        }

        ImGui::SameLine();
        bool has_selection = (S.selected_file_index >= 0 && S.selected_file_index < (int)S.files.size());
        if (!has_selection) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Extract Uncompressed")) {
            ImGui::OpenPopup("progress_win");
            on_extract_adb_selected();
        }
        if (!S.hide_tooltips && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Extract selected ADB file uncompressed");
            ImGui::EndTooltip();
        }
        if (!has_selection) {
            ImGui::EndDisabled();
        }
    } else if (S.viewing_lua) {
        bool has_selection = (S.selected_file_index >= 0 && S.selected_file_index < (int)S.files.size());

        if (!has_selection) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Dump Lua")) {
            if (has_selection && S.selected_file_index < (int)S.lua_files.size()) {
                std::string path = S.lua_files[S.selected_file_index].path;
                dump_single_lua_file(path, S.root_dir);
            }
        }
        if (!S.hide_tooltips && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Decompile and dump selected Lua file");
            ImGui::EndTooltip();
        }
        if (!has_selection) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        if (ImGui::Button("Dump All Lua")) {
            dump_all_lua_files(S.root_dir);
        }
        if (!S.hide_tooltips && ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Decompile and dump all Lua files");
            ImGui::EndTooltip();
        }
    }

    ImGui::EndGroup();

    ImGui::Dummy(ImVec2(0, 8));

    bool has_selection = (S.selected_file_index >= 0 && S.selected_file_index < (int)S.files.size());

    if (S.dev_mode) {
        if (!has_selection) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Hex View")) {
            ImGui::OpenPopup("progress_win");
            if (S.viewing_adb) {
                auto item = S.files[(size_t)S.selected_file_index];
                progress_open(0, "Decompressing ADB...");
                std::thread([item]() {
                    auto entries = decompress_adb(item.name);
                    if (!entries.empty() && !entries[0].data.empty()) {
                        S.hex_data = entries[0].data;
                        S.hex_title = "Hex Editor - " + std::filesystem::path(item.name).filename().string() + " (decompressed)";
                        S.hex_open = true;
                        memset(&S.hex_state, 0, sizeof(S.hex_state));
                        S.hex_state.Bytes = (void*)S.hex_data.data();
                        S.hex_state.MaxBytes = (int)S.hex_data.size();
                        S.hex_state.ReadOnly = true;
                        S.hex_state.ShowAscii = true;
                        S.hex_state.ShowAddress = true;
                        S.hex_state.BytesPerLine = 16;
                    }
                    progress_done();
                    if (entries.empty() || entries[0].data.empty()) show_error_box("Failed to decompress ADB file.");
                }).detach();
            } else {
                open_hex_for_selected();
            }
        }
        if (!has_selection) {
            ImGui::EndDisabled();
        }
    }

    ImGui::SameLine();

    bool can_preview = false;
    bool can_tex = false, can_mdl = false;
    bool can_folder_preview = false;

    if (has_selection && !S.viewing_adb && !S.viewing_lua) {
        std::string n = S.files[(size_t)S.selected_file_index].name;
        std::string l = n;
        std::transform(l.begin(), l.end(), l.begin(), ::tolower);
        can_tex = l.size() >= 4 && l.rfind(".tex") == l.size() - 4;
        can_mdl = l.size() >= 4 && l.rfind(".mdl") == l.size() - 4;
        can_preview = can_tex || can_mdl;
    } else if (!S.selected_folder_path.empty()) {
        std::vector<std::pair<std::string, std::string>> mdl_paths;
        can_folder_preview = find_mdl_files_in_folder(g_tree_root, S.selected_folder_path, mdl_paths);
        can_preview = can_folder_preview;
    }

    {
        bool m_open = S.model_materials_open;
        if (ImGui::Checkbox("Materials Window", &m_open)) {
            S.model_materials_open = m_open;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Open Now")) {
            S.model_materials_open = true;
            S.model_preview_open   = true;
        }
    }

if (!can_preview) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Preview")) {
#ifdef _WIN32
        if (can_folder_preview && !S.selected_folder_path.empty()) {
            std::vector<std::pair<std::string, std::string>> mdl_paths;
            if (find_mdl_files_in_folder(g_tree_root, S.selected_folder_path, mdl_paths)) {
                progress_open(0, "Loading preview...");

                ID3D11Device* device_ptr = device;

                std::thread([device_ptr, mdl_paths]() {
                    std::vector<MDLMeshGeom> all_meshes;
                    MDLInfo combined_info;
                    bool any_success = false;

                    for (const auto& [mdl_path, bnk_source] : mdl_paths) {
                        std::vector<unsigned char> buf;
                        bool ok = false;

                        try {
                            ok = build_mdl_buffer_for_name(mdl_path, buf);
                        } catch (...) {}

                        if (ok && !buf.empty()) {
                            MDLInfo mdl_info;
                            if (parse_mdl_info(buf, mdl_info, mdl_path)) {
                                std::vector<MDLMeshGeom> meshes;
                                if (parse_mdl_geometry(buf, mdl_info, meshes)) {
                                    all_meshes.insert(all_meshes.end(), meshes.begin(), meshes.end());
                                    if (!any_success) {
                                        combined_info = mdl_info;
                                        any_success = true;
                                    }
                                }
                            }
                        }
                    }

                    if (any_success && !all_meshes.empty()) {
                        S.hex_data.clear();
                        S.mdl_info_ok = true;
                        S.mdl_info = combined_info;
                        S.mdl_meshes = all_meshes;

                        extern ModelPreview g_mp;
                        MP_Release(g_mp);
                        MP_Init(device_ptr, g_mp, 800, 520);
                        MP_Build(device_ptr, all_meshes, combined_info, g_mp);
                        S.cam_yaw = 0.0f;
                        S.cam_pitch = 0.2f;
                        S.cam_dist = 3.0f;
                        S.show_model_preview = true;
                    }

                    progress_done();
                    if (!any_success) {
                        show_error_box("Failed to load preview.");
                    }
                }).detach();
            }
        } else {
        auto item = S.files[(size_t)S.selected_file_index];
        auto name = item.name;

        {
            std::string filename = std::filesystem::path(name).filename().string();
            std::string filename_lower = filename;
            std::transform(filename_lower.begin(), filename_lower.end(), filename_lower.begin(), ::tolower);
            bool is_interior_or_exterior = (filename_lower == "interior.mdl" || filename_lower == "exterior.mdl");

            if (is_interior_or_exterior && can_mdl) {

                std::filesystem::path full_path(name);
                std::string folder_path = full_path.parent_path().string();

                std::vector<std::pair<std::string, int>> mdl_files;

                for (size_t i = 0; i < S.files.size(); ++i) {
                    const auto& file = S.files[i];
                    std::filesystem::path file_path(file.name);
                    std::string file_folder = file_path.parent_path().string();
                    std::string file_name_lower = file_path.filename().string();
                    std::transform(file_name_lower.begin(), file_name_lower.end(), file_name_lower.begin(), ::tolower);

                    if (file_folder == folder_path &&
                        (file_name_lower == "interior.mdl" || file_name_lower == "exterior.mdl")) {
                        mdl_files.push_back({file.name, file.index});
                    }
                }

                if (mdl_files.size() > 0) {

                    progress_open(0, "Loading preview...");

                    ID3D11Device* device_ptr = device;

                    std::thread([device_ptr, mdl_files]() {
                        std::vector<MDLMeshGeom> all_meshes;
                        MDLInfo combined_info;
                        bool any_success = false;

                        for (const auto& [mdl_name, mdl_index] : mdl_files) {
                            std::vector<unsigned char> buf;
                            bool ok = false;

                            try {
                                ok = build_mdl_buffer_for_name(mdl_name, buf);
                            } catch (...) {}

                            if (ok && !buf.empty()) {
                                MDLInfo mdl_info;
                                if (parse_mdl_info(buf, mdl_info, mdl_name)) {
                                    std::vector<MDLMeshGeom> meshes;
                                    if (parse_mdl_geometry(buf, mdl_info, meshes)) {
                                        all_meshes.insert(all_meshes.end(), meshes.begin(), meshes.end());
                                        if (!any_success) {
                                            combined_info = mdl_info;
                                            any_success = true;
                                        }
                                    }
                                }
                            }
                        }

                        if (any_success && !all_meshes.empty()) {
                            S.hex_data.clear();
                            S.mdl_info_ok = true;
                            S.mdl_info = combined_info;
                            S.mdl_meshes = all_meshes;

                            extern ModelPreview g_mp;
                            MP_Release(g_mp);
                            MP_Init(device_ptr, g_mp, 800, 520);
                            MP_Build(device_ptr, all_meshes, combined_info, g_mp);
                            S.cam_yaw = 0.0f;
                            S.cam_pitch = 0.2f;
                            S.cam_dist = 3.0f;
                            S.show_model_preview = true;
                        }

                        progress_done();
                        if (!any_success) {
                            show_error_box("Failed to load preview.");
                        }
                    }).detach();

                    goto skip_preview;
                }

            }
        }

        {
        std::string bnk_to_use;
        std::string nested_temp_copy;
        bool is_nested = false;

        if (S.selected_nested_index != -1 && !S.selected_nested_temp_path.empty()) {
            is_nested = true;
            auto tmpdir = std::filesystem::temp_directory_path() / "f2_preview";
            std::error_code ec;
            std::filesystem::create_directories(tmpdir, ec);

            auto unique_temp = tmpdir / ("nested_" + std::to_string(std::hash<std::string>{}(S.selected_nested_temp_path + std::to_string(std::time(nullptr)))) + ".bnk");

            try {
                if (!std::filesystem::exists(S.selected_nested_temp_path)) {
                    show_error_box("Nested BNK source file does not exist");
                    goto skip_preview;
                }

                std::filesystem::copy_file(S.selected_nested_temp_path, unique_temp,
                                          std::filesystem::copy_options::overwrite_existing, ec);
                if (!ec) {
                    nested_temp_copy = unique_temp.string();
                    bnk_to_use = nested_temp_copy;
                } else {
                    show_error_box("Failed to copy nested BNK: " + ec.message());
                    goto skip_preview;
                }
            } catch (const std::exception& e) {
                show_error_box(std::string("Exception copying nested BNK: ") + e.what());
                goto skip_preview;
            }
        } else {
            bnk_to_use = S.selected_bnk;
        }

        progress_open(0, "Loading preview...");

        std::string preferred_for_tex = is_nested
            ? S.selected_nested_temp_path
            : S.selected_bnk;

        std::thread([device, item, name, can_tex, can_mdl, bnk_to_use, nested_temp_copy, is_nested, preferred_for_tex]() {
            std::vector<unsigned char> buf;
            bool ok = false;
            try {
                if (can_tex) {
                    ok = build_any_tex_buffer_for_name(name, buf, preferred_for_tex);
                } else if (can_mdl) {
                    if (is_nested) {
                        ok = reconstruct_nested_mdl(bnk_to_use, item.index, buf);
                    } else {
                        ok = build_mdl_buffer_for_name(name, buf);
                    }
                }

                if (!ok) {
                    auto tmpdir = std::filesystem::temp_directory_path() / "f2_preview";
                    std::error_code ec;
                    std::filesystem::create_directories(tmpdir, ec);
                    auto tmp_file = tmpdir / ("preview_" + std::to_string(std::hash<std::string>{}(name + std::to_string(std::time(nullptr)))) + ".bin");

                    try {
                        extract_one(bnk_to_use, item.index, tmp_file.string());
                        buf = read_all_bytes(tmp_file);
                        ok = !buf.empty();
                        std::filesystem::remove(tmp_file, ec);
                    } catch (...) {
                        std::filesystem::remove(tmp_file, ec);
                        throw;
                    }
                }
            } catch (...) {
                ok = false;
            }

            if (!nested_temp_copy.empty()) {
                std::error_code ec;
                std::filesystem::remove(nested_temp_copy, ec);
            }

            if (ok) {
                S.hex_data = buf;

                if (can_tex) {
                    S.tex_info_ok = parse_tex_info(S.hex_data, S.tex_info);
                    if (S.tex_info_ok && !S.tex_info.Mips.empty()) {

                        int best_mip = -1;
                        size_t best_area = 0;
                        for (int i = 0; i < (int)S.tex_info.Mips.size(); ++i) {
                            int w = S.tex_info.Mips[i].HasWH ? (int)S.tex_info.Mips[i].MipWidth : std::max(1, (int)S.tex_info.TextureWidth >> i);
                            int h = S.tex_info.Mips[i].HasWH ? (int)S.tex_info.Mips[i].MipHeight : std::max(1, (int)S.tex_info.TextureHeight >> i);
                            size_t area = (size_t)w * (size_t)h;
                            if (area > best_area) {
                                best_area = area;
                                best_mip = i;
                            }
                        }
                        if (best_mip >= 0) {
                            S.preview_mip_index = best_mip;
                            S.show_preview_popup = true;
                        } else {
                        }
                    } else if (!S.tex_info_ok) {
                    }
                } else if (can_mdl) {
                    S.mdl_info_ok = parse_mdl_info(S.hex_data, S.mdl_info, name);
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

        skip_preview:;
    }
#else

        if (can_folder_preview && !S.selected_folder_path.empty()) {
            std::vector<std::pair<std::string, std::string>> mdl_paths;
            if (find_mdl_files_in_folder(g_tree_root, S.selected_folder_path, mdl_paths)) {
                progress_open(0, "Loading preview...");

                std::thread([mdl_paths]() {
                    std::vector<MDLMeshGeom> all_meshes;
                    MDLInfo combined_info;
                    bool any_success = false;

                    for (const auto& [mdl_path, bnk_source] : mdl_paths) {
                        std::vector<unsigned char> buf;
                        bool ok = false;

                        try {
                            ok = build_mdl_buffer_for_name(mdl_path, buf);
                        } catch (...) {}

                        if (ok && !buf.empty()) {
                            MDLInfo mdl_info;
                            if (parse_mdl_info(buf, mdl_info, mdl_path)) {
                                std::vector<MDLMeshGeom> meshes;
                                if (parse_mdl_geometry(buf, mdl_info, meshes)) {
                                    all_meshes.insert(all_meshes.end(), meshes.begin(), meshes.end());
                                    if (!any_success) {
                                        combined_info = mdl_info;
                                        any_success = true;
                                    }
                                }
                            }
                        }
                    }

                    if (any_success && !all_meshes.empty()) {
                        S.hex_data.clear();
                        S.mdl_info_ok = true;
                        S.mdl_info = combined_info;
                        S.mdl_meshes = all_meshes;
                        S.cam_yaw = 0.0f;
                        S.cam_pitch = 0.2f;
                        S.cam_dist = 3.0f;
                        S.pending_preview_build = true;
                    }

                    progress_done();
                    if (!any_success) {
                        show_error_box("Failed to load preview.");
                    }
                }).detach();
            }
        } else if (can_mdl) {
            auto item = S.files[(size_t)S.selected_file_index];
            auto name = item.name;
            progress_open(0, "Loading preview...");

            std::thread([name]() {
                bool ok = false;
                std::vector<unsigned char> buf;

                try {
                    ok = build_mdl_buffer_for_name(name, buf);
                } catch (...) {}

                if (ok && !buf.empty()) {
                    MDLInfo mdl_info;
                    if (parse_mdl_info(buf, mdl_info, name)) {
                        std::vector<MDLMeshGeom> meshes;
                        if (parse_mdl_geometry(buf, mdl_info, meshes)) {
                            S.hex_data.clear();
                            S.mdl_info_ok = true;
                            S.mdl_info = mdl_info;
                            S.mdl_meshes = meshes;
                            S.cam_yaw = 0.0f;
                            S.cam_pitch = 0.2f;
                            S.cam_dist = 3.0f;
                            S.pending_preview_build = true;
                            ok = true;
                        }
                    }
                }

                progress_done();
                if (!ok) show_error_box("Failed to load preview.");
            }).detach();
        }
#endif
    }
    if (!can_preview) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();

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

    if (!has_mdl_files || S.viewing_adb) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Export All to GLB")) {
        ImGui::OpenPopup("progress_win");
        if (!S.global_search.empty()) {
            on_export_global_mdl_to_glb(g_global_hits);
        } else {
            on_export_all_mdl_to_glb();
        }
    }
    if (!S.hide_tooltips && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted("Export all .mdl files to GLB format");
        ImGui::EndTooltip();
    }
    if (!has_mdl_files || S.viewing_adb) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();

    bool can_export_mdl = false;
    if (has_selection && !S.viewing_adb) {
        std::string n = S.files[(size_t)S.selected_file_index].name;
        std::string l = n;
        std::transform(l.begin(), l.end(), l.begin(), ::tolower);
        can_export_mdl = l.size() >= 4 && l.rfind(".mdl") == l.size() - 4;
    }

    if (!can_export_mdl) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Export to GLB")) {
        ImGui::OpenPopup("progress_win");
        on_export_mdl_to_glb();
    }
    if (!S.hide_tooltips && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted("Export selected .mdl file to GLB format");
        ImGui::EndTooltip();
    }
    if (!can_export_mdl) {
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

    {
        std::vector<std::string> exts = unique_file_extensions();
        ImGui::SetNextItemWidth(160.0f);
        const char* current_label = S.ext_filter.empty() ? "(all extensions)" : S.ext_filter.c_str();
        if (ImGui::BeginCombo("##ext_filter", current_label)) {
            if (ImGui::Selectable("(all extensions)", S.ext_filter.empty())) {
                S.ext_filter.clear();
            }
            for (const auto& e : exts) {
                bool is_selected = (e == S.ext_filter);
                if (ImGui::Selectable(e.c_str(), is_selected)) {
                    S.ext_filter = e;
                }
                if (is_selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (!S.hide_tooltips && ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Show only files with this extension");
            ImGui::EndTooltip();
        }
    }

    float available_width = ImGui::GetContentRegionAvail().x;
    float field_width = (available_width - 8.0f) * 0.5f;

    ImGui::SetNextItemWidth(field_width);
    const char* filter_hint = "Filter Current BNK";
    if (S.viewing_adb) filter_hint = "Filter ADB Files";
    else if (S.viewing_lua) filter_hint = "Filter Lua Scripts";
    ImGui::InputTextWithHint("##file_filter", filter_hint, &S.file_filter);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(field_width);
    bool search_changed = ImGui::InputTextWithHint("##global_search", "Search All BNKs", &S.global_search);

    if (S.global_search != g_last_global_search) {
        g_last_global_search = S.global_search;
        g_global_hits.clear();
        g_selected_global = -1;

        if (!S.global_search.empty()) {
            S.viewing_adb = false;
            if (!g_global_busy) {
                g_global_busy = true;
                std::string search_term = S.global_search;

                std::thread([search_term]() {
                    std::vector<GlobalHit> local_hits;
                    std::string needle = search_term;
                    std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);

                    auto is_header_bnk = [](const std::string& bnk_path) -> bool {
                        std::string lower_path = bnk_path;
                        std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(), ::tolower);
                        std::string filename = std::filesystem::path(lower_path).filename().string();
                        return filename.find("header") != std::string::npos;
                    };

                    auto is_nested_bnk = [](const std::string& filename) -> bool {
                        std::string lower = filename;
                        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                        return lower.size() >= 4 && lower.substr(lower.size() - 4) == ".bnk";
                    };

                    try {
                        for (const auto& bnk_path : S.bnk_paths) {
                            if (is_header_bnk(bnk_path)) {
                                continue;
                            }

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

                                if (is_nested_bnk(fname)) {
                                    try {
                                        auto tmpdir = std::filesystem::temp_directory_path() / "f2_global_search_nested";
                                        std::error_code ec;
                                        std::filesystem::create_directories(tmpdir, ec);

                                        std::string temp_name = "search_nested_" + std::to_string(std::hash<std::string>{}(bnk_path + fname)) + ".bnk";
                                        auto temp_bnk_path = tmpdir / temp_name;

                                        extract_one(bnk_path, (int)i, temp_bnk_path.string());

                                        BNKReader nested_reader(temp_bnk_path.string());
                                        const auto& nested_files = nested_reader.list_files();

                                        size_t fname_last_slash = fname.find_last_of('/');
                                        std::string prefix = (fname_last_slash == std::string::npos)
                                            ? std::string()
                                            : fname.substr(0, fname_last_slash + 1);

                                        for (size_t j = 0; j < nested_files.size(); ++j) {
                                            const auto& nested_file = nested_files[j];
                                            std::string nested_fname = prefix + nested_file.name;
                                            std::string nested_fname_lower = nested_fname;
                                            std::transform(nested_fname_lower.begin(), nested_fname_lower.end(), nested_fname_lower.begin(), ::tolower);

                                            if (nested_fname_lower.find(needle) != std::string::npos) {
                                                local_hits.push_back({
                                                    temp_bnk_path.string(),
                                                    nested_fname,
                                                    (int)j,
                                                    nested_files[j].uncompressed_size
                                                });
                                            }
                                        }
                                    } catch (...) {}
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

    if (S.viewing_lua) {
        float available_height = ImGui::GetContentRegionAvail().y;
        float table_height = available_height * 0.35f;
        float preview_height = available_height - table_height - 8.0f;

        ImGui::BeginChild("lua_table_container", ImVec2(0, table_height), false);
        draw_file_table();
        ImGui::EndChild();

        if (S.selected_file_index >= 0 && S.selected_file_index < (int)S.lua_files.size()) {
            if (S.lua_preview_selected != S.selected_file_index && !S.lua_preview_loading) {
                S.lua_preview_selected = S.selected_file_index;
                S.lua_preview_title = S.lua_files[S.selected_file_index].filename;
                S.lua_preview_content.clear();
                S.lua_preview_loading = true;

                std::string path = S.lua_files[S.selected_file_index].path;
                std::string title = S.lua_preview_title;

                progress_open(0, "Decompiling " + title + "...");

                std::thread([path, title]() {
                    std::string content = read_lua_file_content(path);
                    S.lua_preview_content = content;
                    S.lua_preview_loading = false;
                    progress_done();
                }).detach();
            }
        }

        ImGui::Dummy(ImVec2(0, 4));

        if (S.lua_preview_loading) {
            ImGui::BeginChild("lua_preview_loading", ImVec2(0, preview_height), true);
            ImGui::TextDisabled("Decompiling...");
            ImGui::EndChild();
        } else if (!S.lua_preview_content.empty()) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
            ImGui::BeginChild("lua_preview", ImVec2(0, preview_height), true, ImGuiWindowFlags_HorizontalScrollbar);

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.7f, 1.0f));
            ImGui::TextUnformatted(S.lua_preview_title.c_str());
            ImGui::PopStyleColor();
            ImGui::Separator();

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.9f, 0.8f, 1.0f));
            ImGui::TextUnformatted(S.lua_preview_content.c_str());
            ImGui::PopStyleColor();

            ImGui::EndChild();
            ImGui::PopStyleColor();
        } else {
            ImGui::BeginChild("lua_preview_empty", ImVec2(0, preview_height), true);
            ImGui::TextDisabled("Select a Lua file to preview");
            ImGui::EndChild();
        }
    } else {
        ImGui::BeginChild("right_table_container", ImVec2(0, 0), false);
        if (!S.global_search.empty()) {
            draw_global_results_table();
        } else {
            draw_file_table();
        }
        ImGui::EndChild();
    };

    if (g_pending_mdl_load && g_pending_mdl_index >= 0 && g_pending_mdl_index < (int)S.files.size()) {
        g_pending_mdl_load = false;
        auto item = S.files[(size_t)g_pending_mdl_index];
        auto name = item.name;
        std::string parse_path = g_pending_mdl_full_path.empty() ? name : g_pending_mdl_full_path;
        g_pending_mdl_full_path.clear();

        std::string bnk_to_use;
        std::string nested_temp_copy;
        bool is_nested = false;

        if (S.selected_nested_index != -1 && !S.selected_nested_temp_path.empty()) {
            is_nested = true;
            auto tmpdir = std::filesystem::temp_directory_path() / "f2_preview";
            std::error_code ec;
            std::filesystem::create_directories(tmpdir, ec);
            auto unique_temp = tmpdir / ("nested_" + std::to_string(std::hash<std::string>{}(S.selected_nested_temp_path + std::to_string(std::time(nullptr)))) + ".bnk");
            try {
                if (std::filesystem::exists(S.selected_nested_temp_path)) {
                    std::filesystem::copy_file(S.selected_nested_temp_path, unique_temp,
                                              std::filesystem::copy_options::overwrite_existing, ec);
                    if (!ec) {
                        nested_temp_copy = unique_temp.string();
                        bnk_to_use = nested_temp_copy;
                    }
                }
            } catch (...) {}
        } else {
            bnk_to_use = S.selected_bnk;
        }

        if (!bnk_to_use.empty()) {
#ifdef _WIN32
            ID3D11Device* device_ptr = device;
            std::thread([device_ptr, item, name, parse_path, bnk_to_use, nested_temp_copy, is_nested]() {
#else
            std::thread([item, name, parse_path, bnk_to_use, nested_temp_copy, is_nested]() {
#endif
                std::vector<unsigned char> buf;
                bool ok = false;

                try {
                    if (is_nested) {
                        ok = reconstruct_nested_mdl(bnk_to_use, item.index, buf);
                    } else {
                        ok = build_mdl_buffer_for_name(name, buf);
                    }

                    if (!ok) {
                        auto tmpdir = std::filesystem::temp_directory_path() / "f2_preview";
                        std::error_code ec;
                        std::filesystem::create_directories(tmpdir, ec);
                        auto tmp_file = tmpdir / ("preview_" + std::to_string(std::hash<std::string>{}(name + std::to_string(std::time(nullptr)))) + ".bin");
                        try {
                            extract_one(bnk_to_use, item.index, tmp_file.string());
                            buf = read_all_bytes(tmp_file);
                            ok = !buf.empty();
                            std::filesystem::remove(tmp_file, ec);
                        } catch (...) {
                            std::filesystem::remove(tmp_file, ec);
                        }
                    }
                } catch (...) {
                    ok = false;
                }

                if (!nested_temp_copy.empty()) {
                    std::error_code ec;
                    std::filesystem::remove(nested_temp_copy, ec);
                }

                if (ok && !buf.empty()) {
                    S.mdl_info_ok = parse_mdl_info(buf, S.mdl_info, parse_path);
                    if (S.mdl_info_ok) {
                        S.mdl_meshes.clear();
                        parse_mdl_geometry(buf, S.mdl_info, S.mdl_meshes);
#ifdef _WIN32
                        extern ModelPreview g_mp;
                        MP_Release(g_mp);
                        MP_Init(device_ptr, g_mp, 800, 600);
                        MP_Build(device_ptr, S.mdl_meshes, S.mdl_info, g_mp);
#else
                        S.pending_preview_build = true;
#endif
                    }
                }
            }).detach();
        }
        g_pending_mdl_index = -1;
    }

    if (g_pending_tex_load && g_pending_tex_index >= 0 && g_pending_tex_index < (int)S.files.size()) {
        g_pending_tex_load = false;
        auto item = S.files[(size_t)g_pending_tex_index];
        auto name = item.name;
        S.texture_window_name = name;

        std::string preferred_for_tex = (S.selected_nested_index != -1 && !S.selected_nested_temp_path.empty())
            ? S.selected_nested_temp_path
            : S.selected_bnk;

        progress_open(0, "Loading texture...");

        std::thread([name, preferred_for_tex]() {
            std::vector<unsigned char> tex_buf;
            if (!build_any_tex_buffer_for_name(name, tex_buf, preferred_for_tex)) {
                S.texture_window_name = "ERROR: Could not load texture file";
                S.pending_texture_load = true;
                S.pending_texture_w = 0;
                S.pending_texture_h = 0;
                progress_done();
                return;
            }

            std::vector<uint8_t> rgba;
            int w = 0, h = 0;
            bool has_alpha = false;
            if (!decode_tex_to_rgba(tex_buf, rgba, w, h, &has_alpha)) {
                S.texture_window_name = "ERROR: Could not decode texture";
                S.pending_texture_load = true;
                S.pending_texture_w = 0;
                S.pending_texture_h = 0;
                progress_done();
                return;
            }

            S.pending_texture_rgba = std::move(rgba);
            S.pending_texture_w = w;
            S.pending_texture_h = h;
            S.pending_texture_load = true;
            progress_done();
        }).detach();

        g_pending_tex_index = -1;
    }

#ifndef _WIN32
    if (S.pending_preview_build) {
        S.pending_preview_build = false;
        extern ModelPreview g_mp;
        extern FlyCam g_flycam;
        MP_Release(g_mp);
        MP_Init(g_mp, 800, 600);
        MP_Build(S.mdl_meshes, S.mdl_info, g_mp);
    }
#endif

    ImGui::EndChild();
}
