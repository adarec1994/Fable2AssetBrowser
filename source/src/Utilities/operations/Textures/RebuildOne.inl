void on_rebuild_and_extract_one(const std::string &tex_name) {
    auto p_headers = find_bnk_by_filename("globals_texture_headers.bnk");
    auto p_mip0 = find_bnk_by_filename("1024mip0_textures.bnk");
    auto p_rest = find_bnk_by_filename("globals_textures.bnk");
    if (!p_headers || !p_rest) {
        show_error_box("Required BNKs not found.");
        return;
    }

    BNKReader r_headers(*p_headers);
    BNKReader r_rest(*p_rest);
    std::optional<BNKReader> r_mip0;
    if (p_mip0) r_mip0.emplace(*p_mip0);

    std::unordered_map<std::string, int> mapH, mapR, mapM;
    for (size_t i = 0; i < r_headers.list_files().size(); ++i) {
        auto &e = r_headers.list_files()[i];
        std::string fname = e.name;
        std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
        std::replace(fname.begin(), fname.end(), '\\', '/');
        mapH.emplace(fname, (int) i);
    }
    for (size_t i = 0; i < r_rest.list_files().size(); ++i) {
        auto &e = r_rest.list_files()[i];
        std::string fname = e.name;
        std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
        std::replace(fname.begin(), fname.end(), '\\', '/');
        mapR.emplace(fname, (int) i);
    }
    if (r_mip0)
        for (size_t i = 0; i < r_mip0->list_files().size(); ++i) {
            auto &e = r_mip0->list_files()[i];
            std::string fname = e.name;
            std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
            std::replace(fname.begin(), fname.end(), '\\', '/');
            mapM.emplace(fname, (int) i);
        }

    std::string key = tex_name;
    std::transform(key.begin(), key.end(), key.begin(), ::tolower);
    std::replace(key.begin(), key.end(), '\\', '/');
    if (!mapH.count(key) || !mapR.count(key)) {
        show_error_box("Texture not found in required BNKs.");
        return;
    }

    auto out_root = (std::filesystem::current_path() / "extracted").string();
    progress_open(1, "Rebuilding...");
    progress_update(0, 1, tex_name);
    std::thread([=]() {
        auto tmpdir = std::filesystem::temp_directory_path() / "f2_tex_rebuild_one";
        std::error_code ec;
        std::filesystem::create_directories(tmpdir, ec);
        auto out_path = std::filesystem::path(out_root) / tex_name;
        std::filesystem::create_directories(out_path.parent_path(), ec);
        auto tmp_h = tmpdir / "h.bin";
        auto tmp_m = tmpdir / "m.bin";
        auto tmp_r = tmpdir / "r.bin";
        try {
            extract_one(*p_headers, mapH.at(key), tmp_h.string());
            if (mapM.count(key) && p_mip0) extract_one(*p_mip0, mapM.at(key), tmp_m.string());
            extract_one(*p_rest, mapR.at(key), tmp_r.string());
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
        } catch (...) {
        }
        progress_update(1, 1, tex_name);
        progress_done();
        if (!S.cancel_requested) show_completion_box(
            std::string("Rebuild complete.\n\nOutput folder:\n") + std::filesystem::absolute(out_root).string());
        S.cancel_requested = false;
    }).detach();
}
