#include "BnkWriter.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <zlib.h>

#include "../BNKReader.cpp"
#include "../UI/OutputLog.h"

namespace BnkWriter {
namespace {

uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

void put32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(uint8_t(x >> 24));
    v.push_back(uint8_t(x >> 16));
    v.push_back(uint8_t(x >> 8));
    v.push_back(uint8_t(x));
}

struct TableEntry {
    std::vector<uint8_t> raw_name;
    uint32_t rel_off = 0;
    uint32_t usize = 0;
    uint32_t csize = 0;
    std::vector<uint32_t> chunks;
    uint32_t disk_size() const { return csize ? csize : usize; }
};

bool inflate_stream(const std::vector<uint8_t>& comp,
                    std::vector<uint8_t>& out) {
    for (int wbits : {15, -15, 31}) {
        z_stream z{};
        if (inflateInit2(&z, wbits) != Z_OK) continue;
        z.next_in = const_cast<Bytef*>(comp.data());
        z.avail_in = (uInt)comp.size();
        std::vector<uint8_t> buf;
        bool ok = true;
        for (;;) {
            const size_t before = buf.size();
            buf.resize(before + 65536);
            z.next_out = buf.data() + before;
            z.avail_out = 65536;
            const int ret = inflate(&z, Z_NO_FLUSH);
            buf.resize(before + (65536 - z.avail_out));
            if (ret == Z_STREAM_END) break;
            if (ret == Z_OK && z.avail_in == 0) break;
            if (ret == Z_BUF_ERROR && z.avail_in == 0) break;
            if (ret != Z_OK) { ok = false; break; }
        }
        inflateEnd(&z);
        if (ok && !buf.empty()) {
            out.swap(buf);
            return true;
        }
    }
    return false;
}

bool deflate_stream(const std::vector<uint8_t>& payload,
                    std::vector<uint8_t>& out) {
    z_stream z{};
    if (deflateInit2(&z, Z_BEST_COMPRESSION, Z_DEFLATED, 15, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK) return false;
    z.next_in = const_cast<Bytef*>(payload.data());
    z.avail_in = (uInt)payload.size();
    out.resize(deflateBound(&z, (uLong)payload.size()));
    z.next_out = out.data();
    z.avail_out = (uInt)out.size();
    const int ret = deflate(&z, Z_FINISH);
    const bool ok = (ret == Z_STREAM_END);
    out.resize(out.size() - z.avail_out);
    deflateEnd(&z);
    return ok;
}

}

bool RebuildWithReplacedEntry(const std::string& bnk_path,
                              int file_index,
                              const std::vector<uint8_t>& new_payload,
                              std::string& err) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(bnk_path, ec)) {
        err = "BNK is not a loose file on disk";
        return false;
    }

    std::vector<uint8_t> src;
    {
        std::ifstream f(bnk_path, std::ios::binary);
        if (!f) { err = "could not open " + bnk_path; return false; }
        f.seekg(0, std::ios::end);
        const std::streamoff n = f.tellg();
        f.seekg(0, std::ios::beg);
        src.resize((size_t)n);
        f.read(reinterpret_cast<char*>(src.data()), n);
        if (!f) { err = "short read of " + bnk_path; return false; }
    }
    if (src.size() < 17) { err = "file too small"; return false; }

    const uint32_t base_old = be32(src.data());
    const uint32_t version = be32(src.data() + 4);
    if (version == 2) { err = "v2 BNK rewrite not supported"; return false; }
    const uint8_t compress_flag = src[8];

    std::vector<uint8_t> table_comp;
    size_t pos = 9;
    for (;;) {
        if (pos + 8 > src.size()) { err = "truncated table chunks"; return false; }
        const uint32_t comp = be32(src.data() + pos);
        const uint32_t uncomp = be32(src.data() + pos + 4);
        (void)uncomp;
        pos += 8;
        if (comp == 0) break;
        if (pos + comp > src.size()) { err = "truncated table chunk"; return false; }
        table_comp.insert(table_comp.end(), src.data() + pos,
                          src.data() + pos + comp);
        pos += comp;
    }

    std::vector<uint8_t> table;
    if (!inflate_stream(table_comp, table)) {
        err = "file table decompression failed";
        return false;
    }

    size_t tp = 0;
    auto t_u32 = [&](uint32_t& v) -> bool {
        if (tp + 4 > table.size()) return false;
        v = be32(table.data() + tp);
        tp += 4;
        return true;
    };
    uint32_t count = 0;
    if (!t_u32(count) || count > (1u << 20)) {
        err = "bad file table count";
        return false;
    }
    std::vector<TableEntry> entries;
    entries.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        TableEntry e;
        uint32_t nlen = 0;
        if (!t_u32(nlen) || nlen > 1000000 || tp + nlen > table.size()) {
            err = "bad file table name";
            return false;
        }
        e.raw_name.assign(table.data() + tp, table.data() + tp + nlen);
        tp += nlen;
        if (!t_u32(e.rel_off) || !t_u32(e.usize)) {
            err = "bad file table entry";
            return false;
        }
        if (compress_flag == 1) {
            uint32_t chunk_count = 0;
            if (!t_u32(e.csize) || !t_u32(chunk_count) ||
                chunk_count > (1u << 20)) {
                err = "bad compressed table entry";
                return false;
            }
            e.chunks.reserve(chunk_count);
            for (uint32_t j = 0; j < chunk_count; ++j) {
                uint32_t v = 0;
                if (!t_u32(v)) { err = "bad chunk table"; return false; }
                e.chunks.push_back(v);
            }
        }
        entries.push_back(std::move(e));
    }
    if (file_index < 0 || file_index >= (int)entries.size()) {
        err = "entry index out of range";
        return false;
    }
    if (entries[(size_t)file_index].csize != 0) {
        err = "target entry is chunk-compressed; rewrite not supported";
        return false;
    }

    std::vector<size_t> order(entries.size());
    std::iota(order.begin(), order.end(), size_t(0));
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return entries[a].rel_off < entries[b].rel_off;
    });

    std::vector<uint32_t> old_off(entries.size());
    std::vector<uint32_t> old_disk(entries.size());
    for (size_t i = 0; i < entries.size(); ++i) {
        old_off[i] = entries[i].rel_off;
        old_disk[i] = entries[i].disk_size();
        if ((uint64_t)base_old + old_off[i] + old_disk[i] > src.size()) {
            err = "entry range outside file";
            return false;
        }
    }

    std::vector<uint32_t> new_disk = old_disk;
    new_disk[(size_t)file_index] = (uint32_t)new_payload.size();

    std::vector<uint32_t> new_off(entries.size());
    uint64_t cursor = 0;
    for (size_t k = 0; k < order.size(); ++k) {
        const size_t i = order[k];
        uint64_t gap;
        if (k == 0) {
            gap = old_off[i];
        } else {
            const size_t p = order[k - 1];
            const uint64_t prev_end = (uint64_t)old_off[p] + old_disk[p];
            gap = old_off[i] >= prev_end ? old_off[i] - prev_end : 0;
        }
        cursor += gap;
        new_off[i] = (uint32_t)cursor;
        cursor += new_disk[i];
    }
    const uint64_t data_size = cursor;

    std::vector<uint8_t> new_table;
    new_table.reserve(table.size() + 16);
    put32(new_table, count);
    for (size_t i = 0; i < entries.size(); ++i) {
        const TableEntry& e = entries[i];
        put32(new_table, (uint32_t)e.raw_name.size());
        new_table.insert(new_table.end(), e.raw_name.begin(),
                         e.raw_name.end());
        put32(new_table, new_off[i]);
        put32(new_table, i == (size_t)file_index
                             ? (uint32_t)new_payload.size()
                             : e.usize);
        if (compress_flag == 1) {
            put32(new_table, e.csize);
            put32(new_table, (uint32_t)e.chunks.size());
            for (uint32_t v : e.chunks) put32(new_table, v);
        }
    }

    std::vector<uint8_t> new_table_comp;
    if (!deflate_stream(new_table, new_table_comp)) {
        err = "file table compression failed";
        return false;
    }

    const uint64_t header_need = 9 + 8 + new_table_comp.size() + 8;
    uint64_t base_new = base_old;
    if (header_need > base_new) {
        base_new = ((header_need + 2047) / 2048) * 2048;
    }

    std::vector<uint8_t> out;
    out.resize(base_new + data_size, 0);
    out[0] = uint8_t(base_new >> 24);
    out[1] = uint8_t(base_new >> 16);
    out[2] = uint8_t(base_new >> 8);
    out[3] = uint8_t(base_new);
    out[4] = uint8_t(version >> 24);
    out[5] = uint8_t(version >> 16);
    out[6] = uint8_t(version >> 8);
    out[7] = uint8_t(version);
    out[8] = compress_flag;
    size_t hp = 9;
    auto hdr32 = [&](uint32_t v) {
        out[hp++] = uint8_t(v >> 24);
        out[hp++] = uint8_t(v >> 16);
        out[hp++] = uint8_t(v >> 8);
        out[hp++] = uint8_t(v);
    };
    hdr32((uint32_t)new_table_comp.size());
    hdr32((uint32_t)new_table.size());
    std::memcpy(out.data() + hp, new_table_comp.data(),
                new_table_comp.size());
    hp += new_table_comp.size();
    hdr32(0);
    hdr32(0);

    for (size_t i = 0; i < entries.size(); ++i) {
        uint8_t* dst = out.data() + base_new + new_off[i];
        if (i == (size_t)file_index) {
            std::memcpy(dst, new_payload.data(), new_payload.size());
        } else {
            std::memcpy(dst, src.data() + base_old + old_off[i],
                        old_disk[i]);
        }
    }

    try {
        BNKReader check(out);
        const auto& files = check.list_files();
        if (files.size() != entries.size()) {
            err = "self-check failed: entry count mismatch";
            return false;
        }
        std::vector<uint8_t> round =
            check.extract_index_bytes(file_index);
        if (round != new_payload) {
            err = "self-check failed: payload round-trip mismatch";
            return false;
        }
    } catch (const std::exception& ex) {
        err = std::string("self-check failed: ") + ex.what();
        return false;
    }

    const std::string tmp = bnk_path + ".tmp_write";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) { err = "could not write " + tmp; return false; }
        f.write(reinterpret_cast<const char*>(out.data()),
                (std::streamsize)out.size());
        if (!f) { err = "short write to " + tmp; return false; }
    }
    std::filesystem::remove(bnk_path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        err = "could not replace " + bnk_path +
              " (file in use?)";
        return false;
    }
    std::filesystem::rename(tmp, bnk_path, ec);
    if (ec) {
        err = "rename failed: " + ec.message() +
              " (rebuilt BNK left at " + tmp + ")";
        return false;
    }
    OutputLog::success("bnk: rebuilt " +
                       std::filesystem::path(bnk_path).filename().string() +
                       " (" + std::to_string(out.size()) + " bytes)");
    return true;
}

}
