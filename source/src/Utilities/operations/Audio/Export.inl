void on_export_wavs() {
    if (S.selected_bnk.empty()) {
        show_error_box("No BNK selected.");
        return;
    }
    std::vector<BNKItemUI> audio_files;
    for (auto &f: S.files) if (is_audio_file(f.name)) audio_files.push_back(f);
    if (audio_files.empty()) {
        show_error_box("No .wav files in this BNK.");
        return;
    }
    auto base_out = (std::filesystem::current_path() / "extracted").string();
    int total = (int) audio_files.size();
    progress_open(total, "Exporting WAVs...");
    progress_update(0, total, "Starting...");
    std::thread([audio_files,base_out,total]() {
        std::atomic<int> done{0};
        std::mutex fail_m;
        std::vector<std::string> failed;
        auto work = [&](const BNKItemUI &it) {
            if (S.cancel_requested || S.exiting) return;
            try { extract_file_one(S.selected_bnk, it, base_out, true); } catch (...) {
                std::lock_guard<std::mutex> lk(fail_m);
                failed.push_back(it.name);
            }
            int cur = ++done;
            progress_update(cur, total, std::filesystem::path(it.name).filename().string());
        };
        if (!S.cancel_requested) {
            std::vector<std::thread> pool;
            int n = std::min(4, std::max(1, (int) std::thread::hardware_concurrency() / 2));
            std::atomic<size_t> i{0};
            for (int t = 0; t < n; ++t) pool.emplace_back([&]() {
                for (;;) {
                    size_t k = i.fetch_add(1);
                    if (k >= audio_files.size()) break;
                    work(audio_files[k]);
                }
            });
            for (auto &th: pool) th.join();
        }
        progress_done();
        std::string msg = std::string("WAV export complete.\n\nOutput folder:\n") + std::filesystem::absolute(base_out).
                          string();
        if (!failed.empty()) {
            msg += std::string("\nFailed: ") + std::to_string((int) failed.size());
        }
        show_completion_box(msg);
        S.cancel_requested = false;
    }).detach();
}
