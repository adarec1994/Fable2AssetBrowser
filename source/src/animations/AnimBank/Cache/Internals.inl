uint64_t cache_fnv64(uint64_t h, const void* data, size_t n) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}



uint64_t clip_cache_fingerprint(const std::string& root,
                                const std::vector<AnimClip>& clips) {
    uint64_t h = 1469598103934665603ull;
    h = cache_fnv64(h, root.data(), root.size());
    std::error_code ec;
    auto stat_file = [&](const std::filesystem::path& p) {
        if (!std::filesystem::is_regular_file(p, ec)) return;
        const uint64_t sz = (uint64_t)std::filesystem::file_size(p, ec);
        const int64_t ticks = (int64_t)std::filesystem::last_write_time(
                                  p, ec)
                                  .time_since_epoch()
                                  .count();
        h = cache_fnv64(h, &sz, sizeof(sz));
        h = cache_fnv64(h, &ticks, sizeof(ticks));
    };
    const std::filesystem::path root_path(root);
    if (std::filesystem::is_regular_file(root_path, ec)) {
        stat_file(root_path);   
    } else {
        stat_file(root_path / "data" / "levels.bnk");
        stat_file(root_path / "data" / "streaming.bnk");
        stat_file(root_path / "data" / "Globals" / "globals.gdb");
    }
    const uint32_t count = (uint32_t)clips.size();
    h = cache_fnv64(h, &count, sizeof(count));
    for (const AnimClip& c : clips) {
        h = cache_fnv64(h, &c.key0, sizeof(c.key0));
    }
    return h;
}

std::filesystem::path clip_cache_path() {
    return std::filesystem::current_path() / "anim_names.cache";
}

void cache_put_u32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(uint8_t(v));
    out.push_back(uint8_t(v >> 8));
    out.push_back(uint8_t(v >> 16));
    out.push_back(uint8_t(v >> 24));
}

void cache_put_str(std::vector<uint8_t>& out, const std::string& s) {
    const uint16_t n = (uint16_t)std::min<size_t>(s.size(), 0xFFFF);
    out.push_back(uint8_t(n));
    out.push_back(uint8_t(n >> 8));
    out.insert(out.end(), s.begin(), s.begin() + n);
}

struct CacheReader {
    const uint8_t* p = nullptr;
    size_t n = 0;
    size_t pos = 0;
    bool ok = true;

    uint32_t u32() {
        if (pos + 4 > n) { ok = false; return 0; }
        const uint32_t v = uint32_t(p[pos]) | (uint32_t(p[pos + 1]) << 8) |
                           (uint32_t(p[pos + 2]) << 16) |
                           (uint32_t(p[pos + 3]) << 24);
        pos += 4;
        return v;
    }
    uint64_t u64() {
        const uint64_t lo = u32();
        const uint64_t hi = u32();
        return lo | (hi << 32);
    }
    std::string str() {
        if (pos + 2 > n) { ok = false; return {}; }
        const size_t len = size_t(p[pos]) | (size_t(p[pos + 1]) << 8);
        pos += 2;
        if (pos + len > n) { ok = false; return {}; }
        std::string s(reinterpret_cast<const char*>(p + pos), len);
        pos += len;
        return s;
    }
};

constexpr uint32_t kClipCacheMagic = 0x43414632u;   
constexpr uint32_t kClipCacheVersion = 1;
