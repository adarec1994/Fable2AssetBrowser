void on_rebuild_and_extract_global_mdl(const std::vector<GlobalHit>& hits) {
    std::vector<GlobalHit> mdl_files;
    for (auto &h: hits) {
        std::string name_lower = h.file_name;
        std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
        if (name_lower.size() >= 4 && name_lower.substr(name_lower.size() - 4) == ".mdl") {
            mdl_files.push_back(h);
        }
    }

    if (mdl_files.empty()) {
        show_error_box("No .mdl files in filtered results.");
        return;
    }

    auto p_headers = find_bnk_by_filename("globals_model_headers.bnk");
    auto p_rest    = find_bnk_by_filename("globals_models.bnk");
    if (!p_headers || !p_rest) {
        show_error_box("Required BNKs not found.");
        return;
    }

    auto out_root = (std::filesystem::current_path() / "extracted").string();
    int total = (int)mdl_files.size();
    progress_open(total, "Rebuilding models...");
    progress_update(0, total, "Starting...");

    std::thread([mdl_files, out_root, total, p_headers, p_rest]() {
        BNKReader r_headers(*p_headers);
        BNKReader r_rest(*p_rest);

        std::unordered_map<std::string, int> mapH, mapR;
        for (size_t i = 0; i < r_headers.list_files().size(); ++i) {
            auto &e = r_headers.list_files()[i];
            std::string fname = std::filesystem::path(e.name).filename().string();
            std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
            mapH.emplace(fname, (int)i);
        }
        for (size_t i = 0; i < r_rest.list_files().size(); ++i) {
            auto &e = r_rest.list_files()[i];
            std::string fname = std::filesystem::path(e.name).filename().string();
            std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
            mapR.emplace(fname, (int)i);
        }

        int done = 0;
        auto tmpdir = std::filesystem::temp_directory_path() / "f2_mdl_rebuild_global";
        std::error_code ec;
        std::filesystem::create_directories(tmpdir, ec);

        for (auto &h : mdl_files) {
            if (S.cancel_requested || S.exiting) break;

            std::string fname = std::filesystem::path(h.file_name).filename().string();
            std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);

            if (!mapH.count(fname) || !mapR.count(fname)) {
                progress_update(++done, total, h.file_name);
                continue;
            }

            std::string output_filename = apply_folder_prefix_to_filename(h.file_name, ".mdl");
            auto out_dir = std::filesystem::path(out_root) / std::filesystem::path(h.file_name).parent_path();
            auto out_path = out_dir / output_filename;
            std::filesystem::create_directories(out_path.parent_path(), ec);

            auto tmp_h = tmpdir / ("h_" + std::to_string(done) + ".bin");
            auto tmp_r = tmpdir / ("r_" + std::to_string(done) + ".bin");

            try {
                extract_one(*p_headers, mapH.at(fname), tmp_h.string());
                extract_one(*p_rest, mapR.at(fname), tmp_r.string());

                std::ofstream out(out_path, std::ios::binary);
                { std::ifstream fh(tmp_h, std::ios::binary); out << fh.rdbuf(); }
                { std::ifstream fr(tmp_r, std::ios::binary); out << fr.rdbuf(); }

                std::filesystem::remove(tmp_h, ec);
                std::filesystem::remove(tmp_r, ec);
            } catch (...) {}

            progress_update(++done, total, h.file_name);
        }

        progress_done();
        if (!S.cancel_requested)
            show_completion_box(std::string("Model rebuild complete.\n\nOutput folder:\n") + std::filesystem::absolute(out_root).string());
        S.cancel_requested = false;
    }).detach();
}
