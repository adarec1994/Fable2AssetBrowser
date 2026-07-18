void on_extract_selected_wav() {
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
    if (!is_audio_file(item.name)) {
        show_error_box("Selected file is not .wav");
        return;
    }
    auto base_out = (std::filesystem::current_path() / "extracted").string();
    progress_open(1, "Exporting WAV...");
    progress_update(0, 1, item.name);
    std::thread([item,base_out]() {
        if (!S.cancel_requested && !S.exiting) {
            try { extract_file_one(S.selected_bnk, item, base_out, true); } catch (...) {
            }
        }
        progress_update(1, 1, item.name);
        progress_done();
        if (!S.cancel_requested) show_completion_box(
            std::string("WAV export complete.\n\nOutput folder:\n") + std::filesystem::absolute(base_out).string());
        S.cancel_requested = false;
    }).detach();
}
