void on_export_mdl_to_glb() {
    int idx = S.selected_file_index;
    if (idx < 0 || idx >= (int)S.files.size()) {
        show_error_box("No file selected.");
        return;
    }

    auto item = S.files[(size_t)idx];
    std::string name = item.name;
    std::string name_lower = name;
    std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);

    if (name_lower.size() < 4 || name_lower.substr(name_lower.size() - 4) != ".mdl") {
        show_error_box("Selected file is not .mdl");
        return;
    }

    auto base_out = (std::filesystem::current_path() / "exported_glb").string();
    progress_open(1, "Exporting GLB...");
    progress_update(0, 1, name);

    std::thread([item, name, base_out]() {
        if (!S.cancel_requested && !S.exiting) {
            try {
                std::vector<unsigned char> mdl_buf;
                if (!build_mdl_buffer_for_name(name, mdl_buf)) {
                    progress_done();
                    show_error_box("Failed to build MDL buffer");
                    return;
                }

                std::string output_filename = apply_folder_prefix_to_filename(name, ".glb");
                auto out_path = std::filesystem::path(base_out) / output_filename;
                std::filesystem::create_directories(out_path.parent_path());

                std::string err;
                if (!mdl_to_glb_full(mdl_buf, out_path.string(), name, err)) {
                    progress_done();
                    show_error_box("GLB export failed: " + err);
                    return;
                }
            } catch (...) {
                progress_done();
                show_error_box("Exception during export");
                return;
            }
        }
        progress_update(1, 1, name);
        progress_done();
        if (!S.cancel_requested) {
            show_completion_box(
                std::string("GLB export complete.\n\nOutput folder:\n") +
                std::filesystem::absolute(base_out).string());
        }
        S.cancel_requested = false;
    }).detach();
}
