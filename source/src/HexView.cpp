#include "HexView.h"
#include "State.h"
#include "Utils.h"
#include "Progress.h"
#include "files.h"
#include "TexParser.h"
#include "ModelParser.h"
#include "ModelPreview.h"
#include "BNKCore.cpp"
#include "imgui.h"
#include "imgui_hex.h"
#include <thread>
#include <filesystem>
#include <algorithm>
#include <d3d11.h>
#include <cmath>

static ModelPreview g_mp;

static void reset_preview_resources() {
    if (S.preview_srv) { S.preview_srv->Release(); S.preview_srv = nullptr; }
    if (S.model_diffuse_srv) { S.model_diffuse_srv->Release(); S.model_diffuse_srv = nullptr; }
    MP_Release(g_mp);
    S.preview_mip_index = -1;
    S.show_preview_popup = false;
    S.tex_info_ok = false;
    S.mdl_info_ok = false;
    S.show_model_preview = false;
    S.mdl_meshes.clear();
    S.cam_yaw = 0.0f; S.cam_pitch = 0.2f; S.cam_dist = 3.0f;
}

static bool is_bc_format(uint32_t comp_flag, DXGI_FORMAT& out_fmt) {
    if (comp_flag == 7) { out_fmt = DXGI_FORMAT_BC1_UNORM; return true; }
    if (comp_flag == 8) { out_fmt = DXGI_FORMAT_BC2_UNORM; return true; }
    if (comp_flag == 9) { out_fmt = DXGI_FORMAT_BC3_UNORM; return true; }
    if (comp_flag == 10) { out_fmt = DXGI_FORMAT_BC4_UNORM; return true; }
    if (comp_flag == 11) { out_fmt = DXGI_FORMAT_BC5_UNORM; return true; }
    return false;
}

void open_hex_for_selected() {
    int idx = S.selected_file_index;
    if (idx < 0 || idx >= (int) S.files.size()) {
        show_error_box("No file selected.");
        return;
    }
    if (S.selected_bnk.empty()) {
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
    std::thread([item, name, want_tex, want_mdl]() {
        std::vector<unsigned char> buf;
        bool ok = false;
        try {
            if (want_tex) {
                ok = build_tex_buffer_for_name(name, buf);
            } else if (want_mdl) {
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
        S.hex_data.clear();
        if (ok) S.hex_data.swap(buf);
        S.hex_title = std::string("Hex Editor - ") + name;
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

void draw_hex_window(ID3D11Device *device) {
    if(!S.hex_open) return;
    if(S.hex_loading.load()) return;
    if(S.hex_data.empty()){ S.hex_open = false; return; }

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
                reset_preview_resources();
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
                            if(m.CompFlag == 7){
                                if(ImGui::Button("Preview")){
                                    S.preview_mip_index = i;
                                    S.show_preview_popup = true;
                                }
                            }
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
                    S.mdl_info_ok = parse_mdl_info(S.hex_data, S.mdl_info);
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

                    if(ImGui::Button("Preview")){
                        progress_open(0, "Loading model preview...");

                        std::thread([device]() {
                            S.mdl_meshes.clear();
                            parse_mdl_geometry(S.hex_data, S.mdl_info, S.mdl_meshes);
                            MP_Release(g_mp);
                            MP_Init(device, g_mp, 800, 520);
                            MP_Build(device, S.mdl_meshes, S.mdl_info, g_mp);
                            S.cam_yaw = 0.0f; S.cam_pitch = 0.2f; S.cam_dist = 3.0f;

                            progress_done();
                            S.show_model_preview = true;
                        }).detach();
                    }

                    if(!S.mdl_info.Bones.empty()){
                        ImGui::Dummy(ImVec2(0,6));
                        ImGui::Text("Bones");
                        ImGui::Separator();
                        if(ImGui::BeginTable("bones_tbl", 3, ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollY, ImVec2(0, 200))){
                            ImGui::TableSetupColumn("Idx", ImGuiTableColumnFlags_WidthFixed, 48.0f);
                            ImGui::TableSetupColumn("Name");
                            ImGui::TableSetupColumn("Parent");
                            ImGui::TableHeadersRow();
                            for(size_t i=0;i<S.mdl_info.Bones.size();++i){
                                ImGui::TableNextRow();
                                ImGui::TableSetColumnIndex(0); ImGui::Text("%d", (int)i);
                                ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(S.mdl_info.Bones[i].Name.c_str());
                                ImGui::TableSetColumnIndex(2); ImGui::Text("%d", S.mdl_info.Bones[i].ParentID);
                            }
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
                                            if(!mat.TextureName.empty())     ImGui::Text("Diffuse:  %s", std::filesystem::path(mat.TextureName).filename().string().c_str());
                                            if(!mat.NormalMapName.empty())   ImGui::Text("Normal:   %s", std::filesystem::path(mat.NormalMapName).filename().string().c_str());
                                            if(!mat.SpecularMapName.empty()) ImGui::Text("Specular: %s", std::filesystem::path(mat.SpecularMapName).filename().string().c_str());
                                            if(!mat.TintName.empty())        ImGui::Text("Tint:     %s", std::filesystem::path(mat.TintName).filename().string().c_str());
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
        if(ImGui::Button("Close", ImVec2(-1,0))) ImGui::CloseCurrentPopup();
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
}