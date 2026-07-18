void on_rebuild_and_extract_one_mdl(const std::string &mdl_name) {
    std::string bnk_to_use_body;
    int body_index = -1;
    bool is_nested = false;

    if (S.selected_nested_index != -1 && !S.selected_nested_temp_path.empty()) {
        is_nested = true;
        bnk_to_use_body = S.selected_nested_temp_path;
        body_index = S.selected_file_index;
    }

    auto p_headers = find_bnk_by_filename("globals_model_headers.bnk");
    if (!p_headers) {
        show_error_box("globals_model_headers.bnk not found.");
        return;
    }

    std::string key = mdl_name;
    std::transform(key.begin(), key.end(), key.begin(), ::tolower);
    std::replace(key.begin(), key.end(), '\\', '/');

    BNKReader r_headers(*p_headers);
    std::unordered_map<std::string, int> mapH;
    for (size_t i = 0; i < r_headers.list_files().size(); ++i) {
        auto &e = r_headers.list_files()[i];
        std::string fname = e.name;
        std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
        std::replace(fname.begin(), fname.end(), '\\', '/');
        mapH.emplace(fname, (int)i);
    }

    if (!mapH.count(key)) {
        show_error_box("Model header not found in globals_model_headers.bnk.");
        return;
    }

    if (!is_nested) {
        auto p_rest = find_bnk_by_filename("globals_models.bnk");
        if (!p_rest) {
            show_error_box("globals_models.bnk not found.");
            return;
        }
        BNKReader r_rest(*p_rest);
        std::unordered_map<std::string, int> mapR;
        for (size_t i = 0; i < r_rest.list_files().size(); ++i) {
            auto &e = r_rest.list_files()[i];
            std::string fname = e.name;
            std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
            std::replace(fname.begin(), fname.end(), '\\', '/');
            mapR.emplace(fname, (int)i);
        }
        if (!mapR.count(key)) {
            show_error_box("Model not found in globals_models.bnk.");
            return;
        }
        bnk_to_use_body = *p_rest;
        body_index = mapR.at(key);
    }

    auto out_root = (std::filesystem::current_path() / "extracted").string();
    progress_open(1, "Rebuilding model...");
    progress_update(0, 1, mdl_name);

    std::string p_headers_copy = *p_headers;
    int header_index = mapH.at(key);

    std::thread([=]() {
        auto tmpdir = std::filesystem::temp_directory_path() / "f2_mdl_rebuild_one";
        std::error_code ec;
        std::filesystem::create_directories(tmpdir, ec);

        std::string output_filename = apply_folder_prefix_to_filename(mdl_name, ".mdl");
        auto out_dir = std::filesystem::path(out_root) / std::filesystem::path(mdl_name).parent_path();
        auto out_path = out_dir / output_filename;
        std::filesystem::create_directories(out_path.parent_path(), ec);

        auto tmp_h = tmpdir / "h.bin";
        auto tmp_r = tmpdir / "r.bin";

        try {
            extract_one(p_headers_copy, header_index, tmp_h.string());
            extract_one(bnk_to_use_body, body_index, tmp_r.string());

            std::ofstream out(out_path, std::ios::binary);
            { std::ifstream fh(tmp_h, std::ios::binary); out << fh.rdbuf(); }
            { std::ifstream fr(tmp_r, std::ios::binary); out << fr.rdbuf(); }

            std::filesystem::remove(tmp_h, ec);
            std::filesystem::remove(tmp_r, ec);
        } catch (...) {}

        progress_update(1, 1, mdl_name);
        progress_done();
        if (!S.cancel_requested)
            show_completion_box(std::string("Model rebuild complete.\n\nOutput folder:\n") + std::filesystem::absolute(out_root).string());
        S.cancel_requested = false;
    }).detach();
}
