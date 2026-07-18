static bool reconstruct_one_tex(
    BnkCacheEntry* body_ce,
    const std::string& body_bnk_path,
    int file_index,
    BnkCacheEntry* header_ce,
    const std::string& header_bnk_path,
    BnkCacheEntry* mip0_ce,
    const std::string& mip0_bnk_path,
    std::vector<unsigned char>& out)
{
    if (!body_ce || !body_ce->reader) return false;
    const auto& body_files = body_ce->reader->list_files();
    if (file_index < 0 || (size_t)file_index >= body_files.size())
        return false;

    auto tmpdir = std::filesystem::temp_directory_path() / "f2_tex_dump";
    std::error_code ec;
    std::filesystem::create_directories(tmpdir, ec);

    auto tmp_body = tmpdir /
        ("body_" + std::to_string(file_index) + ".bin");
    std::vector<unsigned char> body;
    try {
        extract_one(body_bnk_path, file_index, tmp_body.string());
        body = read_all_bytes(tmp_body);
        std::filesystem::remove(tmp_body, ec);
    } catch (...) {
        std::filesystem::remove(tmp_body, ec);
        return false;
    }
    if (body.empty()) return false;

    std::string leaf = std::filesystem::path(body_files[file_index].name)
                           .filename().string();
    std::transform(leaf.begin(), leaf.end(), leaf.begin(), ::tolower);

    std::vector<unsigned char> header_bytes;
    if (header_ce && header_ce->reader) {
        auto h_it = header_ce->by_leaf.find(leaf);
        if (h_it != header_ce->by_leaf.end()) {
            auto tmp_h = tmpdir /
                ("hdr_" + std::to_string(h_it->second) + ".bin");
            try {
                extract_one(header_bnk_path, h_it->second, tmp_h.string());
                header_bytes = read_all_bytes(tmp_h);
                std::filesystem::remove(tmp_h, ec);
            } catch (...) {
                std::filesystem::remove(tmp_h, ec);

            }
        }
    }

    std::vector<unsigned char> mip0_bytes;
    if (mip0_ce && mip0_ce->reader) {
        auto m_it = mip0_ce->by_leaf.find(leaf);
        if (m_it != mip0_ce->by_leaf.end()) {
            auto tmp_m = tmpdir /
                ("mip_" + std::to_string(m_it->second) + ".bin");
            try {
                extract_one(mip0_bnk_path, m_it->second, tmp_m.string());
                mip0_bytes = read_all_bytes(tmp_m);
                std::filesystem::remove(tmp_m, ec);
            } catch (...) {
                std::filesystem::remove(tmp_m, ec);
            }
        }
    }

    out.clear();
    out.reserve(header_bytes.size() + mip0_bytes.size() + body.size());
    out.insert(out.end(), header_bytes.begin(), header_bytes.end());
    out.insert(out.end(), mip0_bytes.begin(), mip0_bytes.end());
    out.insert(out.end(), body.begin(), body.end());
    return !out.empty();
}
