bool write_bytes(const std::filesystem::path& path,
                 const std::vector<uint8_t>& bytes,
                 std::string& err)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        err = ec.message();
        return false;
    }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        err = "cannot open " + path.string();
        return false;
    }
    if (!bytes.empty()) {
        f.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    }
    if (!f.good()) {
        err = "write failed for " + path.string();
        return false;
    }
    return true;
}

bool file_exists_nonempty(const std::filesystem::path& path)
{
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) return false;
    return std::filesystem::file_size(path, ec) > 0 && !ec;
}

bool write_text(const std::filesystem::path& path,
                const std::string& text,
                std::string& err)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        err = ec.message();
        return false;
    }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        err = "cannot open " + path.string();
        return false;
    }
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!f.good()) {
        err = "write failed for " + path.string();
        return false;
    }
    return true;
}

void append_raw(std::vector<uint8_t>& out, const void* data, size_t size)
{
    const auto* p = static_cast<const uint8_t*>(data);
    out.insert(out.end(), p, p + size);
}

size_t append_aligned(std::vector<uint8_t>& out, const void* data, size_t size)
{
    const size_t off = out.size();
    append_raw(out, data, size);
    while (out.size() & 3u) out.push_back(0);
    return off;
}

void put_u32(std::ofstream& out, uint32_t v)
{
    out.write(reinterpret_cast<const char*>(&v), 4);
}
