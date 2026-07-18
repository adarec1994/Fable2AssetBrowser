namespace {

constexpr size_t kChunkBytes = 4 * 1024 * 1024;

std::filesystem::path build_out_path(const std::string& virtual_path) {
    std::string rel = virtual_path;
    while (!rel.empty() && (rel.front() == '/' || rel.front() == '\\'))
        rel.erase(rel.begin());
    std::filesystem::path root =
        S.export_dir.empty() ? std::filesystem::path("extracted")
                             : std::filesystem::path(S.export_dir);
    return root / rel;
}

bool stream_copy_one(int , int ,
                     const MountedFile& mf,
                     const std::filesystem::path& out,
                     std::vector<uint8_t>& buf) {
    std::error_code ec;
    if (auto parent = out.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) return false;
    }

    std::ofstream f(out, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    if (mf.size == 0) return f.good();

    uint64_t remaining = mf.size;
    uint64_t offset    = 0;
    while (remaining > 0) {
        size_t n = (remaining < (uint64_t)kChunkBytes)
                       ? (size_t)remaining
                       : kChunkBytes;
        if (!IsoMount::instance().read_at(mf.path, offset, buf.data(), n)) {
            return false;
        }
        f.write(reinterpret_cast<const char*>(buf.data()),
                (std::streamsize)n);
        if (!f.good()) return false;
        offset    += n;
        remaining -= n;
    }
    return f.good();
}

}
