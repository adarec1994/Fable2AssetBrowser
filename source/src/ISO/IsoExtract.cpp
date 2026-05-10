
#include "IsoExtract.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <filesystem>
#include <fstream>
#include <algorithm>

namespace ISO {

namespace {

constexpr uint32_t kSectorSize         = 2048;
constexpr uint32_t kHeaderOffset       = 0x10000;
constexpr uint64_t kGlobalLseekOffset  = 0x0FD90000ull;
constexpr uint64_t kXgd1LseekOffset    = 0x18300000ull;
constexpr uint64_t kXgd3LseekOffset    = 0x02080000ull;
const     char     kMediaMagic[21]     = "MICROSOFT*XBOX*MEDIA";

struct Stream {
    std::FILE* fp = nullptr;
    uint64_t   base = 0;
    uint64_t   total_size = 0;

    bool open(const std::string& path) {
        fp = std::fopen(path.c_str(), "rb");
        if (!fp) return false;
        std::fseek(fp, 0, SEEK_END);
        total_size = (uint64_t)std::ftell(fp);
        std::fseek(fp, 0, SEEK_SET);
        return true;
    }
    void close() { if (fp) { std::fclose(fp); fp = nullptr; } }

    bool seek(uint64_t off) {
        if (!fp) return false;
#ifdef _WIN32
        return _fseeki64(fp, (long long)off, SEEK_SET) == 0;
#else
        return fseeko(fp, (off_t)off, SEEK_SET) == 0;
#endif
    }
    bool read(void* dst, size_t n) {
        return fp && std::fread(dst, 1, n, fp) == n;
    }

    bool seek_sector(uint32_t sector) {
        return seek(base + (uint64_t)sector * kSectorSize);
    }
};

uint16_t read_u16le(const uint8_t* p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}
uint32_t read_u32le(const uint8_t* p) {
    return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

bool detect_base_offset(Stream& s, uint32_t& root_sector, uint32_t& root_size) {
    static const uint64_t candidates[] = {
        kGlobalLseekOffset, kXgd3LseekOffset, kXgd1LseekOffset, 0
    };
    char buf[20];
    for (uint64_t cand : candidates) {
        if (!s.seek(cand + kHeaderOffset)) continue;
        if (!s.read(buf, sizeof(buf))) continue;
        if (std::memcmp(buf, kMediaMagic, 20) == 0) {

            uint8_t hdr[8];
            if (!s.read(hdr, 8)) continue;
            root_sector = read_u32le(hdr);
            root_size   = read_u32le(hdr + 4);
            s.base = cand;
            return true;
        }
    }
    return false;
}

struct DirEntry {
    uint16_t left_off  = 0;
    uint16_t right_off = 0;
    uint32_t start_sector = 0;
    uint32_t file_size    = 0;
    uint8_t  attrs        = 0;
    uint8_t  name_len     = 0;
    std::string name;
};

constexpr uint8_t kAttrDirectory = 0x10;

bool parse_dir_entry(const uint8_t* data, size_t data_size, uint32_t off, DirEntry& out) {
    if (off + 14 > data_size) return false;
    out.left_off     = read_u16le(data + off + 0);
    out.right_off    = read_u16le(data + off + 2);
    out.start_sector = read_u32le(data + off + 4);
    out.file_size    = read_u32le(data + off + 8);
    out.attrs        = data[off + 12];
    out.name_len     = data[off + 13];
    if (off + 14 + out.name_len > data_size) return false;
    out.name.assign((const char*)(data + off + 14), out.name_len);
    return true;
}

struct Pending {
    uint32_t   sector;
    uint32_t   size;
    std::string rel_path;
};

bool read_directory_blob(Stream& s, uint32_t sector, uint32_t size, std::vector<uint8_t>& out_blob) {
    out_blob.resize(size);
    if (!s.seek_sector(sector)) return false;
    return s.read(out_blob.data(), size);
}

void walk_btree(const uint8_t* blob, size_t blob_size, uint32_t off, std::vector<DirEntry>& out) {
    if (off * 4u >= blob_size) return;
    DirEntry e;
    if (!parse_dir_entry(blob, blob_size, off * 4u, e)) return;
    if (e.name_len == 0 && e.left_off == 0 && e.right_off == 0
        && e.start_sector == 0 && e.file_size == 0)
    {
        return;
    }
    if (e.left_off  != 0 && e.left_off  != 0xFFFF) walk_btree(blob, blob_size, e.left_off,  out);
    out.push_back(std::move(e));
    if (e.right_off != 0 && e.right_off != 0xFFFF) walk_btree(blob, blob_size, e.right_off, out);
}

bool extract_directory(Stream& s,
                       uint32_t sector, uint32_t size,
                       const std::filesystem::path& dest_dir,
                       std::vector<Pending>& subdirs,
                       std::string* err_out)
{
    std::vector<uint8_t> blob;
    if (!read_directory_blob(s, sector, size, blob)) {
        if (err_out) *err_out = "failed reading directory at sector " + std::to_string(sector);
        return false;
    }
    std::vector<DirEntry> entries;
    walk_btree(blob.data(), blob.size(), 0, entries);

    std::error_code ec;
    std::filesystem::create_directories(dest_dir, ec);

    for (const DirEntry& e : entries) {
        std::filesystem::path child = dest_dir / e.name;
        if (e.attrs & kAttrDirectory) {
            if (e.file_size > 0 && e.start_sector > 0) {
                subdirs.push_back({e.start_sector, e.file_size, child.string()});
            } else {
                std::filesystem::create_directories(child, ec);
            }
        } else {

            if (!s.seek_sector(e.start_sector)) {
                if (err_out) *err_out = "seek failed for file " + e.name;
                continue;
            }
            std::ofstream out(child, std::ios::binary | std::ios::trunc);
            if (!out) {
                if (err_out) *err_out = "cannot open output: " + child.string();
                continue;
            }
            std::vector<uint8_t> chunk(64 * 1024);
            uint32_t remaining = e.file_size;
            while (remaining > 0) {
                size_t want = std::min<size_t>(remaining, chunk.size());
                if (!s.read(chunk.data(), want)) break;
                out.write((const char*)chunk.data(), (std::streamsize)want);
                remaining -= (uint32_t)want;
            }
        }
    }
    return true;
}

}

bool extract_iso(const std::string& iso_path,
                 const std::string& dest_dir,
                 std::string* err_out,
                 std::function<void(uint64_t, uint64_t)> progress)
{
    Stream s;
    if (!s.open(iso_path)) {
        if (err_out) *err_out = "cannot open " + iso_path;
        return false;
    }

    uint32_t root_sector = 0, root_size = 0;
    if (!detect_base_offset(s, root_sector, root_size)) {
        if (err_out) *err_out = "no XDVDFS volume descriptor found "
                                "(not an Xbox/360 disc image, or unsupported layout)";
        s.close();
        return false;
    }

    std::filesystem::path dest = dest_dir;
    std::error_code ec;
    std::filesystem::create_directories(dest, ec);

    std::vector<Pending> queue;
    queue.push_back({root_sector, root_size, dest.string()});

    while (!queue.empty()) {
        Pending cur = queue.back();
        queue.pop_back();
        if (!extract_directory(s, cur.sector, cur.size, cur.rel_path, queue, err_out)) {

        }
        if (progress) {
            uint64_t pos = (uint64_t)
#ifdef _WIN32
                _ftelli64(s.fp);
#else
                ftello(s.fp);
#endif
            progress(pos, s.total_size);
        }
    }

    s.close();
    return true;
}

bool is_xbox_iso(const std::string& iso_path) {
    Stream s;
    if (!s.open(iso_path)) return false;
    uint32_t root_sector = 0, root_size = 0;
    bool ok = detect_base_offset(s, root_sector, root_size);
    s.close();
    return ok;
}

}
