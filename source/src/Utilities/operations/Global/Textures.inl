void on_rebuild_and_extract_global_tex(const std::vector<GlobalHit>& hits) {
    auto p_headers = find_bnk_by_filename("globals_texture_headers.bnk");
    auto p_mip0 = find_bnk_by_filename("1024mip0_textures.bnk");
    auto p_rest = find_bnk_by_filename("globals_textures.bnk");
    if (!p_headers || !p_rest) {
        show_error_box("Required BNKs not found.");
        return;
    }

    std::vector<GlobalHit> tex_files;
    for (auto &h: hits) if (is_tex_file(h.file_name)) tex_files.push_back(h);

    if (tex_files.empty()) {
        show_error_box("No .tex files in filtered results.");
        return;
    }

    auto out_root = (std::filesystem::current_path() / "extracted").string();
    int total = (int)tex_files.size();
    progress_open(total, "Rebuilding...");
    progress_update(0, total, "Starting...");

    std::thread([tex_files, out_root, total, p_headers, p_mip0, p_rest]() {
        BNKReader r_headers(*p_headers);
        BNKReader r_rest(*p_rest);
        std::optional<BNKReader> r_mip0;
        if (p_mip0) r_mip0.emplace(*p_mip0);

        std::unordered_map<std::string, int> mapH, mapR, mapM;
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
        if (r_mip0) {
            for (size_t i = 0; i < r_mip0->list_files().size(); ++i) {
                auto &e = r_mip0->list_files()[i];
                std::string fname = std::filesystem::path(e.name).filename().string();
                std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
                mapM.emplace(fname, (int)i);
            }
        }

        int done = 0;
        auto tmpdir = std::filesystem::temp_directory_path() / "f2_tex_rebuild_global";
        std::error_code ec;
        std::filesystem::create_directories(tmpdir, ec);

        for (auto &h : tex_files) {
            if (S.cancel_requested || S.exiting) break;

            std::string fname = std::filesystem::path(h.file_name).filename().string();
            std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);

            if (!mapH.count(fname) || !mapR.count(fname)) {
                progress_update(++done, total, h.file_name);
                continue;
            }

            auto out_path = std::filesystem::path(out_root) / h.file_name;
            std::filesystem::create_directories(out_path.parent_path(), ec);

            auto tmp_h = tmpdir / ("h_" + std::to_string(done) + ".bin");
            auto tmp_m = tmpdir / ("m_" + std::to_string(done) + ".bin");
            auto tmp_r = tmpdir / ("r_" + std::to_string(done) + ".bin");

            try {
                extract_one(*p_headers, mapH.at(fname), tmp_h.string());
                if (mapM.count(fname) && p_mip0) extract_one(*p_mip0, mapM.at(fname), tmp_m.string());
                extract_one(*p_rest, mapR.at(fname), tmp_r.string());

                std::ofstream out(out_path, std::ios::binary);
                std::ifstream fh(tmp_h, std::ios::binary);
                out << fh.rdbuf();
                if (std::filesystem::exists(tmp_m)) {
                    std::ifstream fm(tmp_m, std::ios::binary);
                    out << fm.rdbuf();
                }
                std::ifstream fr(tmp_r, std::ios::binary);
                out << fr.rdbuf();

                std::filesystem::remove(tmp_h, ec);
                if (std::filesystem::exists(tmp_m)) std::filesystem::remove(tmp_m, ec);
                std::filesystem::remove(tmp_r, ec);
            } catch (...) {}

            progress_update(++done, total, h.file_name);
        }

        progress_done();
        if (!S.cancel_requested)
            show_completion_box(std::string("Rebuild complete.\n\nOutput folder:\n") + std::filesystem::absolute(out_root).string());
        S.cancel_requested = false;
    }).detach();
}
