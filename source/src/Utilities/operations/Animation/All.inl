void on_extract_all_adb() {
    if (!S.viewing_adb) {
        show_error_box("Not viewing Audio Database.");
        return;
    }
    if (S.files.empty()) {
        show_error_box("No ADB files to extract.");
        return;
    }

    auto base_out = (std::filesystem::current_path() / "extracted" / "audio_database").string();
    int total = (int)S.files.size();
    progress_open(total, "Extracting ADB files...");
    progress_update(0, total, "Starting...");

    std::thread([base_out, total]() {
        std::atomic<int> extracted{0};
        std::mutex fail_m;
        std::vector<std::string> failed;

        auto work = [&](const BNKItemUI &it) {
            if (S.cancel_requested || S.exiting) return;
            try {
                std::filesystem::create_directories(base_out);
                auto entries = decompress_adb(it.name);

                for (const auto& entry : entries) {
                    auto output_path = std::filesystem::path(base_out) / entry.name;
                    std::ofstream out(output_path, std::ios::binary);
                    out.write((char*)entry.data.data(), entry.data.size());
                }
            } catch (...) {
                std::lock_guard<std::mutex> lk(fail_m);
                failed.push_back(it.name);
            }
            int cur = ++extracted;
            progress_update(cur, total, std::filesystem::path(it.name).filename().string());
        };

        if (!S.cancel_requested) {
            std::vector<std::thread> pool;
            int n = std::min(4, std::max(1, (int)std::thread::hardware_concurrency() / 2));
            std::atomic<size_t> i{0};
            for (int t = 0; t < n; ++t) pool.emplace_back([&]() {
                for (;;) {
                    size_t k = i.fetch_add(1);
                    if (k >= S.files.size()) break;
                    work(S.files[k]);
                }
            });
            for (auto &th: pool) th.join();
        }

        progress_done();
        std::string msg = std::string("ADB extraction complete.\n\nOutput folder:\n") + std::filesystem::absolute(base_out).string();
        if (!failed.empty()) {
            msg += std::string("\nFailed: ") + std::to_string((int)failed.size());
        }
        show_completion_box(msg);
        S.cancel_requested = false;
    }).detach();
}
