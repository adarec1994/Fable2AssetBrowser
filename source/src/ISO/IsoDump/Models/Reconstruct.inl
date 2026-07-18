static bool reconstruct_one_mdl(
    BnkCacheEntry* body_ce,
    const std::string& body_bnk_path,
    int file_index,
    BnkCacheEntry* header_ce,
    const std::string& header_bnk_path,
    std::vector<unsigned char>& out)
{
    if (!body_ce || !body_ce->reader) return false;
    const auto& body_files = body_ce->reader->list_files();
    if (file_index < 0 || (size_t)file_index >= body_files.size())
        return false;

    auto tmpdir = std::filesystem::temp_directory_path() / "f2_mdl_dump";
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

    if (!header_ce) {
        out = std::move(body);
        return true;
    }

    std::string leaf = std::filesystem::path(body_files[file_index].name)
                           .filename().string();
    std::transform(leaf.begin(), leaf.end(), leaf.begin(), ::tolower);
    auto h_it = header_ce->by_leaf.find(leaf);
    if (h_it == header_ce->by_leaf.end()) {

        out = std::move(body);
        return true;
    }

    auto tmp_h = tmpdir /
        ("hdr_" + std::to_string(h_it->second) + ".bin");
    std::vector<unsigned char> hbuf;
    try {
        extract_one(header_bnk_path, h_it->second, tmp_h.string());
        hbuf = read_all_bytes(tmp_h);
        std::filesystem::remove(tmp_h, ec);
    } catch (...) {
        std::filesystem::remove(tmp_h, ec);

        out = std::move(body);
        return true;
    }
    if (hbuf.empty()) { out = std::move(body); return true; }

    out.clear();
    out.reserve(hbuf.size() + body.size());
    out.insert(out.end(), hbuf.begin(), hbuf.end());
    out.insert(out.end(), body.begin(), body.end());
    return true;
}
