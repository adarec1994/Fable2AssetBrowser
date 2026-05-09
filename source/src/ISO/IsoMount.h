// In-memory mount of an Xbox/Xbox360 ISO. The directory tree is parsed
// once on mount() and kept in RAM; file reads stream directly out of the
// .iso file at the right sector range with no extraction step.
//
// Usage pattern:
//   IsoMount::instance().mount("path/to/fable2.iso");
//   for (const auto& f : IsoMount::instance().list_recursive(".bnk")) {
//       auto bytes = IsoMount::instance().read_file(f);
//       BNKReader r(std::move(bytes));   // memory-stream constructor
//       ...
//   }

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
    std::string path;        // virtual path in the iso (forward slashes, no leading slash)
    uint32_t    sector = 0;  // start sector within the iso (relative to the disc base)
    uint32_t    size   = 0;  // file size in bytes
};

class IsoMount {
public:
    static IsoMount& instance();

    // Open the ISO at `iso_path` and parse its directory tree into RAM.
    // Returns true on success; on failure leaves the mount empty.
    bool mount(const std::string& iso_path, std::string* err_out = nullptr);

    // Tear down: closes the .iso file and clears the cached tree.
    void unmount();

    bool is_mounted() const { return mounted_; }
    const std::string& iso_path() const { return iso_path_; }

    // List all files whose path ends with `ext` (case-insensitive). If
    // `ext` is empty, returns every file. The returned MountedFile has
    // a virtual path; pass that path back to read_file() to read the
    // bytes.
    std::vector<MountedFile> list_recursive(const std::string& ext_lower) const;

    // Look up a file by exact virtual path. Returns nullptr if absent.
    const MountedFile* find(const std::string& virtual_path) const;

    // Convenience: case-insensitive, search every file for one whose
    // basename matches `name`. Returns the first hit, or nullptr.
    const MountedFile* find_by_basename(const std::string& name) const;

    // Read the full bytes of a file by virtual path. Returns empty vector
    // on miss / read failure.
    std::vector<uint8_t> read_file(const std::string& virtual_path);

    // Random-access read: read `n` bytes at byte offset `off` within the
    // file at `virtual_path`. Returns true on success. Used by BNKReader
    // in ISO mode so it doesn't have to slurp the whole BNK into RAM.
    bool read_at(const std::string& virtual_path,
                 uint64_t off, void* dst, size_t n);

    // Synthesise a path that the rest of the app can carry around like a
    // disk path — we use the prefix "iso://" and append the virtual path.
    // is_iso_path() recognises these so we can route reads through here.
    static std::string make_iso_path(const std::string& virtual_path);
    static bool        is_iso_path(const std::string& path);
    static std::string strip_iso_prefix(const std::string& path);

private:
    IsoMount() = default;
    ~IsoMount();

    bool mounted_ = false;
    std::string iso_path_;
    std::FILE*  fp_ = nullptr;
    uint64_t    base_offset_ = 0;     // disc-image start within the file
    std::unordered_map<std::string, MountedFile> files_;  // virtual_path -> entry
    mutable std::mutex read_mutex_;   // guards fp_ random-access

    // Tiny LRU byte cache so re-clicking the same BNK doesn't re-read it
    // from disk. Capped by total byte volume to avoid unbounded growth on
    // huge BNKs.
    struct CacheEntry {
        std::string vpath_lower;
        std::vector<uint8_t> bytes;
    };
    mutable std::list<CacheEntry> cache_;            // most-recent at front
    mutable std::unordered_map<std::string,
        std::list<CacheEntry>::iterator> cache_index_;
    mutable uint64_t cache_bytes_ = 0;
    static constexpr uint64_t kCacheCap = 512ull * 1024 * 1024;  // 512 MB

    void cache_evict_to_fit(uint64_t incoming_size);
    const std::vector<uint8_t>* cache_get(const std::string& key_lower);
    void cache_put(const std::string& key_lower, std::vector<uint8_t> bytes);
};

} // namespace ISO
