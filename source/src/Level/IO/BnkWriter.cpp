#include "BnkWriter.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <zlib.h>

#include "BNKReader.cpp"
#include "ISO/IsoMount.h"
#include "UI/OutputLog.h"
#include "Utilities/Progress.h"
#include "Utilities/State.h"

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

bool deflate_table_chunks(const std::vector<uint8_t>& payload,
                          std::vector<uint8_t>& out) {
    if (payload.empty()) return false;
    z_stream z{};
    if (deflateInit2(&z, Z_BEST_COMPRESSION, Z_DEFLATED, 15, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK) return false;
    out.clear();
    std::vector<uint8_t> buf;
    size_t pos = 0;
    bool ok = true;
    while (pos < payload.size()) {
        const size_t take =
            std::min<size_t>(65536, payload.size() - pos);
        z.next_in = const_cast<Bytef*>(payload.data() + pos);
        z.avail_in = (uInt)take;
        buf.resize((size_t)deflateBound(&z, (uLong)take) + 128);
        z.next_out = buf.data();
        z.avail_out = (uInt)buf.size();
        const int ret = deflate(&z, Z_SYNC_FLUSH);
        if (ret != Z_OK || z.avail_in != 0) {
            ok = false;
            break;
        }
        const size_t made = buf.size() - z.avail_out;
        put32(out, (uint32_t)made);
        put32(out, (uint32_t)take);
        out.insert(out.end(), buf.data(), buf.data() + made);
        pos += take;
    }
    deflateEnd(&z);
    return ok && pos >= payload.size();
}

}

namespace {

size_t inflate_truncated_count(const uint8_t* data, size_t size) {
    z_stream z{};
    if (inflateInit2(&z, 15) != Z_OK) return 0;
    z.next_in = const_cast<Bytef*>(data);
    z.avail_in = (uInt)size;
    std::vector<uint8_t> buf(1u << 16);
    size_t produced = 0;
    for (;;) {
        z.next_out = buf.data();
        z.avail_out = (uInt)buf.size();
        const int ret = inflate(&z, Z_SYNC_FLUSH);
        produced += buf.size() - z.avail_out;
        if (ret != Z_OK || z.avail_in == 0) break;
    }
    inflateEnd(&z);
    return produced;
}

bool deflate_slot_chunks(const std::vector<uint8_t>& payload,
                         std::vector<uint8_t>& out,
                         std::vector<uint32_t>& sizes) {
    out.clear();
    sizes.clear();
    size_t pos = 0;
    while (pos < payload.size()) {
        const size_t remaining = payload.size() - pos;
        z_stream z{};
        if (deflateInit2(&z, Z_BEST_COMPRESSION, Z_DEFLATED, 15, 8,
                         Z_DEFAULT_STRATEGY) != Z_OK) return false;
        std::vector<uint8_t> comp(
            (size_t)deflateBound(&z, (uLong)remaining) + 64);
        z.next_in = const_cast<Bytef*>(payload.data() + pos);
        z.avail_in = (uInt)remaining;
        z.next_out = comp.data();
        z.avail_out = (uInt)comp.size();
        const int ret = deflate(&z, Z_SYNC_FLUSH);
        const bool ok = (ret == Z_OK && z.avail_in == 0);
        comp.resize(comp.size() - z.avail_out);
        deflateEnd(&z);
        if (!ok) return false;
        if (comp.size() <= 0x8000) {
            out.insert(out.end(), comp.begin(), comp.end());
            sizes.push_back((uint32_t)remaining);
            pos = payload.size();
        } else {
            const size_t n =
                inflate_truncated_count(comp.data(), 0x8000);
            if (!n || n >= remaining) return false;
            out.insert(out.end(), comp.begin(), comp.begin() + 0x8000);
            sizes.push_back((uint32_t)n);
            pos += n;
        }
    }
    return true;
}

}

bool AddEntriesToBnkBytes(std::vector<uint8_t>& bnk,
                          const std::vector<EntryAddition>& adds,
                          std::string& err) {
    if (adds.empty()) { err = "no additions given"; return false; }
    if (bnk.size() < 17) { err = "bnk too small"; return false; }
    const uint32_t base_old = be32(bnk.data());
    const uint32_t version = be32(bnk.data() + 4);
    if (version == 2) { err = "v2 BNK not supported"; return false; }
    const uint8_t compress_flag = bnk[8];

    std::vector<uint8_t> table_comp;
    size_t pos = 9;
    for (;;) {
        if (pos + 8 > bnk.size()) { err = "truncated table"; return false; }
        const uint32_t comp = be32(bnk.data() + pos);
        pos += 8;
        if (comp == 0) break;
        if (pos + comp > bnk.size()) { err = "truncated chunk"; return false; }
        table_comp.insert(table_comp.end(), bnk.data() + pos,
                          bnk.data() + pos + comp);
        pos += comp;
    }
    std::vector<uint8_t> table;
    if (!inflate_stream(table_comp, table)) {
        err = "table decompression failed";
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
        err = "bad table count";
        return false;
    }
    uint64_t data_end = 0;
    struct Old { size_t name_off, name_len, fields_off, fields_len; };
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t nlen = 0;
        if (!t_u32(nlen) || tp + nlen > table.size()) {
            err = "bad table name";
            return false;
        }
        tp += nlen;
        uint32_t rel = 0, usize = 0;
        if (!t_u32(rel) || !t_u32(usize)) { err = "bad entry"; return false; }
        uint64_t disk = usize;
        if (compress_flag == 1) {
            uint32_t csize = 0, nch = 0;
            if (!t_u32(csize) || !t_u32(nch) || nch > (1u << 20)) {
                err = "bad compressed entry";
                return false;
            }
            tp += 4ull * nch;
            if (tp > table.size()) { err = "bad chunk list"; return false; }
            disk = csize;
        }
        data_end = std::max(data_end, (uint64_t)rel + disk);
    }
    if ((uint64_t)base_old + data_end > bnk.size()) {
        err = "entry range outside bnk";
        return false;
    }

    std::vector<uint8_t> new_table(table.begin(), table.end());
    new_table[0] = uint8_t((count + adds.size()) >> 24);
    new_table[1] = uint8_t((count + adds.size()) >> 16);
    new_table[2] = uint8_t((count + adds.size()) >> 8);
    new_table[3] = uint8_t(count + adds.size());

    std::vector<uint8_t> new_data;
    uint64_t cursor = data_end;
    for (const auto& a : adds) {
        std::vector<uint8_t> disk_bytes;
        std::vector<uint32_t> chunk_sizes;
        if (compress_flag == 1) {
            if (!deflate_slot_chunks(a.payload, disk_bytes, chunk_sizes)) {
                err = "entry compression failed";
                return false;
            }
        } else {
            disk_bytes = a.payload;
        }
        std::string nm = a.name;
        nm.push_back('\0');
        put32(new_table, (uint32_t)nm.size());
        new_table.insert(new_table.end(), nm.begin(), nm.end());
        put32(new_table, (uint32_t)cursor);
        put32(new_table, (uint32_t)a.payload.size());
        if (compress_flag == 1) {
            put32(new_table, (uint32_t)disk_bytes.size());
            put32(new_table, (uint32_t)chunk_sizes.size());
            for (uint32_t v : chunk_sizes) put32(new_table, v);
        }
        new_data.insert(new_data.end(), disk_bytes.begin(),
                        disk_bytes.end());
        cursor += disk_bytes.size();
    }

    std::vector<uint8_t> new_table_comp;
    if (!deflate_table_chunks(new_table, new_table_comp)) {
        err = "table compression failed";
        return false;
    }
    const uint64_t header_need = 9 + new_table_comp.size() + 8;
    uint64_t base_new = base_old;
    if (header_need > base_new) {
        base_new = ((header_need + 2047) / 2048) * 2048;
    }

    std::vector<uint8_t> out;
    out.reserve(base_new + cursor);
    out.resize(9, 0);
    out[0] = uint8_t(base_new >> 24);
    out[1] = uint8_t(base_new >> 16);
    out[2] = uint8_t(base_new >> 8);
    out[3] = uint8_t(base_new);
    out[4] = uint8_t(version >> 24);
    out[5] = uint8_t(version >> 16);
    out[6] = uint8_t(version >> 8);
    out[7] = uint8_t(version);
    out[8] = compress_flag;
    out.insert(out.end(), new_table_comp.begin(), new_table_comp.end());
    put32(out, 0);
    put32(out, 0);
    out.resize(base_new, 0);
    out.insert(out.end(), bnk.begin() + base_old,
               bnk.begin() + base_old + (size_t)data_end);
    out.insert(out.end(), new_data.begin(), new_data.end());

    try {
        BNKReader check(out);
        const auto& files = check.list_files();
        if (files.size() != count + adds.size()) {
            err = "add self-check: entry count mismatch";
            return false;
        }
        for (size_t k = 0; k < adds.size(); ++k) {
            const int idx = (int)(count + k);
            std::vector<uint8_t> round = check.extract_index_bytes(idx);
            if (round != adds[k].payload) {
                err = "add self-check: payload round-trip mismatch";
                return false;
            }
        }
    } catch (const std::exception& ex) {
        err = std::string("add self-check failed: ") + ex.what();
        return false;
    }

    bnk.swap(out);
    return true;
}

bool RebuildWithChanges(const std::string& bnk_path,
                        const std::vector<EntryReplacement>& repls,
                        const std::vector<EntryAddition>& adds,
                        std::string& err) {
    if (repls.empty() && adds.empty()) {
        err = "no BNK changes given";
        return false;
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(bnk_path, ec)) {
        err = "BNK is not a loose file on disk";
        return false;
    }

    progress_update(10, 100, "Reading level BNK...");
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
    progress_update(35, 100, "Rebuilding level BNK...");

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
    struct PreparedReplacement {
        const EntryReplacement* replacement = nullptr;
        std::vector<uint8_t> disk_bytes;
        std::vector<uint32_t> chunks;
    };
    std::vector<PreparedReplacement> prepared;
    prepared.reserve(repls.size());
    for (std::size_t ri = 0; ri < repls.size(); ++ri) {
        const EntryReplacement& replacement = repls[ri];
        if (replacement.file_index < 0 ||
            replacement.file_index >= static_cast<int>(entries.size())) {
            err = "entry index out of range";
            return false;
        }
        for (std::size_t previous = 0; previous < ri; ++previous) {
            if (repls[previous].file_index == replacement.file_index) {
                err = "duplicate replacement entry index";
                return false;
            }
        }
        PreparedReplacement item;
        item.replacement = &replacement;
        if (compress_flag == 1) {
            if (!deflate_slot_chunks(replacement.payload, item.disk_bytes,
                                     item.chunks)) {
                err = "replacement entry compression failed";
                return false;
            }
        } else {
            item.disk_bytes = replacement.payload;
        }
        prepared.push_back(std::move(item));
    }
    auto find_repl = [&](size_t i) -> const PreparedReplacement* {
        for (const PreparedReplacement& item : prepared) {
            if (static_cast<size_t>(item.replacement->file_index) == i) {
                return &item;
            }
        }
        return nullptr;
    };

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
    for (const PreparedReplacement& item : prepared) {
        new_disk[static_cast<size_t>(item.replacement->file_index)] =
            static_cast<uint32_t>(item.disk_bytes.size());
    }

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
        const uint64_t want_mod = old_off[i] % 2048;
        if ((cursor % 2048) != want_mod) {
            cursor += (2048 + want_mod - (cursor % 2048)) % 2048;
        }
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
        const PreparedReplacement* r = find_repl(i);
        put32(new_table,
              r ? static_cast<uint32_t>(r->replacement->payload.size())
                : e.usize);
        if (compress_flag == 1) {
            if (r) {
                put32(new_table,
                      static_cast<uint32_t>(r->disk_bytes.size()));
                put32(new_table, static_cast<uint32_t>(r->chunks.size()));
                for (uint32_t v : r->chunks) put32(new_table, v);
            } else {
                put32(new_table, e.csize);
                put32(new_table, static_cast<uint32_t>(e.chunks.size()));
                for (uint32_t v : e.chunks) put32(new_table, v);
            }
        }
    }

    std::vector<uint8_t> new_table_comp;
    if (!deflate_table_chunks(new_table, new_table_comp)) {
        err = "file table compression failed";
        return false;
    }

    const uint64_t header_need = 9 + new_table_comp.size() + 8;
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
    std::memcpy(out.data() + hp, new_table_comp.data(),
                new_table_comp.size());
    hp += new_table_comp.size();
    hdr32(0);
    hdr32(0);

    for (size_t i = 0; i < entries.size(); ++i) {
        uint8_t* dst = out.data() + base_new + new_off[i];
        if (const PreparedReplacement* r = find_repl(i)) {
            std::memcpy(dst, r->disk_bytes.data(), r->disk_bytes.size());
        } else {
            std::memcpy(dst, src.data() + base_old + old_off[i],
                        old_disk[i]);
        }
    }

    if (!adds.empty() && !AddEntriesToBnkBytes(out, adds, err)) {
        err = "could not append BNK entries: " + err;
        return false;
    }

    progress_update(65, 100, "Verifying rebuilt BNK...");
    try {
        BNKReader check(out);
        const auto& files = check.list_files();
        if (files.size() != entries.size() + adds.size()) {
            err = "self-check failed: entry count mismatch";
            return false;
        }
        for (const auto& r : repls) {
            std::vector<uint8_t> round =
                check.extract_index_bytes(r.file_index);
            if (round != r.payload) {
                err = "self-check failed: payload round-trip mismatch";
                return false;
            }
        }
        for (std::size_t i = 0; i < adds.size(); ++i) {
            const int index = static_cast<int>(entries.size() + i);
            if (check.extract_index_bytes(index) != adds[i].payload) {
                err = "self-check failed: appended payload mismatch";
                return false;
            }
        }
    } catch (const std::exception& ex) {
        err = std::string("self-check failed: ") + ex.what();
        return false;
    }

    progress_update(80, 100, "Writing level BNK...");
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

bool RebuildWithReplacedEntries(const std::string& bnk_path,
                                const std::vector<EntryReplacement>& repls,
                                std::string& err) {
    return RebuildWithChanges(bnk_path, repls, {}, err);
}

bool RebuildWithReplacedEntry(const std::string& bnk_path,
                              int file_index,
                              const std::vector<uint8_t>& new_payload,
                              std::string& err) {
    std::vector<EntryReplacement> repls(1);
    repls[0].file_index = file_index;
    repls[0].payload = new_payload;
    return RebuildWithReplacedEntries(bnk_path, repls, err);
}

bool RebuildIsoLevelBnk(const std::string& virtual_path,
                        int file_index,
                        const std::vector<uint8_t>& new_payload,
                        std::string& err) {
    auto& iso = ISO::IsoMount::instance();
    if (!iso.is_mounted()) { err = "no ISO mounted"; return false; }
    const ISO::MountedFile* mf = iso.find(virtual_path);
    if (!mf) { err = "not found in ISO: " + virtual_path; return false; }

    const uint64_t src_abs =
        iso.base_offset() + (uint64_t)mf->sector * 2048ull;
    const uint64_t size_cur = mf->size;

    uint8_t head[9];
    if (!iso.raw_read_abs(src_abs, head, 9)) {
        err = "header read failed";
        return false;
    }
    const uint32_t base_old = be32(head);
    const uint32_t version = be32(head + 4);
    const uint8_t compress_flag = head[8];
    if (version == 2) { err = "v2 BNK rewrite not supported"; return false; }
    if (base_old < 17 || base_old > (64u << 20) || base_old > size_cur) {
        err = "implausible BNK base offset";
        return false;
    }

    std::vector<uint8_t> hdr_region(base_old);
    if (!iso.raw_read_abs(src_abs, hdr_region.data(), hdr_region.size())) {
        err = "header region read failed";
        return false;
    }

    std::vector<uint8_t> table_comp;
    size_t pos = 9;
    for (;;) {
        if (pos + 8 > hdr_region.size()) {
            err = "truncated table chunks";
            return false;
        }
        const uint32_t comp = be32(hdr_region.data() + pos);
        pos += 8;
        if (comp == 0) break;
        if (pos + comp > hdr_region.size()) {
            err = "truncated table chunk";
            return false;
        }
        table_comp.insert(table_comp.end(), hdr_region.data() + pos,
                          hdr_region.data() + pos + comp);
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
    TableEntry& target = entries[(size_t)file_index];
    if (target.csize != 0) {
        err = "target entry is chunk-compressed; rewrite not supported";
        return false;
    }

    uint64_t data_end_ex = 0;
    uint64_t data_end_all = 0;
    for (size_t i = 0; i < entries.size(); ++i) {
        const uint64_t end =
            (uint64_t)entries[i].rel_off + entries[i].disk_size();
        data_end_all = std::max(data_end_all, end);
        if (i != (size_t)file_index) {
            data_end_ex = std::max(data_end_ex, end);
        }
    }
    const bool target_last = (uint64_t)target.rel_off >= data_end_ex;
    const uint64_t target_rel_new =
        target_last ? target.rel_off : data_end_all;

    std::vector<uint8_t> new_table;
    new_table.reserve(table.size() + 16);
    put32(new_table, count);
    for (size_t i = 0; i < entries.size(); ++i) {
        const TableEntry& e = entries[i];
        put32(new_table, (uint32_t)e.raw_name.size());
        new_table.insert(new_table.end(), e.raw_name.begin(),
                         e.raw_name.end());
        if (i == (size_t)file_index) {
            put32(new_table, (uint32_t)target_rel_new);
            put32(new_table, (uint32_t)new_payload.size());
        } else {
            put32(new_table, e.rel_off);
            put32(new_table, e.usize);
        }
        if (compress_flag == 1) {
            put32(new_table, e.csize);
            put32(new_table, (uint32_t)e.chunks.size());
            for (uint32_t v : e.chunks) put32(new_table, v);
        }
    }
    std::vector<uint8_t> new_table_comp;
    if (!deflate_table_chunks(new_table, new_table_comp)) {
        err = "file table compression failed";
        return false;
    }

    const uint64_t header_need = 9 + new_table_comp.size() + 8;
    uint64_t base_new = base_old;
    if (header_need > base_new) {
        base_new = ((header_need + 2047) / 2048) * 2048;
    }
    const uint64_t content_size =
        base_new + target_rel_new + new_payload.size();
    if (content_size > 0xFFFFFFFFull) {
        err = "BNK would exceed 4 GiB";
        return false;
    }

    std::vector<uint8_t> hdr_out;
    hdr_out.reserve(header_need);
    put32(hdr_out, (uint32_t)base_new);
    put32(hdr_out, version);
    hdr_out.push_back(compress_flag);
    hdr_out.insert(hdr_out.end(), new_table_comp.begin(),
                   new_table_comp.end());
    put32(hdr_out, 0);
    put32(hdr_out, 0);

    const uint64_t capacity_cur = ((size_cur + 2047) / 2048) * 2048;
    const bool in_place =
        (base_new == base_old) && (content_size <= capacity_cur);

    if (in_place) {
        if (!iso.raw_write_abs(src_abs, hdr_out.data(), hdr_out.size())) {
            err = "header write failed";
            return false;
        }
        if (!iso.raw_write_abs(src_abs + base_new + target_rel_new,
                               new_payload.data(), new_payload.size())) {
            err = "payload write failed";
            return false;
        }
        if (content_size > size_cur) {
            if (!iso.repoint(virtual_path, mf->sector,
                             (uint32_t)content_size)) {
                err = "dirent size patch failed";
                return false;
            }
        } else {
            iso.repoint(virtual_path, mf->sector, (uint32_t)size_cur);
        }
        OutputLog::success("bnk: grew " + virtual_path +
                           " in place inside the ISO (" +
                           std::to_string(content_size) + " bytes)");
        return true;
    }

    const uint64_t iso_sz = iso.iso_size();
    if (iso_sz == 0 || iso_sz < iso.base_offset()) {
        err = "bad ISO size";
        return false;
    }
    const uint64_t dest_rel =
        ((iso_sz - iso.base_offset() + 2047) / 2048) * 2048;
    const uint64_t dest_abs = iso.base_offset() + dest_rel;
    const uint32_t new_sector = (uint32_t)(dest_rel / 2048);

    if (!iso.raw_write_abs(dest_abs, hdr_out.data(), hdr_out.size())) {
        err = "header write failed (relocate)";
        return false;
    }

    {
        const size_t kChunk = 8u << 20;
        std::vector<uint8_t> buf(kChunk);
        uint64_t off = 0;
        const int total_mb = (int)(data_end_all >> 20) + 1;
        while (off < data_end_all) {
            if (S.cancel_requested.load()) {
                err = "cancelled (original BNK untouched)";
                return false;
            }
            progress_update((int)(off >> 20), total_mb,
                            "Relocating level BNK inside ISO (" +
                            std::to_string(off >> 20) + " / " +
                            std::to_string(total_mb) + " MB)");
            const size_t n =
                (size_t)std::min<uint64_t>(kChunk, data_end_all - off);
            if (!iso.raw_read_abs(src_abs + base_old + off, buf.data(),
                                  n)) {
                err = "data copy read failed";
                return false;
            }
            if (!iso.raw_write_abs(dest_abs + base_new + off, buf.data(),
                                   n)) {
                err = "data copy write failed";
                return false;
            }
            off += n;
        }
        progress_update(total_mb, total_mb, "Finalising level BNK...");
    }

    if (!iso.raw_write_abs(dest_abs + base_new + target_rel_new,
                           new_payload.data(), new_payload.size())) {
        err = "payload write failed (relocate)";
        return false;
    }

    const uint64_t kSlack = 1u << 20;
    const uint64_t padded_size =
        ((content_size + kSlack + 2047) / 2048) * 2048;
    {
        const uint8_t zero = 0;
        if (!iso.raw_write_abs(dest_abs + padded_size - 1, &zero, 1)) {
            err = "tail pad write failed";
            return false;
        }
    }

    if (!iso.repoint(virtual_path, new_sector, (uint32_t)padded_size)) {
        err = "dirent repoint failed";
        return false;
    }

    {
        std::vector<uint8_t> verify(new_payload.size());
        if (!iso.raw_read_abs(dest_abs + base_new + target_rel_new,
                              verify.data(), verify.size()) ||
            verify != new_payload) {
            err = "self-check failed after relocate";
            return false;
        }
    }

    OutputLog::success("bnk: relocated " + virtual_path +
                       " to ISO end (sector " +
                       std::to_string(new_sector) + ", " +
                       std::to_string(padded_size) + " bytes)");
    return true;
}

}
