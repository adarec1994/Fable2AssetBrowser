void on_rebuild_and_extract_models() {
    bool is_nested = (S.selected_nested_index != -1 && !S.selected_nested_temp_path.empty());

    if (is_nested) {
        std::vector<BNKItemUI> mdl_files;
        for (auto &f: S.files) {
            if (is_mdl_file(f.name)) {
                mdl_files.push_back(f);
            }
        }

        if (mdl_files.empty()) {
            show_error_box("No .mdl files in this BNK.");
            return;
        }

        auto p_headers = find_bnk_by_filename("globals_model_headers.bnk");
        if (!p_headers) {
            show_error_box("globals_model_headers.bnk not found.");
            return;
        }

        BNKReader r_headers(*p_headers);
        std::unordered_map<std::string, int> mapH;
        for (size_t i = 0; i < r_headers.list_files().size(); ++i) {
            auto &e = r_headers.list_files()[i];
            std::string fname = std::filesystem::path(e.name).filename().string();
            std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
            mapH.emplace(fname, (int)i);
        }

        auto out_root = (std::filesystem::current_path() / "extracted").string();
        int total = (int)mdl_files.size();
        progress_open(total, "Rebuilding models...");
        progress_update(0, total, "Starting...");

        std::string nested_path = S.selected_nested_temp_path;
        std::string p_headers_copy = *p_headers;

        std::thread([mdl_files, out_root, total, nested_path, p_headers_copy, mapH]() {
            std::atomic<int> done{0};
            std::mutex fail_m;
            std::vector<std::string> failed;

            auto work = [&](const BNKItemUI &it) {
                if (S.cancel_requested || S.exiting) return;

                std::string fname = std::filesystem::path(it.name).filename().string();
                std::string fname_lower = fname;
                std::transform(fname_lower.begin(), fname_lower.end(), fname_lower.begin(), ::tolower);

                if (!mapH.count(fname_lower)) {
                    std::lock_guard<std::mutex> lk(fail_m);
                    failed.push_back(it.name);
                    return;
                }

                try {
                    auto tmpdir = std::filesystem::temp_directory_path() / "f2_mdl_rebuild_nested";
                    std::error_code ec;
                    std::filesystem::create_directories(tmpdir, ec);

                    std::string output_filename = apply_folder_prefix_to_filename(it.name, ".mdl");
                    auto out_dir = std::filesystem::path(out_root) / std::filesystem::path(it.name).parent_path();
                    auto out_path = out_dir / output_filename;
                    std::filesystem::create_directories(out_path.parent_path(), ec);

                    auto tmp_h = tmpdir / ("h_" + std::to_string(it.index) + ".bin");
                    auto tmp_r = tmpdir / ("r_" + std::to_string(it.index) + ".bin");

                    extract_one(p_headers_copy, mapH.at(fname_lower), tmp_h.string());
                    extract_one(nested_path, it.index, tmp_r.string());

                    std::ofstream out(out_path, std::ios::binary);
                    { std::ifstream fh(tmp_h, std::ios::binary); out << fh.rdbuf(); }
                    { std::ifstream fr(tmp_r, std::ios::binary); out << fr.rdbuf(); }

                    std::filesystem::remove(tmp_h, ec);
                    std::filesystem::remove(tmp_r, ec);
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
            std::string msg = std::string("Model rebuild complete.\n\nOutput folder:\n") + std::filesystem::absolute(out_root).string();
            if (!failed.empty()) {
                msg += std::string("\nFailed: ") + std::to_string((int)failed.size());
            }
            show_completion_box(msg);
            S.cancel_requested = false;
        }).detach();

        return;
    }

    auto p_headers = find_bnk_by_filename("globals_model_headers.bnk");
    auto p_rest    = find_bnk_by_filename("globals_models.bnk");
    if (!p_headers || !p_rest) {
        show_error_box("Required BNKs not found.");
        return;
    }

    BNKReader r_headers(*p_headers);
    BNKReader r_rest(*p_rest);

    struct Entry { int idx; std::string name; uint32_t size; };
    std::vector<Entry> H, R;
    for (size_t i = 0; i < r_headers.list_files().size(); ++i) {
        auto &e = r_headers.list_files()[i];
        H.push_back({(int)i, e.name, e.uncompressed_size});
    }
    for (size_t i = 0; i < r_rest.list_files().size(); ++i) {
        auto &e = r_rest.list_files()[i];
        R.push_back({(int)i, e.name, e.uncompressed_size});
    }

    std::unordered_map<std::string, int> mapH, mapR;
    mapH.reserve(H.size() * 2 + 1);
    mapR.reserve(R.size() * 2 + 1);
    for (auto &e : H) {
        std::string fname = std::filesystem::path(e.name).filename().string();
        std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
        mapH.emplace(fname, e.idx);
    }
    for (auto &e : R) {
        std::string fname = std::filesystem::path(e.name).filename().string();
        std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
        mapR.emplace(fname, e.idx);
    }

    std::vector<std::string> names;
    names.reserve(std::max(H.size(), R.size()));
    for (auto &e : H) names.push_back(e.name);
    for (auto &e : R) {
        std::string fname = std::filesystem::path(e.name).filename().string();
        std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
        if (!mapH.count(fname)) names.push_back(e.name);
    }

    const int total = (int)names.size();
    if (total <= 0) {
        show_error_box("No model names found.");
        return;
    }

    auto out_root = (std::filesystem::current_path() / "extracted").string();
    progress_open(total, "Rebuilding models...");
    progress_update(0, total, "Starting...");

    std::thread([=]() {
        int done = 0;
        auto tmpdir = std::filesystem::temp_directory_path() / "f2_mdl_rebuild";
        std::error_code ec;
        std::filesystem::create_directories(tmpdir, ec);

        for (auto &name : names) {
            if (S.cancel_requested || S.exiting) break;

            std::string fname = std::filesystem::path(name).filename().string();
            std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);

            if (!mapH.count(fname) || !mapR.count(fname)) {
                progress_update(++done, total, name);
                continue;
            }

            std::string output_filename = apply_folder_prefix_to_filename(name, ".mdl");
            auto out_dir = std::filesystem::path(out_root) / std::filesystem::path(name).parent_path();
            auto out_path = out_dir / output_filename;
            std::filesystem::create_directories(out_path.parent_path(), ec);

            auto tmp_h = tmpdir / ("h_" + std::to_string(done) + ".bin");
            auto tmp_r = tmpdir / ("r_" + std::to_string(done) + ".bin");

            try {
                extract_one(*p_headers, mapH.at(fname), tmp_h.string());
                extract_one(*p_rest,    mapR.at(fname), tmp_r.string());

                std::ofstream out(out_path, std::ios::binary);
                { std::ifstream fh(tmp_h, std::ios::binary); out << fh.rdbuf(); }
                { std::ifstream fr(tmp_r, std::ios::binary); out << fr.rdbuf(); }

                std::filesystem::remove(tmp_h, ec);
                std::filesystem::remove(tmp_r, ec);
            } catch (...) {}

            progress_update(++done, total, name);
        }

        progress_done();
        if (!S.cancel_requested)
            show_completion_box(std::string("Model rebuild complete.\n\nOutput folder:\n") + std::filesystem::absolute(out_root).string());
        S.cancel_requested = false;
    }).detach();
}
