void on_export_all_mdl_to_glb() {
    std::vector<BNKItemUI> mdl_files;
    for (auto &f: S.files) {
        std::string name_lower = f.name;
        std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
        if (name_lower.size() >= 4 && name_lower.substr(name_lower.size() - 4) == ".mdl") {
            mdl_files.push_back(f);
        }
    }

    if (mdl_files.empty()) {
        show_error_box("No .mdl files in this BNK.");
        return;
    }

    auto base_out = (std::filesystem::current_path() / "exported_glb").string();
    int total = (int)mdl_files.size();
    progress_open(total, "Exporting GLBs...");
    progress_update(0, total, "Starting...");

    std::thread([mdl_files, base_out, total]() {
        std::atomic<int> done{0};
        std::mutex fail_m;
        std::vector<std::string> failed;

        auto work = [&](const BNKItemUI &it) {
            if (S.cancel_requested || S.exiting) return;
            try {
                std::vector<unsigned char> mdl_buf;
                if (!build_mdl_buffer_for_name(it.name, mdl_buf)) {
                    std::lock_guard<std::mutex> lk(fail_m);
                    failed.push_back(it.name);
                    return;
                }

                std::string output_filename = apply_folder_prefix_to_filename(it.name, ".glb");
                auto out_path = std::filesystem::path(base_out) / output_filename;
                std::filesystem::create_directories(out_path.parent_path());

                std::string err;
                if (!mdl_to_glb_full(mdl_buf, out_path.string(), it.name, err)) {
                    std::lock_guard<std::mutex> lk(fail_m);
                    failed.push_back(it.name);
                }
            } catch (...) {
                std::lock_guard<std::mutex> lk(fail_m);
                failed.push_back(it.name);
            }
            int cur = ++done;
            progress_update(cur, total, std::filesystem::path(it.name).filename().string());
        };

        if (!S.cancel_requested) {
            std::vector<std::thread> pool;
            int n = std::min(4, std::max(1, (int)std::thread::hardware_concurrency() / 2));
            std::atomic<size_t> i{0};
            for (int t = 0; t < n; ++t) pool.emplace_back([&]() {
                for (;;) {
                    size_t k = i.fetch_add(1);
                    if (k >= mdl_files.size()) break;
                    work(mdl_files[k]);
                }
            });
            for (auto &th: pool) th.join();
        }

        progress_done();
        std::string msg = std::string("GLB export complete.\n\nOutput folder:\n") +
                         std::filesystem::absolute(base_out).string();
        if (!failed.empty()) {
            msg += std::string("\nFailed: ") + std::to_string((int)failed.size());
        }
        show_completion_box(msg);
        S.cancel_requested = false;
    }).detach();
}
