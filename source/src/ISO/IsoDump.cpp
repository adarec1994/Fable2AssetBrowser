#include "IsoDump.h"

#include "IsoMount.h"
#include "../Utilities/State.h"
#include "../Utilities/Progress.h"
#include "../Utilities/Utils.h"
#include "../Utilities/Files.h"
#include "../UI/OutputLog.h"
#include "../UI/Panels/PanelInternal.h"
#include "../MDL/ModelParser.h"
#include "../BNKCore.cpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace ISO {

namespace {

constexpr size_t kChunkBytes = 4 * 1024 * 1024;   // 4 MiB

// ---------------------------------------------------------------------------
// Debug logger
// ---------------------------------------------------------------------------
// File-based, line-flushed log written next to the .exe. We open it
// fresh at the start of every dump and tear it down at the end. The
// goal is to localise the random crash the user is hitting somewhere
// near 75% — by writing a line BEFORE every potentially-faulty op
// (read_at, write, allocate) we know where the worker died from the
// last line in the file. flush after every write so nothing buffered
// is lost when the process drops. Cheap; the log is bytes per second
// at most.
// ---------------------------------------------------------------------------

std::ofstream g_dlog;
std::mutex    g_dlog_mutex;

std::string exe_dir() {
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        std::string p(buf, buf + n);
        size_t slash = p.find_last_of("\\/");
        if (slash != std::string::npos) p.resize(slash);
        return p;
    }
    return ".";
#else
    return ".";
#endif
}

void dlog_open() {
    std::lock_guard<std::mutex> lk(g_dlog_mutex);
    if (g_dlog.is_open()) g_dlog.close();
    auto path = std::filesystem::path(exe_dir()) / "dump_debug.log";
    g_dlog.open(path, std::ios::trunc);
    if (!g_dlog) return;
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char ts[32];
    std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm);
    g_dlog << "==== dump_debug.log opened " << ts << " ====\n";
    g_dlog.flush();
}

void dlog_close() {
    std::lock_guard<std::mutex> lk(g_dlog_mutex);
    if (g_dlog.is_open()) {
        g_dlog << "==== closed cleanly ====\n";
        g_dlog.close();
    }
}

void dlog(const std::string& msg) {
    std::lock_guard<std::mutex> lk(g_dlog_mutex);
    if (!g_dlog.is_open()) return;
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()).count() % 1000;
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char ts[40];
    std::snprintf(ts, sizeof(ts), "%02d:%02d:%02d.%03lld",
                  tm.tm_hour, tm.tm_min, tm.tm_sec, (long long)ms);
    g_dlog << '[' << ts << "] " << msg << '\n';
    g_dlog.flush();   // flush every line so a crash leaves the last
                      // line on disk; this is the whole point.
}

// ---------------------------------------------------------------------------
// Path / I/O helpers
// ---------------------------------------------------------------------------

std::filesystem::path build_out_path(const std::string& virtual_path) {
    std::string rel = virtual_path;
    while (!rel.empty() && (rel.front() == '/' || rel.front() == '\\'))
        rel.erase(rel.begin());
    std::filesystem::path root =
        S.export_dir.empty() ? std::filesystem::path("extracted")
                             : std::filesystem::path(S.export_dir);
    return root / rel;
}

bool stream_copy_one(int idx, int total,
                     const MountedFile& mf,
                     const std::filesystem::path& out,
                     std::vector<uint8_t>& buf) {
    char hdr[64];
    std::snprintf(hdr, sizeof(hdr), "[%d/%d] ", idx, total);
    const std::string p = hdr;

    dlog(p + "begin file path=" + mf.path +
         " size=" + std::to_string(mf.size) +
         " sector=" + std::to_string(mf.sector));

    std::error_code ec;
    if (auto parent = out.parent_path(); !parent.empty()) {
        dlog(p + "create_directories: " + parent.string());
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            dlog(p + "create_directories FAILED: " + ec.message());
            return false;
        }
    }

    dlog(p + "ofstream open: " + out.string());
    std::ofstream f(out, std::ios::binary | std::ios::trunc);
    if (!f) {
        dlog(p + "ofstream open FAILED");
        return false;
    }
    if (mf.size == 0) {
        dlog(p + "zero-size file, done");
        return f.good();
    }

    uint64_t remaining = mf.size;
    uint64_t offset    = 0;
    int      chunk_no  = 0;
    while (remaining > 0) {
        size_t n = (remaining < (uint64_t)kChunkBytes)
                       ? (size_t)remaining
                       : kChunkBytes;
        ++chunk_no;
        dlog(p + "read_at chunk=" + std::to_string(chunk_no) +
             " offset=" + std::to_string(offset) +
             " n=" + std::to_string(n));
        if (!IsoMount::instance().read_at(mf.path, offset, buf.data(), n)) {
            dlog(p + "read_at FAILED");
            return false;
        }
        dlog(p + "write n=" + std::to_string(n));
        f.write(reinterpret_cast<const char*>(buf.data()),
                (std::streamsize)n);
        if (!f.good()) {
            dlog(p + "write FAILED, ofstream good()=false");
            return false;
        }
        offset    += n;
        remaining -= n;
    }
    dlog(p + "file done OK");
    return f.good();
}

} // anonymous

void dump_iso_contents() {
    if (!IsoMount::instance().is_mounted()) {
        OutputLog::error("Dump: no ISO mounted.");
        return;
    }

    std::vector<MountedFile> targets =
        IsoMount::instance().list_recursive(".bnk");

    if (targets.empty()) {
        OutputLog::warn("Dump: no .bnk files found in ISO.");
        return;
    }

    IsoMount::instance().clear_cache();

    const std::string export_root =
        S.export_dir.empty() ? std::filesystem::absolute("extracted").string()
                             : S.export_dir;
    const int total = (int)targets.size();

    // Open the debug log fresh for this run. Its path is reported in
    // the OutputLog so the user knows where to look if it crashes.
    dlog_open();
    auto log_path = std::filesystem::path(exe_dir()) / "dump_debug.log";
    dlog("dump_iso_contents: total=" + std::to_string(total) +
         " export_root=" + export_root +
         " iso=" + IsoMount::instance().iso_path());
    OutputLog::info(std::string("Dumping ") + std::to_string(total) +
                    " BNK(s) → " + export_root +
                    " (debug log: " + log_path.string() + ")");

    progress_open(total, std::string("Dumping BNKs → ") + export_root);
    progress_update(0, total, "Starting...");

    std::thread([targets = std::move(targets), total]() {
        struct DumpGuard {
            ~DumpGuard() {
                progress_done();
                dlog("dump worker exit, closing debug log");
                dlog_close();
            }
        } pg;

        std::atomic<int> done{0};
        std::vector<std::string> failed;
        std::mutex fail_m;

        std::vector<uint8_t> buf;
        try {
            buf.resize(kChunkBytes);
            dlog("scratch buffer allocated, kChunkBytes=" +
                 std::to_string(kChunkBytes));
        } catch (const std::exception& e) {
            dlog(std::string("FATAL: scratch alloc threw: ") + e.what());
            OutputLog::error(std::string("Dump: cannot allocate I/O "
                                         "buffer: ") + e.what());
            return;
        }

        try {
            int idx = 0;
            for (const auto& mf : targets) {
                ++idx;
                if (S.cancel_requested.load() || S.exiting.load()) {
                    dlog("cancel/exit flag set, breaking loop at idx=" +
                         std::to_string(idx));
                    break;
                }

                const auto out = build_out_path(mf.path);
                bool ok = false;
                try {
                    ok = stream_copy_one(idx, total, mf, out, buf);
                } catch (const std::exception& e) {
                    dlog(std::string("EXCEPTION in stream_copy_one: ") +
                         e.what() + " (file=" + mf.path + ")");
                    OutputLog::error(std::string("Dump exception on ") +
                                     mf.path + ": " + e.what());
                } catch (...) {
                    dlog(std::string("UNKNOWN EXCEPTION in stream_copy_one"
                                     " (file=") + mf.path + ")");
                    OutputLog::error(std::string("Dump exception on ") +
                                     mf.path);
                }

                if (!ok) {
                    std::lock_guard<std::mutex> lk(fail_m);
                    failed.push_back(mf.path);
                    OutputLog::error(std::string("Dump failed: ") + mf.path);
                }

                int cur = ++done;
                progress_update(
                    cur, total,
                    std::filesystem::path(mf.path).filename().string());
            }
        } catch (const std::exception& e) {
            dlog(std::string("FATAL outer: ") + e.what());
            OutputLog::error(std::string("Dump worker aborted: ") + e.what());
            return;
        } catch (...) {
            dlog("FATAL outer: unknown");
            OutputLog::error("Dump worker aborted (unknown exception).");
            return;
        }

        dlog("loop done: done=" + std::to_string(done.load()) +
             " failed=" + std::to_string((int)failed.size()) +
             " cancel=" + std::to_string((int)S.cancel_requested.load()));

        if (S.cancel_requested.load()) {
            OutputLog::warn(std::string("Dump cancelled (")
                          + std::to_string(done.load()) + "/"
                          + std::to_string(total) + " written).");
            S.cancel_requested = false;
            return;
        }

        const int n_failed = (int)failed.size();
        if (n_failed > 0) {
            OutputLog::warn("Dump finished: " +
                            std::to_string(done.load() - n_failed) + "/" +
                            std::to_string(total) + " written, " +
                            std::to_string(n_failed) + " failed.");
        } else {
            OutputLog::success("Dump complete: " +
                               std::to_string(total) + " BNKs written.");
        }
    }).detach();
}

// ---------------------------------------------------------------------------
// MDL dump
// ---------------------------------------------------------------------------
// Iterates BNKs (not asset paths) and reconstructs every MDL inside by
// pairing each body BNK with its corresponding `_model_headers.bnk`.
// The dumped bytes are header + body concatenated, byte-for-byte the
// buffer the runtime feeds to the model parser — same shape as the
// in-app preview's `build_mdl_buffer_for_name`.
//
// Why per-BNK and not per-path: the previous version called
// `build_mdl_buffer_for_name(e.name, …)` which is hardcoded to the
// `globals_models.bnk` / `globals_model_headers.bnk` pair, so anything
// that lived in a character / region / dlc BNK silently failed the
// rebuild. Going through the source BNK directly + a name-derived
// header pair lookup makes the rebuild work for every BNK on disc.
//
// Pairing rule: take the source BNK's basename, replace
// `_models.bnk` with `_model_headers.bnk`, and look that up via
// `find_bnk_by_filename`. If the pair doesn't exist we fall back to
// `globals_model_headers.bnk` (a few older nested archives ship a
// body-only and pull headers from globals), and as a last resort we
// emit the body raw. That last case mirrors what
// `reconstruct_nested_mdl` already does for unpaired nested MDLs.

// Helper: write a buffer through to disk under the export root,
// preserving the asset path. Mirrors stream_copy_one's mkdir/write
// flow without the chunked read (the asset is already fully in
// memory). Shared by every dump_*_files variant below.
static bool write_buf_to_disk(const std::filesystem::path& out,
                              const std::vector<unsigned char>& bytes) {
    std::error_code ec;
    if (auto parent = out.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) return false;
    }
    std::ofstream f(out, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    if (!bytes.empty()) {
        f.write(reinterpret_cast<const char*>(bytes.data()),
                (std::streamsize)bytes.size());
    }
    return f.good();
}

// Build the on-disk destination for a flat asset entry. Same path
// normalisation pattern as the BNK dump — strip leading slashes so
// the concat doesn't accidentally root the result, and force the
// expected extension on if `full_path` somehow lost it (defensive
// only — every flat-list entry already has the right extension).
static std::filesystem::path build_asset_out_path(const FlatAssetEntry& e,
                                                  const char* expected_ext) {
    std::string rel = e.full_path;
    while (!rel.empty() && (rel.front() == '/' || rel.front() == '\\'))
        rel.erase(rel.begin());
    std::filesystem::path root =
        S.export_dir.empty() ? std::filesystem::path("extracted")
                             : std::filesystem::path(S.export_dir);
    std::filesystem::path p(rel);
    if (p.extension() != expected_ext) p += expected_ext;
    return root / p;
}

// ---------------------------------------------------------------------------
// Cache of opened BNK readers + their lowercased basename → file index
// maps. Building these once per BNK is the whole point of the BNK-based
// rewrite: the previous path-based version reopened globals BNKs (and
// rebuilt the lookup map) once per asset, which dominated the runtime
// for a 5000-MDL dump. Now each BNK opens once and serves every MDL it
// contains.
//
// We hold the reader behind a unique_ptr because BNKReader has no
// default constructor (it requires a path or byte buffer at
// construction). That lets us default-construct the cache entry first
// and only build the reader on demand.
// ---------------------------------------------------------------------------
struct BnkCacheEntry {
    std::unique_ptr<BNKReader> reader;
    // leaf-filename (lowercased) → index into reader->list_files()
    std::unordered_map<std::string, int> by_leaf;
};

static BnkCacheEntry* get_or_open_bnk(
    std::unordered_map<std::string, BnkCacheEntry>& cache,
    const std::string& bnk_path)
{
    auto& ce = cache[bnk_path];
    if (ce.reader) return &ce;
    try {
        // BNKReader constructor opens; reader streams file extracts on
        // demand (no full pre-decompression).
        ce.reader = std::make_unique<BNKReader>(bnk_path);
        const auto& files = ce.reader->list_files();
        ce.by_leaf.reserve(files.size());
        for (size_t i = 0; i < files.size(); ++i) {
            std::string leaf = std::filesystem::path(files[i].name)
                                   .filename().string();
            std::transform(leaf.begin(), leaf.end(), leaf.begin(), ::tolower);
            ce.by_leaf.emplace(std::move(leaf), (int)i);
        }
        return &ce;
    } catch (...) {
        // Drop the half-built entry so a future caller could retry.
        cache.erase(bnk_path);
        return nullptr;
    }
}

// Generic pair-name derivation. Strips `body_suffix` from the source
// BNK's basename and appends `header_suffix`, then looks the result
// up via `find_bnk_by_filename` (which walks both top-level BNKs and
// the nested-BNK temp paths the file-tree builder extracted). On
// miss, falls back to `globals_fallback` — for both MDLs and TEXs the
// global headers BNK is sometimes the only source of headers for
// body-only nested archives.
static std::optional<std::string> derive_paired_bnk(
    const std::string& body_bnk_path,
    const char* body_suffix,
    const char* header_suffix,
    const char* globals_fallback)
{
    std::string base = std::filesystem::path(body_bnk_path)
                           .filename().string();
    std::string base_lower = base;
    std::transform(base_lower.begin(), base_lower.end(),
                   base_lower.begin(), ::tolower);
    const std::string body_sfx(body_suffix);
    if (base_lower.size() >= body_sfx.size() &&
        base_lower.compare(base_lower.size() - body_sfx.size(),
                           body_sfx.size(), body_sfx) == 0) {
        std::string paired =
            base_lower.substr(0, base_lower.size() - body_sfx.size())
            + header_suffix;
        if (auto p = find_bnk_by_filename(paired)) return p;
    }
    // Last-resort fallback. Most non-globals BNKs ship a sibling pair
    // these days, but a few nested archives are body-only and pull
    // headers from globals — same fallback chain reconstruct_nested_mdl
    // uses for MDLs.
    if (globals_fallback) {
        if (auto p = find_bnk_by_filename(globals_fallback)) return p;
    }
    return std::nullopt;
}

// Convenience wrappers for the two kinds of pairs we currently
// reconstruct. MDL: `_models.bnk` ↔ `_model_headers.bnk`. TEX:
// `_textures.bnk` ↔ `_texture_headers.bnk`. Both fall back to
// `globals_*_headers.bnk` when no sibling pair is on disc.
static std::optional<std::string> derive_paired_model_headers_bnk(
    const std::string& body_bnk_path)
{
    return derive_paired_bnk(body_bnk_path, "_models.bnk",
                             "_model_headers.bnk",
                             "globals_model_headers.bnk");
}

static std::optional<std::string> derive_paired_texture_headers_bnk(
    const std::string& body_bnk_path)
{
    // Try the standard `_textures.bnk` ↔ `_texture_headers.bnk` pair
    // first. The `gui_*` pair has its own special form
    // (`gui_textures.bnk` ↔ `gui_texture_headers.bnk`) that the
    // suffix-substitution rule already handles correctly because
    // both names end in the standard suffixes.
    return derive_paired_bnk(body_bnk_path, "_textures.bnk",
                             "_texture_headers.bnk",
                             "globals_texture_headers.bnk");
}

// Reconstruct one MDL by pulling its body from `body_cache_entry` at
// `file_index` and (if available) prefixing the matching header from
// `header_cache_entry`. Either cache pointer can be nullptr — that
// case emits body-only, which is the right thing when the source BNK
// has no header pair.
//
// The temp file dance is the same shape extract_one + read_all_bytes
// uses everywhere else; doing it inline here avoids re-entering the
// flat-list lookup path that the previous version went through.
static bool reconstruct_one_mdl(
    BnkCacheEntry* body_ce,
    const std::string& body_bnk_path,
    int file_index,
    BnkCacheEntry* header_ce,
    const std::string& header_bnk_path,
    std::vector<unsigned char>& out)
{
    if (!body_ce || !body_ce->reader) return false;
    const auto& body_files = body_ce->reader->list_files();
    if (file_index < 0 || (size_t)file_index >= body_files.size())
        return false;

    auto tmpdir = std::filesystem::temp_directory_path() / "f2_mdl_dump";
    std::error_code ec;
    std::filesystem::create_directories(tmpdir, ec);

    auto tmp_body = tmpdir /
        ("body_" + std::to_string(file_index) + ".bin");
    std::vector<unsigned char> body;
    try {
        extract_one(body_bnk_path, file_index, tmp_body.string());
        body = read_all_bytes(tmp_body);
        std::filesystem::remove(tmp_body, ec);
    } catch (...) {
        std::filesystem::remove(tmp_body, ec);
        return false;
    }
    if (body.empty()) return false;

    if (!header_ce) {
        out = std::move(body);
        return true;
    }

    // Match on the leaf filename — sibling BNKs sometimes use
    // slightly different folder hierarchies for the same logical
    // asset, mirroring reconstruct_nested_mdl's matching rule.
    std::string leaf = std::filesystem::path(body_files[file_index].name)
                           .filename().string();
    std::transform(leaf.begin(), leaf.end(), leaf.begin(), ::tolower);
    auto h_it = header_ce->by_leaf.find(leaf);
    if (h_it == header_ce->by_leaf.end()) {
        // Body without a paired header — same fallback as the nested
        // case, emit body raw rather than failing the whole entry.
        out = std::move(body);
        return true;
    }

    auto tmp_h = tmpdir /
        ("hdr_" + std::to_string(h_it->second) + ".bin");
    std::vector<unsigned char> hbuf;
    try {
        extract_one(header_bnk_path, h_it->second, tmp_h.string());
        hbuf = read_all_bytes(tmp_h);
        std::filesystem::remove(tmp_h, ec);
    } catch (...) {
        std::filesystem::remove(tmp_h, ec);
        // Header extraction failed — body alone is still useful.
        out = std::move(body);
        return true;
    }
    if (hbuf.empty()) { out = std::move(body); return true; }

    out.clear();
    out.reserve(hbuf.size() + body.size());
    out.insert(out.end(), hbuf.begin(), hbuf.end());
    out.insert(out.end(), body.begin(), body.end());
    return true;
}

void dump_mdl_files() {
    if (S.all_mdl_files.empty()) {
        OutputLog::warn("Dump MDL: no .mdl files indexed (open a "
                        "Fable 2 root first).");
        return;
    }

    // Snapshot to avoid racing with the file-tree builder if the user
    // somehow hits this mid-build. The snapshot is small (a few KB
    // per entry × thousands of entries = a few MB at most).
    std::vector<FlatAssetEntry> targets = S.all_mdl_files;

    // Group by source BNK path so each body BNK opens exactly once.
    // The order of entries within a BNK is preserved (we just walk
    // them in flat-list order). The header BNK opens once-per-body-
    // BNK as well via the cache below.
    std::unordered_map<std::string, std::vector<int>> by_bnk;
    by_bnk.reserve(64);
    for (size_t i = 0; i < targets.size(); ++i) {
        by_bnk[targets[i].bnk_path].push_back((int)i);
    }
    const int total = (int)targets.size();

    const std::string export_root =
        S.export_dir.empty() ? std::filesystem::absolute("extracted").string()
                             : S.export_dir;

    OutputLog::info(std::string("Dumping ") + std::to_string(total) +
                    " .mdl file(s) from " +
                    std::to_string(by_bnk.size()) + " BNK(s) → " +
                    export_root);
    progress_open(total, std::string("Dumping MDLs → ") + export_root);
    progress_update(0, total, "Starting...");

    std::thread([targets = std::move(targets),
                 by_bnk = std::move(by_bnk),
                 total]() {
        struct DumpGuard {
            ~DumpGuard() { progress_done(); }
        } pg;

        std::atomic<int> done{0};
        std::vector<std::string> failed;
        std::mutex fail_m;

        // Per-worker BNK reader cache. Lives only for the duration of
        // this dump — we don't want the readers to outlive the
        // operation since the file-tree builder might re-extract
        // nested temp paths on the next root open.
        std::unordered_map<std::string, BnkCacheEntry> bnk_cache;

        try {
            for (const auto& [bnk_path, indices] : by_bnk) {
                if (S.cancel_requested.load() || S.exiting.load()) break;

                BnkCacheEntry* body_ce = get_or_open_bnk(bnk_cache, bnk_path);
                if (!body_ce) {
                    OutputLog::error(std::string("MDL dump: cannot open ")
                                     + bnk_path);
                    // Mark every entry from this BNK as failed and
                    // continue with the next one.
                    for (int ti : indices) {
                        std::lock_guard<std::mutex> lk(fail_m);
                        failed.push_back(targets[(size_t)ti].full_path);
                        ++done;
                    }
                    progress_update(done.load(), total,
                                    std::filesystem::path(bnk_path)
                                        .filename().string());
                    continue;
                }

                // Resolve the paired header BNK once per body BNK.
                // Empty string means "no pair available" — body-only
                // dumps for everything in this BNK.
                std::string header_path;
                BnkCacheEntry* header_ce = nullptr;
                if (auto p = derive_paired_headers_bnk(bnk_path)) {
                    header_path = *p;
                    header_ce = get_or_open_bnk(bnk_cache, header_path);
                    // If the paired BNK exists in the index but fails
                    // to open, fall through to body-only — better to
                    // emit unpaired bodies than fail the whole BNK.
                }

                for (int ti : indices) {
                    if (S.cancel_requested.load() || S.exiting.load()) break;
                    const auto& e = targets[(size_t)ti];

                    std::vector<unsigned char> buf;
                    bool ok = false;
                    try {
                        ok = reconstruct_one_mdl(body_ce, bnk_path,
                                                 e.file_index,
                                                 header_ce, header_path,
                                                 buf);
                    } catch (const std::exception& ex) {
                        OutputLog::error(std::string("MDL exception on ") +
                                         e.full_path + ": " + ex.what());
                        ok = false;
                    } catch (...) {
                        OutputLog::error(std::string("MDL exception on ") +
                                         e.full_path);
                        ok = false;
                    }

                    if (ok && !buf.empty()) {
                        auto out = build_asset_out_path(e, ".mdl");
                        ok = write_buf_to_disk(out, buf);
                        if (!ok) {
                            OutputLog::error(std::string("MDL write failed: ")
                                             + out.string());
                        }
                    } else {
                        OutputLog::error(std::string("MDL rebuild failed: ") +
                                         e.full_path);
                    }

                    if (!ok) {
                        std::lock_guard<std::mutex> lk(fail_m);
                        failed.push_back(e.full_path);
                    }

                    int cur = ++done;
                    progress_update(cur, total,
                                    std::filesystem::path(e.name)
                                        .filename().string());
                }
            }
        } catch (const std::exception& ex) {
            OutputLog::error(std::string("MDL dump worker aborted: ") +
                             ex.what());
            return;
        } catch (...) {
            OutputLog::error("MDL dump worker aborted (unknown exception).");
            return;
        }

        if (S.cancel_requested.load()) {
            OutputLog::warn(std::string("MDL dump cancelled (")
                          + std::to_string(done.load()) + "/"
                          + std::to_string(total) + " written).");
            S.cancel_requested = false;
            return;
        }

        const int n_failed = (int)failed.size();
        if (n_failed > 0) {
            OutputLog::warn("MDL dump finished: " +
                            std::to_string(done.load() - n_failed) + "/" +
                            std::to_string(total) + " written, " +
                            std::to_string(n_failed) + " failed.");
        } else {
            OutputLog::success("MDL dump complete: " +
                               std::to_string(total) + " files written.");
        }
    }).detach();
}

} // namespace ISO
