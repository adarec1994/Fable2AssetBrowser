namespace {

std::vector<unsigned char> reconstruct_one_mdl_for_export(
    const std::string& bnk_path, int file_index)
{
    std::vector<unsigned char> body, header_bytes;
    std::error_code ec;
    auto tmpdir = std::filesystem::temp_directory_path() / "f2_mdl_export_oneoff";
    std::filesystem::create_directories(tmpdir, ec);

    try {
        BNKReader src(bnk_path);
        const auto& src_files = src.list_files();
        if (file_index < 0 || (size_t)file_index >= src_files.size())
            return {};
        std::string mdl_name = src_files[file_index].name;

        auto tmp_body = tmpdir / "body.bin";
        extract_one(bnk_path, file_index, tmp_body.string());
        body = read_all_bytes(tmp_body);
        std::filesystem::remove(tmp_body, ec);
        if (body.empty()) return {};

        std::string base = std::filesystem::path(bnk_path)
                               .filename().string();
        std::transform(base.begin(), base.end(), base.begin(), ::tolower);
        std::optional<std::string> p_headers;
        const std::string suffix = "_models.bnk";
        if (base.size() >= suffix.size() &&
            base.compare(base.size() - suffix.size(),
                         suffix.size(), suffix) == 0) {
            std::string paired = base.substr(0, base.size() - suffix.size())
                               + "_model_headers.bnk";
            p_headers = find_bnk_by_filename(paired);
        }
        if (!p_headers) {
            p_headers = find_bnk_by_filename("globals_model_headers.bnk");
        }
        if (!p_headers) return body;

        BNKReader hr(*p_headers);
        const auto& h_files = hr.list_files();
        std::string mdl_leaf = std::filesystem::path(mdl_name)
                                   .filename().string();
        std::transform(mdl_leaf.begin(), mdl_leaf.end(),
                       mdl_leaf.begin(), ::tolower);
        int h_idx = -1;
        for (size_t i = 0; i < h_files.size(); ++i) {
            std::string hn = std::filesystem::path(h_files[i].name)
                                 .filename().string();
            std::transform(hn.begin(), hn.end(), hn.begin(), ::tolower);
            if (hn == mdl_leaf) { h_idx = (int)i; break; }
        }
        if (h_idx < 0) return body;

        auto tmp_h = tmpdir / "header.bin";
        extract_one(*p_headers, h_idx, tmp_h.string());
        header_bytes = read_all_bytes(tmp_h);
        std::filesystem::remove(tmp_h, ec);
        if (header_bytes.empty()) return body;
    } catch (...) {
        return body;
    }

    std::vector<unsigned char> out;
    out.reserve(header_bytes.size() + body.size());
    out.insert(out.end(), header_bytes.begin(), header_bytes.end());
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

const char* mdl_fmt_label(MdlExportFormat fmt) {
    switch (fmt) {
        case MdlExportFormat::GLB: return "GLB";
        case MdlExportFormat::FBX: return "FBX";
        case MdlExportFormat::RAW: return "MDL";
    }
    return "?";
}

const char* mdl_fmt_ext(MdlExportFormat fmt) {
    switch (fmt) {
        case MdlExportFormat::GLB: return ".glb";
        case MdlExportFormat::FBX: return ".fbx";
        case MdlExportFormat::RAW: return ".mdl";
    }
    return ".bin";
}

}
