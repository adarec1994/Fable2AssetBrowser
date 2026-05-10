
#include "IsoMount.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

namespace ISO {

namespace {

constexpr uint32_t kSectorSize         = 2048;
constexpr uint32_t kHeaderOffset       = 0x10000;
constexpr uint64_t kGlobalLseekOffset  = 0x0FD90000ull;
constexpr uint64_t kXgd1LseekOffset    = 0x18300000ull;
constexpr uint64_t kXgd3LseekOffset    = 0x02080000ull;
const     char     kMediaMagic[21]     = "MICROSOFT*XBOX*MEDIA";

uint16_t read_u16le(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t read_u32le(const uint8_t* p) {
    return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

bool seek_abs(std::FILE* fp, uint64_t off) {
#ifdef _WIN32
    return _fseeki64(fp, (long long)off, SEEK_SET) == 0;
#else
    return fseeko(fp, (off_t)off, SEEK_SET) == 0;
#endif
}

bool detect_base_offset_and_root(std::FILE* fp, uint64_t& base, uint32_t& root_sector, uint32_t& root_size) {
    static const uint64_t candidates[] = {
        kGlobalLseekOffset, kXgd3LseekOffset, kXgd1LseekOffset, 0
    };
    char magic[20];
    uint8_t hdr[8];
    for (uint64_t cand : candidates) {
        if (!seek_abs(fp, cand + kHeaderOffset)) continue;
        if (std::fread(magic, 1, 20, fp) != 20) continue;
        if (std::memcmp(magic, kMediaMagic, 20) != 0) continue;
        if (std::fread(hdr, 1, 8, fp) != 8) continue;
        root_sector = read_u32le(hdr);
        root_size   = read_u32le(hdr + 4);
        base = cand;
        return true;
    }
    return false;
}

constexpr uint8_t kAttrDirectory = 0x10;

struct DirEntry {
    uint16_t left_off  = 0;
    uint16_t right_off = 0;
    uint32_t start_sector = 0;
    uint32_t file_size    = 0;
    uint8_t  attrs        = 0;
    uint8_t  name_len     = 0;
    std::string name;
};

bool parse_entry(const uint8_t* data, size_t size, uint32_t off, DirEntry& out) {
    if ((size_t)off + 14 > size) return false;
    out.left_off     = read_u16le(data + off + 0);
    out.right_off    = read_u16le(data + off + 2);
    out.start_sector = read_u32le(data + off + 4);
    out.file_size    = read_u32le(data + off + 8);
    out.attrs        = data[off + 12];
    out.name_len     = data[off + 13];
    if ((size_t)off + 14 + out.name_len > size) return false;
    out.name.assign((const char*)(data + off + 14), out.name_len);
    return true;
}

void walk_btree(const uint8_t* blob, size_t blob_size, uint32_t off, std::vector<DirEntry>& out) {
    if ((size_t)off * 4u >= blob_size) return;
    DirEntry e;
    if (!parse_entry(blob, blob_size, off * 4u, e)) return;
    if (e.name_len == 0 && e.left_off == 0 && e.right_off == 0
        && e.start_sector == 0 && e.file_size == 0) return;
    if (e.left_off  != 0 && e.left_off  != 0xFFFF) walk_btree(blob, blob_size, e.left_off,  out);
    out.push_back(std::move(e));
    if (e.right_off != 0 && e.right_off != 0xFFFF) walk_btree(blob, blob_size, e.right_off, out);
}

bool read_dir_blob(std::FILE* fp, uint64_t base, uint32_t sector, uint32_t size, std::vector<uint8_t>& out) {
    out.resize(size);
    if (!seek_abs(fp, base + (uint64_t)sector * kSectorSize)) return false;
    return std::fread(out.data(), 1, size, fp) == size;
}

std::string lower(const std::string& s) {
    std::string r; r.reserve(s.size());
    for (char c : s) r.push_back((char)std::tolower((unsigned char)c));
    return r;
}

bool ends_with_lower(const std::string& s, const std::string& tail_lower) {
    if (tail_lower.empty()) return true;
    if (s.size() < tail_lower.size()) return false;
    for (size_t i = 0; i < tail_lower.size(); ++i) {
        char a = (char)std::tolower((unsigned char)s[s.size() - tail_lower.size() + i]);
        if (a != tail_lower[i]) return false;
    }
    return true;
}

}

IsoMount& IsoMount::instance() {
    static IsoMount inst;
    return inst;
}

IsoMount::~IsoMount() { unmount(); }

void IsoMount::unmount() {
    std::lock_guard<std::mutex> lock(read_mutex_);
    if (fp_) { std::fclose(fp_); fp_ = nullptr; }
    files_.clear();
    iso_path_.clear();
    base_offset_ = 0;
    mounted_ = false;
    cache_.clear();
    cache_index_.clear();
    cache_bytes_ = 0;
}

bool IsoMount::mount(const std::string& iso_path, std::string* err_out) {
    unmount();

    std::lock_guard<std::mutex> lock(read_mutex_);
    fp_ = std::fopen(iso_path.c_str(), "rb");
    if (!fp_) {
        if (err_out) *err_out = "cannot open " + iso_path;
        return false;
    }

    uint32_t root_sector = 0, root_size = 0;
    if (!detect_base_offset_and_root(fp_, base_offset_, root_sector, root_size)) {
        if (err_out) *err_out = "no XDVDFS volume descriptor (not a recognised Xbox/360 disc image)";
        std::fclose(fp_); fp_ = nullptr;
        return false;
    }

    iso_path_ = iso_path;

    struct Pending { uint32_t sector, size; std::string prefix; };
    std::vector<Pending> queue;
    queue.push_back({root_sector, root_size, ""});

    while (!queue.empty()) {
        Pending cur = queue.back();
        queue.pop_back();
        std::vector<uint8_t> blob;
        if (!read_dir_blob(fp_, base_offset_, cur.sector, cur.size, blob)) continue;
        std::vector<DirEntry> entries;
        walk_btree(blob.data(), blob.size(), 0, entries);
        for (const DirEntry& e : entries) {
            std::string vpath = cur.prefix.empty() ? e.name : (cur.prefix + "/" + e.name);
            if (e.attrs & kAttrDirectory) {
                if (e.file_size > 0 && e.start_sector > 0) {
                    queue.push_back({e.start_sector, e.file_size, vpath});
                }
            } else {
                MountedFile mf;
                mf.path   = vpath;
                mf.sector = e.start_sector;
                mf.size   = e.file_size;
                files_[lower(vpath)] = mf;
            }
        }
    }

    mounted_ = true;
    return true;
}

std::vector<MountedFile> IsoMount::list_recursive(const std::string& ext_lower) const {
    std::vector<MountedFile> out;
    out.reserve(files_.size());
    for (const auto& [k, v] : files_) {
        if (ext_lower.empty() || ends_with_lower(v.path, ext_lower)) out.push_back(v);
    }
    return out;
}

const MountedFile* IsoMount::find(const std::string& virtual_path) const {
    auto it = files_.find(lower(virtual_path));
    if (it == files_.end()) return nullptr;
    return &it->second;
}

const MountedFile* IsoMount::find_by_basename(const std::string& name) const {
    std::string n_lower = lower(name);
    for (const auto& [k, v] : files_) {
        size_t slash = v.path.find_last_of('/');
        std::string base = (slash == std::string::npos) ? v.path : v.path.substr(slash + 1);
        if (lower(base) == n_lower) return &v;
    }
    return nullptr;
}

void IsoMount::cache_evict_to_fit(uint64_t incoming_size) {
    while (cache_bytes_ + incoming_size > kCacheCap && !cache_.empty()) {
        auto it = std::prev(cache_.end());
        cache_bytes_ -= it->bytes.size();
        cache_index_.erase(it->vpath_lower);
        cache_.erase(it);
    }
}

const std::vector<uint8_t>* IsoMount::cache_get(const std::string& key_lower) {
    auto it = cache_index_.find(key_lower);
    if (it == cache_index_.end()) return nullptr;
    cache_.splice(cache_.begin(), cache_, it->second);
    return &it->second->bytes;
}

void IsoMount::cache_put(const std::string& key_lower, std::vector<uint8_t> bytes) {
    if (bytes.size() > kCacheCap) return;
    cache_evict_to_fit(bytes.size());
    cache_.push_front({key_lower, std::move(bytes)});
    cache_index_[key_lower] = cache_.begin();
    cache_bytes_ += cache_.front().bytes.size();
}

void IsoMount::clear_cache() {
    std::lock_guard<std::mutex> lock(read_mutex_);
    cache_.clear();
    cache_index_.clear();
    cache_bytes_ = 0;
}

std::vector<uint8_t> IsoMount::read_file(const std::string& virtual_path) {
    std::vector<uint8_t> out;
    const MountedFile* mf = find(virtual_path);
    if (!mf) return out;

    std::lock_guard<std::mutex> lock(read_mutex_);
    if (!fp_) return out;

    std::string key = lower(virtual_path);
    if (const auto* hit = cache_get(key)) {
        out.assign(hit->begin(), hit->end());
        return out;
    }

    if (!seek_abs(fp_, base_offset_ + (uint64_t)mf->sector * kSectorSize)) return out;
    out.resize(mf->size);
    size_t got = std::fread(out.data(), 1, mf->size, fp_);
    if (got != mf->size) out.resize(got);

    if (got == mf->size) cache_put(key, out);
    return out;
}

bool IsoMount::read_at(const std::string& virtual_path,
                       uint64_t off, void* dst, size_t n)
{
    const MountedFile* mf = find(virtual_path);
    if (!mf) return false;
    if ((uint64_t)off + n > (uint64_t)mf->size) return false;

    std::lock_guard<std::mutex> lock(read_mutex_);
    if (!fp_) return false;
    uint64_t abs = base_offset_ + (uint64_t)mf->sector * kSectorSize + off;
    if (!seek_abs(fp_, abs)) return false;
    return std::fread(dst, 1, n, fp_) == n;
}

std::string IsoMount::make_iso_path(const std::string& virtual_path) {
    return std::string("iso://") + virtual_path;
}

bool IsoMount::is_iso_path(const std::string& path) {
    return path.compare(0, 6, "iso://") == 0;
}

std::string IsoMount::strip_iso_prefix(const std::string& path) {
    return is_iso_path(path) ? path.substr(6) : path;
}

}
