void on_extract_adb_selected() {
    int idx = S.selected_file_index;
    if (idx < 0 || idx >= (int)S.files.size()) {
        show_error_box("No file selected.");
        return;
    }
    if (!S.viewing_adb) {
        show_error_box("Not viewing Audio Database.");
        return;
    }

    auto item = S.files[(size_t)idx];
    auto base_out = (std::filesystem::current_path() / "extracted" / "audio_database").string();

    progress_open(1, "Extracting ADB...");
    progress_update(0, 1, item.name);

    std::thread([item, base_out]() {
        if (!S.cancel_requested && !S.exiting) {
            try {
                std::filesystem::create_directories(base_out);
                auto entries = decompress_adb(item.name);

                for (const auto& entry : entries) {
                    auto output_path = std::filesystem::path(base_out) / entry.name;
                    std::ofstream out(output_path, std::ios::binary);
                    out.write((char*)entry.data.data(), entry.data.size());
                }
            } catch (...) {}
        }
        progress_update(1, 1, item.name);
        progress_done();
        if (!S.cancel_requested) show_completion_box(
            std::string("ADB extraction complete.\n\nOutput folder:\n") + std::filesystem::absolute(base_out).string());
        S.cancel_requested = false;
    }).detach();
}
