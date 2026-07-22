        if (s_active_tab == 2) {

            const float footer_h = ImGui::GetFrameHeightWithSpacing();
            draw_flat_asset_tab("Models", S.all_mdl_files, S.mdl_filter,
                                "models_list", 0, footer_h,
                                true, "F2_MODEL");

            const bool has_any = !S.all_mdl_files.empty();
            if (!has_any) ImGui::BeginDisabled();
            if (ImGui::Button("Extract All as...##mdl_extract_all_as",
                              ImVec2(-1, 0))) {
                ImGui::OpenPopup("##mdl_extract_all_as_popup");
            }
            if (!has_any) ImGui::EndDisabled();
            if (!has_any && !S.hide_tooltips &&
                ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(
                    "No MDLs indexed yet - open a Fable 2 root "
                    "(folder or ISO) to populate this list.");
                ImGui::EndTooltip();
            }
            if (ImGui::BeginPopup("##mdl_extract_all_as_popup")) {
                if (ImGui::MenuItem("GLB")) {
                    ISO::dump_mdl_files_as(ISO::MdlExportFormat::GLB);
                }
                if (ImGui::MenuItem("FBX")) {
                    ISO::dump_mdl_files_as(ISO::MdlExportFormat::FBX);
                }
                ImGui::Separator();
                if (ImGui::MenuItem(".mdl (raw)")) {
                    ISO::dump_mdl_files_as(ISO::MdlExportFormat::RAW);
                }
                ImGui::EndPopup();
            }
        }
        if (s_active_tab == 3) {

            const float footer_h = ImGui::GetFrameHeightWithSpacing();
            draw_flat_asset_tab("Textures", S.all_tex_files, S.tex_filter,
                                "textures_list", 1, footer_h,
                                true, "F2_TEXTURE");

            const bool has_any = !S.all_tex_files.empty();
            if (!has_any) ImGui::BeginDisabled();
            if (ImGui::Button("Extract All as...##tex_extract_all_as",
                              ImVec2(-1, 0))) {
                ImGui::OpenPopup("##tex_extract_all_as_popup");
            }
            if (!has_any) ImGui::EndDisabled();
            if (!has_any && !S.hide_tooltips &&
                ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(
                    "No textures indexed yet - open a Fable 2 root "
                    "(folder or ISO) to populate this list.");
                ImGui::EndTooltip();
            }
            if (ImGui::BeginPopup("##tex_extract_all_as_popup")) {
                if (ImGui::MenuItem("PNG")) {
                    ISO::dump_tex_files_as(TexExportFormat::PNG);
                }
                if (ImGui::MenuItem("JPG")) {
                    ISO::dump_tex_files_as(TexExportFormat::JPG);
                }
                if (ImGui::MenuItem("TIFF")) {
                    ISO::dump_tex_files_as(TexExportFormat::TIFF);
                }
                if (ImGui::MenuItem("DDS")) {
                    ISO::dump_tex_files_as(TexExportFormat::DDS);
                }
                ImGui::Separator();
                if (ImGui::MenuItem(".tex (raw)")) {

                    ISO::dump_tex_files();
                }
                ImGui::EndPopup();
            }
        }
        if (s_active_tab == 4) {

            const float footer_h = ImGui::GetFrameHeightWithSpacing();
            draw_flat_asset_tab("Audio", S.all_wav_files, S.wav_filter,
                                "audio_list", 2, footer_h);

            const bool has_any = !S.all_wav_files.empty();
            if (!has_any) ImGui::BeginDisabled();
            if (ImGui::Button("Extract All as...##wav_extract_all_as",
                              ImVec2(-1, 0))) {
                ImGui::OpenPopup("##wav_extract_all_as_popup");
            }
            if (!has_any) ImGui::EndDisabled();
            if (!has_any && !S.hide_tooltips &&
                ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(
                    "No audio indexed yet - open a Fable 2 root "
                    "(folder or ISO) to populate this list.");
                ImGui::EndTooltip();
            }
            if (ImGui::BeginPopup("##wav_extract_all_as_popup")) {
                if (ImGui::MenuItem("WAV (PCM)")) {
                    ISO::dump_wav_files_as(
                        ISO::AudioExportFormat::WAV_PCM);
                }
                if (ImGui::MenuItem(".wav (raw XMA)")) {
                    ISO::dump_wav_files_as(
                        ISO::AudioExportFormat::WAV_RAW);
                }
                ImGui::Separator();

                if (ImGui::MenuItem("MP3")) {
                    ISO::dump_wav_files_as(
                        ISO::AudioExportFormat::MP3);
                }
                if (ImGui::MenuItem("AAC (.m4a)")) {
                    ISO::dump_wav_files_as(
                        ISO::AudioExportFormat::AAC);
                }
                ImGui::EndPopup();
            }
        }
