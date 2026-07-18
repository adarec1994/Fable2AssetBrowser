void on_dump_all_global(const std::vector<GlobalHit>& hits) {
    if (hits.empty()) {
        show_error_box("No files to dump.");
        return;
    }

    auto base_out = (std::filesystem::current_path() / "extracted").string();
    int total = (int)hits.size();
    progress_open(total, "Dumping...");
    progress_update(0, total, "Starting...");

    std::thread([hits, base_out, total]() {
        std::atomic<int> dumped{0};
        std::mutex fail_m;
        std::vector<std::string> failed;

        auto work = [&](const GlobalHit &h) {
            if (S.cancel_requested || S.exiting) return;
            try {
                BNKItemUI item;
                item.index = h.index;
                item.name = h.file_name;
                item.size = h.size;
                extract_file_one(h.bnk_path, item, base_out, false);
            } catch (...) {
                std::lock_guard<std::mutex> lk(fail_m);
                failed.push_back(h.file_name);
            }
            int cur = ++dumped;
            progress_update(cur, total, std::filesystem::path(h.file_name).filename().string());
        };

        if (!S.cancel_requested) {
            std::vector<std::thread> pool;
            int n = std::min(8, std::max(1, (int)std::thread::hardware_concurrency()));
            std::atomic<size_t> i{0};
            for (int t = 0; t < n; ++t) pool.emplace_back([&]() {
                for (;;) {
                    size_t k = i.fetch_add(1);
                    if (k >= hits.size()) break;
                    work(hits[k]);
                }
            });
            for (auto &th: pool) th.join();
        }

        progress_done();
        std::string msg = std::string("Dump complete.\n\nOutput folder:\n") + std::filesystem::absolute(base_out).string();
        if (!failed.empty()) {
            msg += std::string("\nFailed: ") + std::to_string((int)failed.size());
        }
        show_completion_box(msg);
        S.cancel_requested = false;
    }).detach();
}
