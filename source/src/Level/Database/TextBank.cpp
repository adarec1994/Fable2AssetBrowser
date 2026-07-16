#include "TextBank.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>

#include <zlib.h>

#include "UI/OutputLog.h"

namespace TextBank {
namespace {

struct Entry {
    uint32_t page = 0;
    uint32_t offset = 0;
};

struct Bank {
    bool ok = false;
    std::string path;
    uint32_t header_le = 0;
    std::unordered_map<uint32_t, Entry> entries;
    std::vector<uint32_t> entry_order;
    std::unordered_map<uint32_t, std::vector<uint8_t>> pages;
};

Bank& bank() {
    static Bank b;
    return b;
}
std::mutex& mtx() {
    static std::mutex m;
    return m;
}

uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
void put_be32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(uint8_t(x >> 24));
    v.push_back(uint8_t(x >> 16));
    v.push_back(uint8_t(x >> 8));
    v.push_back(uint8_t(x));
}

bool inflate_page(const uint8_t* src, size_t comp, size_t dec_size,
                  std::vector<uint8_t>& out) {
    out.assign(dec_size, 0);
    z_stream z{};
    if (inflateInit(&z) != Z_OK) return false;
    z.next_in = const_cast<Bytef*>(src);
    z.avail_in = (uInt)comp;
    z.next_out = out.data();
    z.avail_out = (uInt)out.size();
    const int ret = inflate(&z, Z_FINISH);
    const bool ok = (ret == Z_STREAM_END || ret == Z_OK ||
                     ret == Z_BUF_ERROR) && z.avail_out == 0;
    inflateEnd(&z);
    return ok;
}

bool deflate_page(const std::vector<uint8_t>& in,
                  std::vector<uint8_t>& out) {
    z_stream z{};
    if (deflateInit(&z, Z_BEST_COMPRESSION) != Z_OK) return false;
    out.resize((size_t)deflateBound(&z, (uLong)in.size()) + 64);
    z.next_in = const_cast<Bytef*>(in.data());
    z.avail_in = (uInt)in.size();
    z.next_out = out.data();
    z.avail_out = (uInt)out.size();
    const int ret = deflate(&z, Z_FINISH);
    const bool ok = (ret == Z_STREAM_END);
    out.resize(out.size() - z.avail_out);
    deflateEnd(&z);
    return ok;
}

std::string utf16be_to_utf8(const uint8_t* p, size_t chars) {
    std::string out;
    out.reserve(chars);
    size_t i = 0;
    while (i < chars) {
        uint32_t c = (uint32_t(p[i * 2]) << 8) | p[i * 2 + 1];
        ++i;
        if (c >= 0xD800 && c <= 0xDBFF && i < chars) {
            const uint32_t lo =
                (uint32_t(p[i * 2]) << 8) | p[i * 2 + 1];
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                ++i;
            }
        }
        if (c == 0) break;
        if (c < 0x80) {
            out.push_back(char(c));
        } else if (c < 0x800) {
            out.push_back(char(0xC0 | (c >> 6)));
            out.push_back(char(0x80 | (c & 0x3F)));
        } else if (c < 0x10000) {
            out.push_back(char(0xE0 | (c >> 12)));
            out.push_back(char(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(char(0x80 | (c & 0x3F)));
        } else {
            out.push_back(char(0xF0 | (c >> 18)));
            out.push_back(char(0x80 | ((c >> 12) & 0x3F)));
            out.push_back(char(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(char(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

void utf8_to_utf16be(const std::string& s, std::vector<uint8_t>& out) {
    size_t i = 0;
    auto put = [&](uint32_t cp) {
        if (cp >= 0x10000) {
            cp -= 0x10000;
            const uint32_t hi = 0xD800 + (cp >> 10);
            const uint32_t lo = 0xDC00 + (cp & 0x3FF);
            out.push_back(uint8_t(hi >> 8));
            out.push_back(uint8_t(hi));
            out.push_back(uint8_t(lo >> 8));
            out.push_back(uint8_t(lo));
        } else {
            out.push_back(uint8_t(cp >> 8));
            out.push_back(uint8_t(cp));
        }
    };
    while (i < s.size()) {
        const uint8_t c = uint8_t(s[i]);
        uint32_t cp = 0;
        int n = 0;
        if (c < 0x80) { cp = c; n = 0; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; n = 3; }
        else { ++i; continue; }
        ++i;
        bool bad = false;
        for (int k = 0; k < n; ++k) {
            if (i >= s.size() || (uint8_t(s[i]) & 0xC0) != 0x80) {
                bad = true;
                break;
            }
            cp = (cp << 6) | (uint8_t(s[i]) & 0x3F);
            ++i;
        }
        if (bad) continue;
        if (cp == '\r') continue;
        put(cp);
    }
}

bool parse_bank(const std::vector<uint8_t>& d, Bank& b) {
    if (d.size() < 12) return false;
    b.header_le = uint32_t(d[0]) | (uint32_t(d[1]) << 8) |
                  (uint32_t(d[2]) << 16) | (uint32_t(d[3]) << 24);
    const uint32_t count = be32(d.data() + 4);
    size_t o = 8;
    if (o + (uint64_t)count * 12 + 4 > d.size() || count > (1u << 24)) {
        return false;
    }
    b.entries.reserve(count * 2);
    b.entry_order.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t tag = be32(d.data() + o);
        Entry e;
        e.page = be32(d.data() + o + 4);
        e.offset = be32(d.data() + o + 8);
        b.entries.emplace(tag, e);
        b.entry_order.push_back(tag);
        o += 12;
    }
    const uint32_t page_count = be32(d.data() + o);
    o += 4;
    for (uint32_t i = 0; i < page_count; ++i) {
        if (o + 12 > d.size()) return false;
        const uint32_t ph = be32(d.data() + o);
        const uint32_t comp = be32(d.data() + o + 4);
        const uint32_t dec = be32(d.data() + o + 8);
        o += 12;
        if (o + comp > d.size() || dec > (64u << 20)) return false;
        std::vector<uint8_t> blob;
        if (!inflate_page(d.data() + o, comp, dec, blob)) return false;
        b.pages.emplace(ph, std::move(blob));
        o += comp;
    }
    return true;
}

bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    out.resize(size_t(f.tellg()));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(out.data()),
           (std::streamsize)out.size());
    return bool(f);
}

std::vector<std::string> find_babels(const std::string& root_dir) {
    namespace fs = std::filesystem;
    std::vector<std::string> out;
    std::error_code ec;
    fs::path lang = fs::path(root_dir) / "data" / "language";
    if (!fs::is_directory(lang, ec)) return out;
    for (const auto& c : fs::directory_iterator(lang, ec)) {
        if (!c.is_directory()) continue;
        fs::path p = c.path() / "text" / "book.babel";
        if (fs::is_regular_file(p, ec)) out.push_back(p.string());
    }
    std::sort(out.begin(), out.end(),
              [](const std::string& a, const std::string& b) {
                  const bool ea = a.find("en-") != std::string::npos;
                  const bool eb = b.find("en-") != std::string::npos;
                  if (ea != eb) return ea;
                  return a < b;
              });
    return out;
}

bool lookup_in(const Bank& b, uint32_t tag, std::string& out_utf8) {
    auto it = b.entries.find(tag);
    if (it == b.entries.end()) return false;
    auto pit = b.pages.find(it->second.page);
    if (pit == b.pages.end()) return false;
    const auto& blob = pit->second;
    const size_t off = it->second.offset;
    if (off + 4 > blob.size()) return false;
    const uint32_t chars = be32(blob.data() + off);
    if (off + 4 + (uint64_t)chars * 2 > blob.size() || chars > (8u << 20)) {
        return false;
    }
    out_utf8 = utf16be_to_utf8(blob.data() + off + 4, chars);
    return true;
}

bool rewrite_file(const std::string& path,
                  const std::unordered_map<uint32_t, std::string>& edits,
                  std::string& err) {
    std::vector<uint8_t> d;
    if (!read_file(path, d)) {
        err = "cannot read " + path;
        return false;
    }
    if (d.size() < 12) {
        err = "babel too small: " + path;
        return false;
    }
    const uint32_t count = be32(d.data() + 4);
    size_t o = 8;
    if (o + (uint64_t)count * 12 + 4 > d.size()) {
        err = "babel directory truncated: " + path;
        return false;
    }
    struct DirEnt {
        uint32_t tag, page, off;
    };
    std::vector<DirEnt> dir;
    dir.reserve(count + edits.size());
    std::unordered_map<uint32_t, size_t> dir_index;
    dir_index.reserve(count * 2);
    for (uint32_t i = 0; i < count; ++i) {
        DirEnt e{be32(d.data() + o), be32(d.data() + o + 4),
                 be32(d.data() + o + 8)};
        dir_index.emplace(e.tag, dir.size());
        dir.push_back(e);
        o += 12;
    }
    const uint32_t page_count = be32(d.data() + o);
    o += 4;
    struct RawPage {
        uint32_t hash;
        const uint8_t* bytes;
        size_t size;
    };
    std::vector<RawPage> raw_pages;
    raw_pages.reserve(page_count + 1);
    std::unordered_map<uint32_t, bool> page_hashes;
    for (uint32_t i = 0; i < page_count; ++i) {
        if (o + 12 > d.size()) {
            err = "babel pages truncated: " + path;
            return false;
        }
        const uint32_t ph = be32(d.data() + o);
        const uint32_t comp = be32(d.data() + o + 4);
        if (o + 12 + comp > d.size()) {
            err = "babel page overruns file: " + path;
            return false;
        }
        raw_pages.push_back({ph, d.data() + o, size_t(12) + comp});
        page_hashes.emplace(ph, true);
        o += 12 + comp;
    }
    const size_t tail_start = o;

    std::vector<uint8_t> page;
    uint32_t new_page_hash = 0xF2ABF2AB;
    while (page_hashes.count(new_page_hash)) ++new_page_hash;
    size_t added = 0;
    for (const auto& kv : edits) {
        std::vector<uint8_t> u16;
        utf8_to_utf16be(kv.second, u16);
        u16.push_back(0);
        u16.push_back(0);
        const uint32_t chars = uint32_t(u16.size() / 2);
        const uint32_t off = uint32_t(page.size());
        put_be32(page, chars);
        page.insert(page.end(), u16.begin(), u16.end());
        auto it = dir_index.find(kv.first);
        if (it != dir_index.end()) {
            dir[it->second].page = new_page_hash;
            dir[it->second].off = off;
        } else {
            dir.push_back({kv.first, new_page_hash, off});
        }
        ++added;
    }
    if (!added) return true;
    std::sort(dir.begin(), dir.end(),
              [](const DirEnt& a, const DirEnt& b) {
                  return a.tag < b.tag;
              });
    std::vector<uint8_t> comp;
    if (!deflate_page(page, comp)) {
        err = "babel page deflate failed";
        return false;
    }

    std::vector<uint8_t> out;
    out.reserve(d.size() + comp.size() + edits.size() * 12 + 64);
    out.push_back(d[0]);
    out.push_back(d[1]);
    out.push_back(d[2]);
    out.push_back(d[3]);
    put_be32(out, uint32_t(dir.size()));
    for (const auto& e : dir) {
        put_be32(out, e.tag);
        put_be32(out, e.page);
        put_be32(out, e.off);
    }
    put_be32(out, page_count + 1);
    for (const auto& rp : raw_pages) {
        out.insert(out.end(), rp.bytes, rp.bytes + rp.size);
    }
    put_be32(out, new_page_hash);
    put_be32(out, uint32_t(comp.size()));
    put_be32(out, uint32_t(page.size()));
    out.insert(out.end(), comp.begin(), comp.end());
    out.insert(out.end(), d.begin() + tail_start, d.end());

    std::error_code ec;
    const std::string bak = path + ".bak";
    if (!std::filesystem::exists(bak, ec)) {
        std::filesystem::copy_file(path, bak, ec);
        if (ec) {
            err = "babel backup failed: " + bak;
            return false;
        }
    }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        err = "cannot write " + path;
        return false;
    }
    f.write(reinterpret_cast<const char*>(out.data()),
            (std::streamsize)out.size());
    if (!f) {
        err = "short write to " + path;
        return false;
    }
    return true;
}

}

bool LoadForRoot(const std::string& root_dir) {
    std::lock_guard<std::mutex> lk(mtx());
    Bank& b = bank();
    const auto paths = find_babels(root_dir);
    if (paths.empty()) {
        b = Bank{};
        return false;
    }
    if (b.ok && b.path == paths.front()) return true;
    Bank nb;
    std::vector<uint8_t> d;
    if (!read_file(paths.front(), d) || !parse_bank(d, nb)) {
        OutputLog::warn("text bank: failed to parse " + paths.front());
        b = Bank{};
        return false;
    }
    nb.ok = true;
    nb.path = paths.front();
    b = std::move(nb);
    OutputLog::info("text bank: " + b.path + " (" +
                    std::to_string(b.entries.size()) + " strings, " +
                    std::to_string(b.pages.size()) + " pages)");
    return true;
}

void Invalidate() {
    std::lock_guard<std::mutex> lk(mtx());
    bank() = Bank{};
}

bool Loaded() {
    std::lock_guard<std::mutex> lk(mtx());
    return bank().ok;
}

const std::string& LoadedPath() {
    std::lock_guard<std::mutex> lk(mtx());
    return bank().path;
}

bool HasTag(uint32_t tag_hash) {
    std::lock_guard<std::mutex> lk(mtx());
    return bank().entries.count(tag_hash) != 0;
}

bool Lookup(uint32_t tag_hash, std::string& out_utf8) {
    std::lock_guard<std::mutex> lk(mtx());
    if (!bank().ok) return false;
    return lookup_in(bank(), tag_hash, out_utf8);
}

uint32_t TagHash(const std::string& tag) {
    uint32_t h = 0x811C9DC5u;
    for (unsigned char c : tag) {
        h *= 0x01000193u;
        h ^= c;
    }
    return h;
}

bool LookupTag(const std::string& tag, std::string& out_utf8) {
    if (Lookup(TagHash(tag), out_utf8)) return true;




    std::string male;
    std::string female;
    const bool has_male = Lookup(TagHash(tag + "_HM"), male);
    const bool has_female = Lookup(TagHash(tag + "_HF"), female);
    if (!has_male && !has_female) return false;
    if (!has_male) {
        out_utf8 = std::move(female);
    } else if (!has_female || male == female) {
        out_utf8 = std::move(male);
    } else {
        const bool childhood = tag.find("TEXT_QUEST_QC010_") == 0;
        const char* male_label = childhood ? "Male Sparrow" : "Male Hero";
        const char* female_label = childhood ? "Female Sparrow" : "Female Hero";
        out_utf8 = std::string(male_label) + ": \"" + male +
            "\" / " + female_label + ": \"" + female + "\"";
    }
    return true;
}

uint32_t AllocTagHash(const std::string& seed) {
    std::lock_guard<std::mutex> lk(mtx());
    uint32_t h = TagHash(seed);
    if (!bank().ok) return h;
    while (bank().entries.count(h) != 0 || h == 0) ++h;
    return h;
}

bool ApplyEdits(const std::string& root_dir,
                const std::unordered_map<uint32_t, std::string>& edits,
                std::string& err) {
    if (edits.empty()) return true;
    const auto paths = find_babels(root_dir);
    if (paths.empty()) {
        err = "no book.babel found under " + root_dir;
        return false;
    }
    for (const auto& p : paths) {
        if (!rewrite_file(p, edits, err)) return false;
    }
    {
        std::lock_guard<std::mutex> lk(mtx());
        Bank& b = bank();
        b = Bank{};
    }
    LoadForRoot(root_dir);
    return true;
}

}
