void extract_file_one(const std::string &bnk_path, const BNKItemUI &item, const std::string &base_out_dir,
                             bool convert_audio) {
    std::filesystem::create_directories(base_out_dir);
    auto dst = std::filesystem::path(base_out_dir) / item.name;
    std::filesystem::create_directories(dst.parent_path());
    extract_one(bnk_path, item.index, dst.string());
    if (convert_audio && is_audio_file(item.name)) {

        auto raw = read_all_bytes(dst);
        if (!raw.empty()) {
            std::vector<uint8_t> src(raw.begin(), raw.end());
            std::string err;
            XmaDecoder::decode_xma_wav_file_to_pcm_wav(src, dst.string(), &err);
        }
    }
}

void on_extract_selected_raw() {
    int idx = S.selected_file_index;
    if (idx < 0 || idx >= (int) S.files.size()) {
        show_error_box("No file selected.");
        return;
    }

    std::string bnk_to_use;
    if (S.selected_nested_index != -1 && !S.selected_nested_temp_path.empty()) {
        bnk_to_use = S.selected_nested_temp_path;
    } else {
        bnk_to_use = S.selected_bnk;
    }

    if (bnk_to_use.empty()) {
        show_error_box("No BNK selected.");
        return;
    }
    auto item = S.files[(size_t) idx];
    auto base_out = (std::filesystem::current_path() / "extracted").string();
    progress_open(1, "Extracting File...");
    progress_update(0, 1, item.name);
    std::thread([item,base_out,bnk_to_use]() {
        if (!S.cancel_requested && !S.exiting) {
            try { extract_file_one(bnk_to_use, item, base_out, false); } catch (...) {
            }
        }
        progress_update(1, 1, item.name);
        progress_done();
        if (!S.cancel_requested) show_completion_box(
            std::string("Extraction complete.\n\nOutput folder:\n") + std::filesystem::absolute(base_out).string());
        S.cancel_requested = false;
    }).detach();
}
