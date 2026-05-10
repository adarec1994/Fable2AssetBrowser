
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <list>
#include <cstdint>
#include <cstdio>
#include <mutex>

namespace ISO {

struct MountedFile {
    std::string path;
    uint32_t    sector = 0;
    uint32_t    size   = 0;
};

class IsoMount {
public:
    static IsoMount& instance();

    bool mount(const std::string& iso_path, std::string* err_out = nullptr);

    void unmount();

    bool is_mounted() const { return mounted_; }
    const std::string& iso_path() const { return iso_path_; }

    std::vector<MountedFile> list_recursive(const std::string& ext_lower) const;

    const MountedFile* find(const std::string& virtual_path) const;

    const MountedFile* find_by_basename(const std::string& name) const;

    std::vector<uint8_t> read_file(const std::string& virtual_path);

    bool read_at(const std::string& virtual_path,
                 uint64_t off, void* dst, size_t n);

    void clear_cache();

    static std::string make_iso_path(const std::string& virtual_path);
    static bool        is_iso_path(const std::string& path);
    static std::string strip_iso_prefix(const std::string& path);

private:
    IsoMount() = default;
    ~IsoMount();

    bool mounted_ = false;
    std::string iso_path_;
    std::FILE*  fp_ = nullptr;
    uint64_t    base_offset_ = 0;
    std::unordered_map<std::string, MountedFile> files_;
    mutable std::mutex read_mutex_;

    struct CacheEntry {
        std::string vpath_lower;
        std::vector<uint8_t> bytes;
    };
    mutable std::list<CacheEntry> cache_;
    mutable std::unordered_map<std::string,
        std::list<CacheEntry>::iterator> cache_index_;
    mutable uint64_t cache_bytes_ = 0;
    static constexpr uint64_t kCacheCap = 512ull * 1024 * 1024;

    void cache_evict_to_fit(uint64_t incoming_size);
    const std::vector<uint8_t>* cache_get(const std::string& key_lower);
    void cache_put(const std::string& key_lower, std::vector<uint8_t> bytes);
};

}
