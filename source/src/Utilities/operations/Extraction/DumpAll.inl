void on_dump_all_raw() {
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
    if (S.files.empty()) {
        show_error_box("No files to dump in this BNK.");
        return;
    }
    auto base_out = (std::filesystem::current_path() / "extracted").string();
    int total = (int) S.files.size();
    progress_open(total, "Dumping...");
    progress_update(0, total, "Starting...");
    std::thread([base_out,total,bnk_to_use]() {
        std::atomic<int> dumped{0};
        std::mutex fail_m;
        std::vector<std::string> failed;
        auto work = [&](const BNKItemUI &it) {
            if (S.cancel_requested || S.exiting) return;
            try { extract_file_one(bnk_to_use, it, base_out, false); } catch (...) {
                std::lock_guard<std::mutex> lk(fail_m);
                failed.push_back(it.name);
            }
            int cur = ++dumped;
            progress_update(cur, total, std::filesystem::path(it.name).filename().string());
        };
        if (!S.cancel_requested) {
            std::vector<std::thread> pool;
            int n = std::min(8, std::max(1, (int) std::thread::hardware_concurrency()));
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
        std::string msg = std::string("Dump complete.\n\nOutput folder:\n") + std::filesystem::absolute(base_out).
                          string();
        if (!failed.empty()) {
            msg += std::string("\nFailed: ") + std::to_string((int) failed.size());
        }
        show_completion_box(msg);
        S.cancel_requested = false;
    }).detach();
}
