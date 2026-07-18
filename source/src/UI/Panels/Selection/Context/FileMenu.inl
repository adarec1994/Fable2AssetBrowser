void file_context_menu(const std::string& bnk_path,
                       int file_index, bool is_nested,
                       const std::string& file_name) {
    const bool is_tex   = is_tex_file(file_name);

    if (ImGui::BeginPopupContextItem()) {
        if (is_tex) {

            std::string backup_error;
            const bool backup_ready =
                GameBackup::RequireBackup(backup_error);
            if (!backup_ready) {
                ImGui::MenuItem(
                    "Replace (UNAVAILABLE WITHOUT BACKUP)", nullptr,
                    false, false);
            } else {
                const bool replace_busy = ImportDialog::Busy();
                if (replace_busy) ImGui::BeginDisabled();
                if (ImGui::MenuItem("Replace...")) {
                    ImportDialog::OpenTextureReplacement(
                        bnk_path, file_index, file_name);
                }
                if (replace_busy) ImGui::EndDisabled();
            }
            ImGui::Separator();

            tex_export_menu_named(file_name, file_name, bnk_path,
                                  0);
        } else if (is_mdl_file(file_name)) {

            if (ImGui::BeginMenu("Export to")) {
                if (ImGui::MenuItem("GLB")) {
                    ISO::mdl_export_begin_named(
                        ISO::MdlExportFormat::GLB,
                        bnk_path, file_index, file_name, is_nested);
                }
                if (ImGui::MenuItem("FBX")) {
                    ISO::mdl_export_begin_named(
                        ISO::MdlExportFormat::FBX,
                        bnk_path, file_index, file_name, is_nested);
                }
                ImGui::Separator();
                if (ImGui::MenuItem(".mdl (raw)")) {
                    ISO::mdl_export_begin_named(
                        ISO::MdlExportFormat::RAW,
                        bnk_path, file_index, file_name, is_nested);
                }
                ImGui::EndMenu();
            }
        } else if (is_audio_file(file_name)) {

            if (ImGui::BeginMenu("Export to")) {
                if (ImGui::MenuItem("MP3")) {
                    asset_export_audio_encoded(bnk_path, file_index,
                                               file_name, false);
                }
                if (ImGui::MenuItem("M4A")) {
                    asset_export_audio_encoded(bnk_path, file_index,
                                               file_name, true);
                }
                if (ImGui::MenuItem("WAV")) {
                    asset_export_to_export_dir(bnk_path, file_index,
                                               is_nested, file_name,
                                               true);
                }
                ImGui::Separator();
                if (ImGui::MenuItem(".xma (raw)")) {
                    asset_export_audio_raw_xma(bnk_path, file_index,
                                               file_name);
                }
                ImGui::EndMenu();
            }
        } else {
            if (ImGui::MenuItem("Export")) {
                asset_export_to_export_dir(bnk_path, file_index,
                                           is_nested, file_name);
            }
        }
        ImGui::EndPopup();
    }
}
