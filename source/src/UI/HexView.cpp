#include "HexView.h"
#include "../Utilities/State.h"
#include "../Utilities/Utils.h"
#include "../Utilities/Progress.h"
#include "../Utilities/Files.h"
#include "../textures/TexParser.h"
#include "../textures/LhTexCodec.h"
#include "../MDL/ModelParser.h"
#include "ModelPreview.h"
#include "../Level/TerrainTextureRegistry.h"
#include "../BNKCore.cpp"
#include "imgui.h"
#include "imgui_hex.h"
#include <thread>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <vector>
#include <string>
#include <fstream>
#include <cstring>
#include <zlib.h>
#include <ctime>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#include <d3d11.h>
#else
#include <GL/glew.h>
#endif

extern ModelPreview g_mp;

static void reset_preview_resources() {
#ifdef _WIN32
    if (S.preview_srv) { S.preview_srv->Release(); S.preview_srv = nullptr; }
    if (S.model_diffuse_srv) { S.model_diffuse_srv->Release(); S.model_diffuse_srv = nullptr; }
    MP_Release(g_mp);
#else
    if (S.preview_tex) { glDeleteTextures(1, &S.preview_tex); S.preview_tex = 0; }
    if (S.model_diffuse_tex) { glDeleteTextures(1, &S.model_diffuse_tex); S.model_diffuse_tex = 0; }
#endif
    S.preview_mip_index = -1;
    S.show_preview_popup = false;
    S.tex_info_ok = false;
    S.mdl_info_ok = false;
    S.show_model_preview = false;
    S.mdl_meshes.clear();
    S.cam_yaw = 0.0f; S.cam_pitch = 0.2f; S.cam_dist = 3.0f;
}

#ifdef _WIN32
static bool is_bc_format(uint32_t comp_flag, DXGI_FORMAT& out_fmt) {
    if (comp_flag == 7) { out_fmt = DXGI_FORMAT_BC1_UNORM; return true; }
    if (comp_flag == 8) { out_fmt = DXGI_FORMAT_BC2_UNORM; return true; }
    if (comp_flag == 9) { out_fmt = DXGI_FORMAT_BC3_UNORM; return true; }
    if (comp_flag == 10) { out_fmt = DXGI_FORMAT_BC4_UNORM; return true; }
    if (comp_flag == 11) { out_fmt = DXGI_FORMAT_BC5_UNORM; return true; }
    return false;
}
#endif

static bool reconstruct_nested_mdl(const std::string& nested_bnk_path, int file_index, std::vector<unsigned char>& out) {
    try {
        BNKReader nested_reader(nested_bnk_path);
        const auto& files = nested_reader.list_files();
        if (file_index < 0 || file_index >= (int)files.size()) return false;

        std::string mdl_name = files[file_index].name;

        auto tmpdir = std::filesystem::temp_directory_path() / "f2_nested_mdl_reconstruct";
        std::error_code ec;
        std::filesystem::create_directories(tmpdir, ec);

        auto tmp_body = tmpdir / "body.bin";
        extract_one(nested_bnk_path, file_index, tmp_body.string());
        auto body_data = read_all_bytes(tmp_body);
        std::filesystem::remove(tmp_body, ec);

        if (body_data.empty()) return false;

        auto p_headers = find_bnk_by_filename("globals_model_headers.bnk");
        if (!p_headers) {
            out = body_data;
            return true;
        }

        BNKReader r_headers(*p_headers);
        const auto& header_files = r_headers.list_files();

        std::string mdl_filename = std::filesystem::path(mdl_name).filename().string();
        std::string mdl_lower = mdl_filename;
        std::transform(mdl_lower.begin(), mdl_lower.end(), mdl_lower.begin(), ::tolower);

        int header_idx = -1;
        for (size_t i = 0; i < header_files.size(); ++i) {
            std::string hname = std::filesystem::path(header_files[i].name).filename().string();
            std::string hname_lower = hname;
            std::transform(hname_lower.begin(), hname_lower.end(), hname_lower.begin(), ::tolower);
            if (hname_lower == mdl_lower) {
                header_idx = (int)i;
                break;
            }
        }

        if (header_idx == -1) {
            out = body_data;
            return true;
        }

        auto tmp_header = tmpdir / "header.bin";
        extract_one(*p_headers, header_idx, tmp_header.string());
        auto header_data = read_all_bytes(tmp_header);
        std::filesystem::remove(tmp_header, ec);

        if (header_data.empty()) {
            out = body_data;
            return true;
        }

        out.clear();
        out.reserve(header_data.size() + body_data.size());
        out.insert(out.end(), header_data.begin(), header_data.end());
        out.insert(out.end(), body_data.begin(), body_data.end());

        return true;

    } catch (...) {
        return false;
    }
}

std::vector<ADBEntry> decompress_adb(const std::string& path) {
    std::vector<ADBEntry> result;

    std::ifstream f(path, std::ios::binary);
    if (!f) return result;

    char header[12];
    f.read(header, 12);
    if (memcmp(header, "LhCoMpReSsEd", 12) != 0) {
        return result;
    }

    uint32_t file_count;
    f.read((char*)&file_count, 4);
    file_count = (file_count >> 24) | ((file_count >> 8) & 0xFF00) | ((file_count << 8) & 0xFF0000) | (file_count << 24);

    std::string base_name = std::filesystem::path(path).stem().string();

    for (uint32_t entry_num = 0; entry_num < file_count; ++entry_num) {
        uint32_t decomp_size, comp_size;
        f.read((char*)&decomp_size, 4);
        f.read((char*)&comp_size, 4);

        decomp_size = (decomp_size >> 24) | ((decomp_size >> 8) & 0xFF00) | ((decomp_size << 8) & 0xFF0000) | (decomp_size << 24);
        comp_size = (comp_size >> 24) | ((comp_size >> 8) & 0xFF00) | ((comp_size << 8) & 0xFF0000) | (comp_size << 24);

        if (comp_size == 0 || comp_size > 100000000) break;

        std::vector<uint8_t> compressed(comp_size);
        f.read((char*)compressed.data(), comp_size);

        z_stream strm;
        memset(&strm, 0, sizeof(strm));

        if (inflateInit(&strm) != Z_OK) continue;

        std::vector<uint8_t> decompressed;
        strm.avail_in = (uInt)compressed.size();
        strm.next_in = compressed.data();

        uint8_t outbuffer[32768];
        do {
            strm.avail_out = sizeof(outbuffer);
            strm.next_out = outbuffer;

            int ret = inflate(&strm, Z_NO_FLUSH);
            if (ret != Z_OK && ret != Z_STREAM_END) {
                inflateEnd(&strm);
                break;
            }

            size_t have = sizeof(outbuffer) - strm.avail_out;
            decompressed.insert(decompressed.end(), outbuffer, outbuffer + have);

            if (ret == Z_STREAM_END) break;
        } while (strm.avail_out == 0);

        inflateEnd(&strm);

        if (!decompressed.empty()) {
            ADBEntry entry;
            if (file_count == 1) {
                entry.name = base_name + ".bin";
            } else {
                char buf[32];
                snprintf(buf, sizeof(buf), "%s_%04u.bin", base_name.c_str(), entry_num);
                entry.name = buf;
            }
            entry.data = decompressed;
            result.push_back(entry);
        }
    }

    return result;
}

void open_hex_for_selected() {
    int idx = S.selected_file_index;
    if (idx < 0 || idx >= (int) S.files.size()) {
        show_error_box("No file selected.");
        return;
    }

    std::string bnk_to_use;
    std::string nested_temp_copy;
    bool is_nested = false;

    if (S.selected_nested_index != -1 && !S.selected_nested_temp_path.empty()) {
        is_nested = true;
        auto tmpdir = std::filesystem::temp_directory_path() / "f2_hex_view";
        std::error_code ec;
        std::filesystem::create_directories(tmpdir, ec);

        auto unique_temp = tmpdir / ("nested_" + std::to_string(std::hash<std::string>{}(S.selected_nested_temp_path + std::to_string(std::time(nullptr)))) + ".bnk");

        try {
            if (!std::filesystem::exists(S.selected_nested_temp_path)) {
                show_error_box("Nested BNK source file does not exist");
                return;
            }

            std::filesystem::copy_file(S.selected_nested_temp_path, unique_temp,
                                      std::filesystem::copy_options::overwrite_existing, ec);
            if (!ec) {
                nested_temp_copy = unique_temp.string();
                bnk_to_use = nested_temp_copy;
            } else {
                show_error_box("Failed to copy nested BNK: " + ec.message());
                return;
            }
        } catch (const std::exception& e) {
            show_error_box(std::string("Exception copying nested BNK: ") + e.what());
            return;
        }
    } else {
        bnk_to_use = S.selected_bnk;
    }

    if (bnk_to_use.empty()) {
        show_error_box("No BNK selected.");
        return;
    }

    auto item = S.files[(size_t) idx];
    auto name = item.name;
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    bool want_tex = is_tex_file(lower);
    bool want_mdl = is_mdl_file(lower);

    progress_open(0, "Loading hex.");
    S.hex_loading.store(true);

    std::string preferred_for_tex = is_nested
        ? S.selected_nested_temp_path
        : S.selected_bnk;

    std::thread([item, name, want_tex, want_mdl, bnk_to_use, nested_temp_copy, is_nested, preferred_for_tex]() {
        std::vector<unsigned char> buf;
        bool ok = false;

        try {
            if (want_tex) {
                ok = build_any_tex_buffer_for_name(name, buf, preferred_for_tex);
            } else if (want_mdl) {
                if (is_nested) {
                    ok = reconstruct_nested_mdl(bnk_to_use, item.index, buf);
                } else {
                    ok = build_mdl_buffer_for_name(name, buf);
                }
            }

            if (!ok) {
                auto tmpdir = std::filesystem::temp_directory_path() / "f2_hex_view";
                std::error_code ec;
                std::filesystem::create_directories(tmpdir, ec);
                auto tmp_file = tmpdir / ("hex_" + std::to_string(std::hash<std::string>{}(name + std::to_string(std::time(nullptr)))) + ".bin");

                try {
                    extract_one(bnk_to_use, item.index, tmp_file.string());
                    buf = read_all_bytes(tmp_file);
                    ok = !buf.empty();
                    std::filesystem::remove(tmp_file, ec);
                } catch (...) {
                    std::error_code ec2;
                    std::filesystem::remove(tmp_file, ec2);
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

        S.hex_data.clear();
        if (ok) S.hex_data.swap(buf);
        S.hex_title = std::string("Hex Editor - ") + name;
        S.hex_file_path = name;
        S.hex_open = ok;
        memset(&S.hex_state, 0, sizeof(S.hex_state));
        if (ok) {
            S.hex_state.Bytes = (void *) S.hex_data.data();
            S.hex_state.MaxBytes = (int) S.hex_data.size();
            S.hex_state.ReadOnly = true;
            S.hex_state.ShowAscii = true;
            S.hex_state.ShowAddress = true;
            S.hex_state.BytesPerLine = 16;
        }
        S.hex_loading.store(false);
        progress_done();
        if (!ok) show_error_box("Failed to load bytes for hex view.");
    }).detach();
}

#ifdef _WIN32
void draw_hex_window(ID3D11Device *device) {
#else
void draw_hex_window() {
#endif

    if (!S.dev_mode) {
        S.hex_open = false;
        return;
    }
    if (S.hex_open && S.hex_data.empty()) S.hex_open = false;
    const bool show_hex = S.hex_open && !S.hex_loading.load() && !S.hex_data.empty();
    if (show_hex) {

    ImGui::SetNextWindowSize(ImVec2(1000, 620), ImGuiCond_FirstUseEver);
    if(ImGui::Begin(S.hex_title.c_str(), &S.hex_open))
    {
        static ImGuiHexEditorState hex{};
        unsigned char* bytes_ptr = S.hex_data.data();
        int max_bytes = (int)std::min<size_t>(S.hex_data.size(), (size_t)INT_MAX);

        if(hex.Bytes != bytes_ptr || hex.MaxBytes != max_bytes){
            hex = ImGuiHexEditorState{};
            hex.Bytes        = (void*)bytes_ptr;
            hex.MaxBytes     = max_bytes > 0 ? max_bytes : 1;
            hex.ReadOnly     = true;
            hex.ShowAscii    = true;
            hex.ShowAddress  = true;
            hex.BytesPerLine = 16;
        }
        if(hex.BytesPerLine <= 0) hex.BytesPerLine = 16;

        ImGui::BeginChild("hex_and_info", ImVec2(0,0), false);
        ImGui::BeginGroup();

        float left_w = ImGui::GetContentRegionAvail().x * 0.55f;
        if(left_w < 160.0f) left_w = ImGui::GetContentRegionAvail().x * 0.60f;
        ImGui::BeginChild("hex_left", ImVec2(left_w, 0), true);
        ImGui::BeginHexEditor("hex_view", &hex, ImVec2(0,0));
        ImGui::EndHexEditor();
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("hex_right", ImVec2(0,0), true);

        static int cached_sel_index = -1;
        bool has_sel = (S.selected_file_index>=0 && S.selected_file_index<(int)S.files.size());

        if(has_sel){
            if(cached_sel_index != S.selected_file_index){
                const std::string& sel_name =
                    S.files[(size_t)S.selected_file_index].name;
                const bool hex_data_matches_selection =
                    !S.hex_file_path.empty() && sel_name == S.hex_file_path;
                if (hex_data_matches_selection) {
                    reset_preview_resources();
                }
                cached_sel_index = S.selected_file_index;
            }

            std::string sel = S.files[(size_t)S.selected_file_index].name;

            if(is_tex_file(sel)){
                if(!S.tex_info_ok){
                    S.tex_info_ok = parse_tex_info(S.hex_data, S.tex_info);
                }

                ImGui::Text("Header");
                ImGui::Separator();

                if(S.hex_data.size() >= 32){
                    uint32_t sign, rawsize, unk0, unk1, width, height, pixfmt, mipmap;
                    size_t o=0;
                    bool header_ok = true;
                    header_ok &= rd32be(S.hex_data, o, sign); o+=4;
                    header_ok &= rd32be(S.hex_data, o, rawsize); o+=4;
                    header_ok &= rd32be(S.hex_data, o, unk0); o+=4;
                    header_ok &= rd32be(S.hex_data, o, unk1); o+=4;
                    header_ok &= rd32be(S.hex_data, o, width); o+=4;
                    header_ok &= rd32be(S.hex_data, o, height); o+=4;
                    header_ok &= rd32be(S.hex_data, o, pixfmt); o+=4;
                    header_ok &= rd32be(S.hex_data, o, mipmap); o+=4;

                    if(header_ok){
                        ImGui::Text("Sign: 0x%08X", sign);
                        ImGui::Text("RawDataSize: %u", rawsize);
                        ImGui::Text("Unknown_0: %u", unk0);
                        ImGui::Text("Unknown_1: %u", unk1);
                        ImGui::Text("Width: %u", width);
                        ImGui::Text("Height: %u", height);
                        ImGui::Text("PixelFormat: %u (0x%08X)", pixfmt, pixfmt);
                        ImGui::Text("MipMap: %u", mipmap);
                    }else{
                        ImGui::TextColored(ImVec4(1,0.5f,0.5f,1), "Failed to read header");
                    }
                }else{
                    ImGui::TextColored(ImVec4(1,0.5f,0.5f,1), "File too small (< 32 bytes)");
                }

                if(S.tex_info_ok && !S.tex_info.Mips.empty()){
                    ImGui::Dummy(ImVec2(0,6));
                    ImGui::Text("MipMap Definitions");
                    ImGui::Separator();

                    for(int i=0;i<(int)S.tex_info.Mips.size();++i){
                        const auto& m = S.tex_info.Mips[i];
                        char lbl[64]; snprintf(lbl,sizeof(lbl),"Mip %d", i);
                        if(ImGui::TreeNode(lbl)){
                            ImGui::Text("DefOffset: 0x%zX", m.DefOffset);
                            ImGui::Text("CompFlag: %u", m.CompFlag);
                            ImGui::Text("DataOffset: 0x%08X", m.DataOffset);
                            ImGui::Text("DataSize: %u", m.DataSize);
                            ImGui::Text("Unknown_3..11: %u %u %u %u %u %u %u %u %u",
                                        m.Unknown_3,m.Unknown_4,m.Unknown_5,m.Unknown_6,m.Unknown_7,
                                        m.Unknown_8,m.Unknown_9,m.Unknown_10,m.Unknown_11);
                            if(m.HasWH){
                                ImGui::Text("MipWidth: %u", (unsigned)m.MipWidth);
                                ImGui::Text("MipHeight: %u", (unsigned)m.MipHeight);
                            }else{
                                uint32_t w = std::max(1u, S.tex_info.TextureWidth  >> i);
                                uint32_t h = std::max(1u, S.tex_info.TextureHeight >> i);
                                ImGui::Text("Derived Size: %ux%u", w, h);
                            }
                            ImGui::Text("MipMapData@ 0x%zX, Size %zu", m.MipDataOffset, m.MipDataSizeParsed);

                            ImGui::TreePop();
                        }
                    }
                }else if(S.hex_data.size() >= 32){
                    ImGui::Dummy(ImVec2(0,6));
                    ImGui::TextColored(ImVec4(1,0.7f,0.3f,1), "Mipmap parsing failed");
                    ImGui::TextWrapped("Could not parse mipmap definitions. File may be corrupted or incomplete.");
                }

            }else if(is_mdl_file(sel)){
                if(!S.mdl_info_ok){
                    S.mdl_info_ok = parse_mdl_info(S.hex_data, S.mdl_info, S.hex_file_path);
                }

                if(!S.mdl_info_ok){
                    ImGui::TextColored(ImVec4(1,0.5f,0.5f,1), "Failed to parse .mdl");
                }else{
                    ImGui::Text("Header");
                    ImGui::Separator();
                    ImGui::TextUnformatted(("Magic: " + S.mdl_info.Magic).c_str());
                    ImGui::Text("HeaderSize: %u", S.mdl_info.HeaderSize);
                    ImGui::Text("BoneCount: %u", S.mdl_info.BoneCount);
                    ImGui::Text("BoneTransformCount: %u %s", S.mdl_info.BoneTransformCount, S.mdl_info.HasBoneTransforms?"(match)":"");
                    ImGui::Text("Unk6Count: %u", S.mdl_info.Unk6Count);
                    ImGui::Text("MeshCount: %u", S.mdl_info.MeshCount);

                    if(!S.mdl_info.Bones.empty()){
                        ImGui::Dummy(ImVec2(0,6));
                        ImGui::Text("Bones");
                        ImGui::Separator();
                        if(ImGui::BeginTable("bones_tbl", 3, ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollY, ImVec2(0, 200))){
                            ImGui::TableSetupColumn("Idx", ImGuiTableColumnFlags_WidthFixed, 48.0f);
                            ImGui::TableSetupColumn("Name");
                            ImGui::TableSetupColumn("Parent");
                            ImGui::TableHeadersRow();
                            ImGuiListClipper clipper;
                            clipper.Begin((int)S.mdl_info.Bones.size());
                            while (clipper.Step()) {
                                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                                    ImGui::TableNextRow();
                                    ImGui::TableSetColumnIndex(0); ImGui::Text("%d", i);
                                    ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(S.mdl_info.Bones[i].Name.c_str());
                                    ImGui::TableSetColumnIndex(2); ImGui::Text("%d", S.mdl_info.Bones[i].ParentID);
                                }
                            }
                            clipper.End();
                            ImGui::EndTable();
                        }
                    }

                    if(!S.mdl_info.Meshes.empty()){
                        ImGui::Dummy(ImVec2(0,6));
                        ImGui::Text("Meshes");
                        ImGui::Separator();
                        for(size_t k=0;k<S.mdl_info.Meshes.size();++k){
                            const auto& m = S.mdl_info.Meshes[k];
                            std::string lbl = "Mesh " + std::to_string(k) + " - " + m.MeshName;
                            if(ImGui::TreeNode(lbl.c_str())){
                                ImGui::Text("MaterialCount: %u", m.MaterialCount);
                                if(k < S.mdl_info.MeshBuffers.size()){
                                    const auto& mb = S.mdl_info.MeshBuffers[k];
                                    ImGui::Dummy(ImVec2(0,4));
                                    ImGui::Text("Vertices: %u", mb.VertexCount);
                                    ImGui::Text("VertexOffset: 0x%zX", mb.VertexOffset);
                                    ImGui::Text("Faces: %u", mb.FaceCount);
                                    ImGui::Text("FaceOffset: 0x%zX", mb.FaceOffset);
                                    ImGui::Text("SubMeshes: %u", mb.SubMeshCount);
                                }
                                if(!m.Materials.empty()){
                                    ImGui::Dummy(ImVec2(0,4));
                                    ImGui::Text("Materials & Textures");
                                    ImGui::Separator();
                                    for(size_t mi=0; mi<m.Materials.size(); ++mi){
                                        const auto& mat = m.Materials[mi];
                                        std::string ml = "Material " + std::to_string(mi);
                                        if(ImGui::TreeNode(ml.c_str())){
                                            if(!mat.DiffuseTexName.empty())  ImGui::Text("Diffuse:  %s", std::filesystem::path(mat.DiffuseTexName).filename().string().c_str());
                                            if(!mat.NormalTexName.empty())   ImGui::Text("Normal:   %s", std::filesystem::path(mat.NormalTexName).filename().string().c_str());
                                            if(!mat.SpecularTexName.empty()) ImGui::Text("Specular: %s", std::filesystem::path(mat.SpecularTexName).filename().string().c_str());
                                            if(!mat.MetallicTexName.empty()) ImGui::Text("Metallic: %s", std::filesystem::path(mat.MetallicTexName).filename().string().c_str());
                                            if(!mat.ExtraTexName.empty())    ImGui::Text("Extra:    %s", std::filesystem::path(mat.ExtraTexName).filename().string().c_str());
                                            ImGui::TreePop();
                                        }
                                    }
                                }
                                ImGui::TreePop();
                            }
                        }
                    }
                }
            }else{
                ImGui::TextUnformatted("No parsed info");
            }
        }else{
            ImGui::TextUnformatted("No file selected");
        }
        ImGui::EndChild();

        ImGui::EndGroup();
        ImGui::EndChild();
    }
    ImGui::End();
    }

#ifdef _WIN32
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

                    std::vector<uint8_t> payload;

                    if (m.CompFlag == 7) {

                        payload.assign(src, src + src_sz);
                        if (S.tex_info.PixelFormat == 39) {

                            for (size_t i = 0; i + 16 <= payload.size(); i += 16) {
                                uint64_t alpha_bits = 0;
                                for (int j = 0; j < 6; j++) alpha_bits |= ((uint64_t)payload[i+2+j]) << (j*8);
                                uint64_t alpha_swapped = 0;
                                for (int j = 0; j < 6; j++) alpha_swapped |= ((alpha_bits >> (j*8)) & 0xFF) << ((5-j)*8);
                                for (int j = 0; j < 6; j++) payload[i+2+j] = (alpha_swapped >> (j*8)) & 0xFF;

                                size_t k = i + 8;
                                uint16_t c0 = (payload[k+0] << 8) | payload[k+1];
                                uint16_t c1 = (payload[k+2] << 8) | payload[k+3];
                                uint32_t idx = ((uint32_t)payload[k+4] << 24) | ((uint32_t)payload[k+5] << 16) | ((uint32_t)payload[k+6] << 8) | payload[k+7];
                                payload[k+0] = c0 & 0xFF; payload[k+1] = (c0 >> 8) & 0xFF;
                                payload[k+2] = c1 & 0xFF; payload[k+3] = (c1 >> 8) & 0xFF;
                                payload[k+4] = idx & 0xFF; payload[k+5] = (idx >> 8) & 0xFF;
                                payload[k+6] = (idx >> 16) & 0xFF; payload[k+7] = (idx >> 24) & 0xFF;
                            }
                        } else {

                            for (size_t i = 0; i + 8 <= payload.size(); i += 8) {
                                uint16_t c0 = (payload[i+0] << 8) | payload[i+1];
                                uint16_t c1 = (payload[i+2] << 8) | payload[i+3];
                                uint32_t idx = ((uint32_t)payload[i+4] << 24) | ((uint32_t)payload[i+5] << 16) | ((uint32_t)payload[i+6] << 8) | payload[i+7];
                                payload[i+0] = c0 & 0xFF; payload[i+1] = (c0 >> 8) & 0xFF;
                                payload[i+2] = c1 & 0xFF; payload[i+3] = (c1 >> 8) & 0xFF;
                                payload[i+4] = idx & 0xFF; payload[i+5] = (idx >> 8) & 0xFF;
                                payload[i+6] = (idx >> 16) & 0xFF; payload[i+7] = (idx >> 24) & 0xFF;
                            }
                        }
                    } else {

                        if (S.tex_info.PixelFormat != 35) {
                            std::ostringstream os;
                            os << "CompFlag=" << m.CompFlag
                               << " with PixelFormat=" << S.tex_info.PixelFormat
                               << " — only BC1 (35) compressed mips are supported by the codec port";
                            ImGui::TextUnformatted("Compressed non-BC1 preview not supported yet.");
                            if(ImGui::Button("Close", ImVec2(-1,0))) ImGui::CloseCurrentPopup();
                            ImGui::EndPopup();
                            return;
                        }

                        const size_t body_start = m.DefOffset + 48;
                        const size_t body_size  = m.DataSize;
                        if (body_start + body_size > S.hex_data.size()) {
                            ImGui::TextUnformatted("Compressed mip body out of bounds.");
                            if(ImGui::Button("Close", ImVec2(-1,0))) ImGui::CloseCurrentPopup();
                            ImGui::EndPopup();
                            return;
                        }
                        const uint8_t* body_ptr = S.hex_data.data() + body_start;

                        int dec_w = 0, dec_h = 0;
                        std::string err;
                        if (!lh_decode_compressed_mip(body_ptr, body_size, dec_w, dec_h, payload, &err)) {
                            ImGui::TextUnformatted(
                                ("Compressed mip decode failed: " + err).c_str());
                            if(ImGui::Button("Close", ImVec2(-1,0))) ImGui::CloseCurrentPopup();
                            ImGui::EndPopup();
                            return;
                        }

                        w = (uint32_t)dec_w;
                        h = (uint32_t)dec_h;
                        fmt = DXGI_FORMAT_BC1_UNORM;
                    }

                    size_t blocks_x = (w + 3) / 4;
                    const UINT block_bytes =
                        (fmt == DXGI_FORMAT_BC1_UNORM) ? 8u : 16u;
                    UINT pitch = (UINT)(blocks_x * block_bytes);

                    D3D11_TEXTURE2D_DESC td{};
                    td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1; td.Format = fmt;
                    td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                    D3D11_SUBRESOURCE_DATA sd{}; sd.pSysMem = payload.data(); sd.SysMemPitch = pitch;
                    ID3D11Texture2D* tex = nullptr;
                    HRESULT hr = device->CreateTexture2D(&td, &sd, &tex);
                    if(SUCCEEDED(hr)){
                        D3D11_SHADER_RESOURCE_VIEW_DESC svd{};
                        svd.Format = td.Format; svd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; svd.Texture2D.MipLevels = 1;
                        device->CreateShaderResourceView(tex, &svd, &S.preview_srv); tex->Release();
                    } else {
                        std::ostringstream os;
                        os << "CreateTexture2D failed (HRESULT=0x"
                           << std::hex << (uint32_t)hr << std::dec
                           << ", w=" << w << ", h=" << h
                           << ", fmt=" << (int)fmt
                           << ", payload=" << payload.size()
                           << ", pitch=" << pitch << ")";
                    }
                }
            }
            if(S.preview_srv) ImGui::Image((ImTextureID)S.preview_srv, ImVec2(512, 512));
            else ImGui::TextUnformatted("Preview unsupported or failed.");
        }else{
            ImGui::TextUnformatted("No mip selected");
        }
        if(ImGui::Button("Close", ImVec2(-1,0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    {

        if (S.show_model_preview || S.model_preview_open ||
            S.model_materials_open) {
            S.show_model_preview = false;
            S.model_preview_open = false;
            S.model_materials_open = false;
        }

        const ImVec2 canvas(960, 640);
        const ImVec2 preview_size(canvas.x + 32.0f, canvas.y + 110.0f);

        if (S.model_preview_open) {
            ImGui::SetNextWindowSize(preview_size, ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Model Preview", &S.model_preview_open,
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings))
            {
            MP_Render(device, g_mp, g_flycam);

            ImGui::BeginChild("##canvas_col", canvas, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
            ImVec2 pos = ImGui::GetCursorScreenPos();
            if(g_mp.srv) ImGui::GetWindowDrawList()->AddImage((ImTextureID)g_mp.srv, pos, ImVec2(pos.x + canvas.x, pos.y + canvas.y));
            ImGui::InvisibleButton("model_canvas", canvas);

            float dt = ImGui::GetIO().DeltaTime;
            bool canvas_hovered = ImGui::IsItemHovered();
            auto& io = ImGui::GetIO();

            if(canvas_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)){
                g_flycam.is_looking = true;
                g_flycam.saved_mouse_x = io.MousePos.x;
                g_flycam.saved_mouse_y = io.MousePos.y;
#ifdef _WIN32
                ShowCursor(FALSE);
#endif
            }
            if(ImGui::IsMouseReleased(ImGuiMouseButton_Right)){
                g_flycam.is_looking = false;
#ifdef _WIN32
                ShowCursor(TRUE);
                SetCursorPos((int)g_flycam.saved_mouse_x, (int)g_flycam.saved_mouse_y);
#endif
            }

            float mdx = 0.0f, mdy = 0.0f;
            if(g_flycam.is_looking){
#ifdef _WIN32
                POINT cur;
                GetCursorPos(&cur);
                mdx = (float)(cur.x - (int)g_flycam.saved_mouse_x);
                mdy = (float)(cur.y - (int)g_flycam.saved_mouse_y);
                SetCursorPos((int)g_flycam.saved_mouse_x, (int)g_flycam.saved_mouse_y);
#else
                mdx = io.MouseDelta.x;
                mdy = io.MouseDelta.y;
#endif
            }

            bool kw = ImGui::IsKeyDown(S.key_forward);
            bool ks = ImGui::IsKeyDown(S.key_back);
            bool ka = ImGui::IsKeyDown(S.key_left);
            bool kd = ImGui::IsKeyDown(S.key_right);
            bool kq = ImGui::IsKeyDown(S.key_down);
            bool ke = ImGui::IsKeyDown(S.key_up);

            FlyCam_Update(g_flycam, dt, kw, ks, ka, kd, kq, ke, mdx, mdy);

            if(canvas_hovered){
                float wheel = io.MouseWheel;
                if(fabsf(wheel) > 0.0001f){
                    float cy_ = cosf(g_flycam.yaw), sy_ = sinf(g_flycam.yaw);
                    float cp_ = cosf(g_flycam.pitch), sp_ = sinf(g_flycam.pitch);
                    float step = g_mp.radius * 0.15f * wheel;
                    g_flycam.pos[0] += sy_ * cp_ * step;
                    g_flycam.pos[1] += sp_ * step;
                    g_flycam.pos[2] += cy_ * cp_ * step;
                }
            }

            ImGui::Text("Right-drag: look  WASD/QE: move  Scroll: zoom");

            ImGui::Dummy(ImVec2(0,4));
            if(ImGui::Button("Reset Camera", ImVec2(120,0)))
                FlyCam_Reset(g_flycam, g_mp.center[0], g_mp.center[1], g_mp.center[2], g_mp.radius);
            ImGui::SameLine();
            if(ImGui::Button("Close", ImVec2(-1,0))) {
                S.model_preview_open = false;
            }
            ImGui::EndChild();
            }
            ImGui::End();

            if (!S.model_preview_open) {
                MP_Release(g_mp);
                S.model_materials_open = false;
            }
        }

        if (S.model_materials_open) {
            ImGui::SetNextWindowSize(ImVec2(380.0f, 600.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Materials & Textures", &S.model_materials_open,
                             ImGuiWindowFlags_NoCollapse))
            {

            if (g_mp.has_model && !g_mp.meshes.empty()) {

                struct TexEntry {
                    std::string name;
                    std::vector<size_t> mesh_idx;
                    bool any_visible = false;
                };
                enum Slot { SLOT_DIFFUSE=0, SLOT_NORMAL=1, SLOT_SPEC=2, SLOT_METALLIC=3, SLOT_EXTRA=4, SLOT_COUNT=5 };
                const char* slot_label[SLOT_COUNT] = { "Diffuse", "Normal", "Specular", "Metallic", "Extra" };
                std::vector<TexEntry> entries[SLOT_COUNT];

                auto get_name = [&](const MPPerMesh& m, int s) -> const std::string& {
                    switch (s) {
                        case SLOT_DIFFUSE:  return m.diffuse_tex_name;
                        case SLOT_NORMAL:   return m.normal_tex_name;
                        case SLOT_SPEC:     return m.specular_tex_name;
                        case SLOT_METALLIC: return m.metallic_tex_name;
                        default:            return m.extra_tex_name;
                    }
                };
                auto get_visible = [&](const MPPerMesh& m, int s) -> bool {
                    switch (s) {
                        case SLOT_DIFFUSE:  return m.diffuse_visible;
                        case SLOT_NORMAL:   return m.normal_visible;
                        case SLOT_SPEC:     return m.specular_visible;
                        case SLOT_METALLIC: return m.metallic_visible;
                        default:            return m.extra_visible;
                    }
                };
                auto set_visible = [&](MPPerMesh& m, int s, bool v) {
                    switch (s) {
                        case SLOT_DIFFUSE:  m.diffuse_visible  = v; break;
                        case SLOT_NORMAL:   m.normal_visible   = v; break;
                        case SLOT_SPEC:     m.specular_visible = v; break;
                        case SLOT_METALLIC: m.metallic_visible = v; break;
                        default:            m.extra_visible    = v; break;
                    }
                };

                for (int s = 0; s < SLOT_COUNT; ++s) {
                    for (size_t i = 0; i < g_mp.meshes.size(); ++i) {
                        const std::string& nm = get_name(g_mp.meshes[i], s);
                        if (nm.empty()) continue;
                        auto it = std::find_if(entries[s].begin(), entries[s].end(),
                            [&](const TexEntry& e) { return e.name == nm; });
                        if (it == entries[s].end()) {
                            TexEntry e; e.name = nm; e.mesh_idx.push_back(i);
                            e.any_visible = get_visible(g_mp.meshes[i], s);
                            entries[s].push_back(e);
                        } else {
                            it->mesh_idx.push_back(i);
                            it->any_visible = it->any_visible || get_visible(g_mp.meshes[i], s);
                        }
                    }
                }

                bool any_textures = false;
                for (int s = 0; s < SLOT_COUNT; ++s) any_textures |= !entries[s].empty();

                if (!any_textures) {
                    ImGui::TextDisabled("(no textures referenced)");
                } else {
                    if (ImGui::Button("Show All", ImVec2(140, 0))) {
                        for (auto& mm : g_mp.meshes) {
                            mm.diffuse_visible = mm.normal_visible =
                            mm.specular_visible = mm.metallic_visible =
                            mm.extra_visible = true;
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Hide All", ImVec2(140, 0))) {
                        for (auto& mm : g_mp.meshes) {
                            mm.diffuse_visible = mm.normal_visible =
                            mm.specular_visible = mm.metallic_visible =
                            mm.extra_visible = false;
                        }
                    }
                    ImGui::Separator();

                    for (int s = 0; s < SLOT_COUNT; ++s) {
                        if (entries[s].empty()) continue;
                        ImGui::PushID(s);
                        if (ImGui::CollapsingHeader(slot_label[s],
                                                    ImGuiTreeNodeFlags_DefaultOpen)) {
                            ImGui::Indent(8.0f);
                            for (size_t ei = 0; ei < entries[s].size(); ++ei) {
                                auto& e = entries[s][ei];
                                bool v = e.any_visible;
                                std::string fname = std::filesystem::path(e.name)
                                    .filename().string();
                                std::string label = fname + "##slot_" + std::to_string(s)
                                    + "_" + std::to_string(ei);
                                if (ImGui::Checkbox(label.c_str(), &v)) {
                                    for (size_t mi : e.mesh_idx) {
                                        set_visible(g_mp.meshes[mi], s, v);
                                    }
                                }
                                if (e.mesh_idx.size() > 1) {
                                    ImGui::SameLine();
                                    ImGui::TextDisabled("(%zu)", e.mesh_idx.size());
                                }
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip("%s", e.name.c_str());
                                }
                            }
                            ImGui::Unindent(8.0f);
                        }
                        ImGui::PopID();
                    }

                    ImGui::Separator();
                    ImGui::TextDisabled("%zu meshes loaded", g_mp.meshes.size());
                }
            } else {
                ImGui::TextDisabled("(no model loaded)");
            }

            {
                const auto& palette = TerrainTextureRegistry::GetLodPalette();
                if (!palette.empty()) {
                    ImGui::Separator();
                    if (ImGui::CollapsingHeader(
                            ".ehf LOD Palette",
                            ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        ImGui::TextDisabled("%zu materials referenced by .ehf",
                                            palette.size());
                        ImGui::Indent(8.0f);
                        for (size_t i = 0; i < palette.size(); ++i) {
                            const auto& e = palette[i];
                            auto basename = [](const std::string& s) {
                                if (s.empty()) return std::string("(none)");
                                size_t pos = s.find_last_of("/\\");
                                return (pos == std::string::npos)
                                    ? s : s.substr(pos + 1);
                            };
                            ImGui::PushID(int(i));
                            if (ImGui::TreeNodeEx((void*)i,
                                ImGuiTreeNodeFlags_DefaultOpen,
                                "[%zu] %s", i,
                                basename(e.base_diffuse).c_str()))
                            {
                                auto row = [](const char* tag,
                                              const std::string& path) {
                                    ImGui::TextUnformatted(tag);
                                    ImGui::SameLine(110.0f);
                                    if (path.empty()) {
                                        ImGui::TextDisabled("(none)");
                                    } else {
                                        size_t pos = path.find_last_of("/\\");
                                        std::string bn = (pos == std::string::npos)
                                            ? path : path.substr(pos + 1);
                                        ImGui::TextUnformatted(bn.c_str());
                                        if (ImGui::IsItemHovered()) {
                                            ImGui::SetTooltip("%s", path.c_str());
                                        }
                                    }
                                };
                                row("base diffuse",   e.base_diffuse);
                                row("base normal",    e.base_normal);
                                row("detail diffuse", e.detail_diffuse);
                                row("detail normal",  e.detail_normal);
                                ImGui::TreePop();
                            }
                            ImGui::PopID();
                        }
                        ImGui::Unindent(8.0f);
                    }
                }
            }
            }
            ImGui::End();
        }
    }
#endif
}
