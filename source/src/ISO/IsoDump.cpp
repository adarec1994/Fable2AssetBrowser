#include "IsoDump.h"

#include "IsoMount.h"
#include "../Utilities/State.h"
#include "../Utilities/Progress.h"
#include "../Utilities/Utils.h"
#include "../Utilities/Files.h"
#include "../UI/OutputLog.h"
#include "../UI/Panels/PanelInternal.h"
#include "../MDL/ModelParser.h"
#include "../MDL/mdl_converter.h"    // mdl_to_glb_full
#include "../MDL/MdlFbxExport.h"     // mdl_to_fbx_full
#include "../Audio/XmaDecoder.h"     // decode_xma_wav_file_to_pcm_wav
#include "../Audio/MfAudioEncoder.h" // PCM→MP3/AAC via Media Foundation
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
// destination extension to `expected_ext` REPLACING any existing
// extension on the asset path. The earlier `if (p.extension() !=
// expected_ext) p += expected_ext;` shape was a bug: for cross-
// format outputs (audio .wav → .mp3 / .m4a) the source's `.wav`
// would still be in the path and the new extension was tacked on,
// producing `foo.wav.mp3` files that some players refuse. Using
// replace_extension keeps the original behaviour for same-extension
// dumps (.mdl/.tex/.wav-raw — no-op since old == new) and produces
// the right `.mp3` / `.m4a` for cross-format ones.
static std::filesystem::path build_asset_out_path(const FlatAssetEntry& e,
                                                  const char* expected_ext) {
    std::string rel = e.full_path;
    while (!rel.empty() && (rel.front() == '/' || rel.front() == '\\'))
        rel.erase(rel.begin());
    std::filesystem::path root =
        S.export_dir.empty() ? std::filesystem::path("extracted")
                             : std::filesystem::path(S.export_dir);
    std::filesystem::path p(rel);
    p.replace_extension(expected_ext);
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
                if (auto p = derive_paired_model_headers_bnk(bnk_path)) {
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

// ---------------------------------------------------------------------------
// MDL — decoded variants (GLB / FBX) and the per-file entry point.
// ---------------------------------------------------------------------------
// Reuses reconstruct_one_mdl (paired header+body BNK lookup) to get
// the raw MDL bytes the parsers + exporters consume. Each entry runs
// through `mdl_to_glb_full` or `mdl_to_fbx_full` — both already pull
// in textures via `build_any_tex_buffer_for_name`, decode them via
// `mdl_export_decode_texture_to_png`, and embed the PNG bytes in the
// output file (GLB.images or FBX.Video.Content). Skin / weights /
// UVs are populated by the same parsers the in-app preview uses.

namespace {

// Per-file rebuild (raw bytes from BNK pair) using the same one-shot
// helpers Selection.cpp's right-click menu uses. Mirrors the inner
// loop in dump_mdl_files but for one entry. Returns the
// reconstructed MDL bytes; on failure logs and returns empty.
std::vector<unsigned char> reconstruct_one_mdl_for_export(
    const std::string& bnk_path, int file_index)
{
    std::vector<unsigned char> body, header_bytes;
    std::error_code ec;
    auto tmpdir = std::filesystem::temp_directory_path() / "f2_mdl_export_oneoff";
    std::filesystem::create_directories(tmpdir, ec);

    try {
        BNKReader src(bnk_path);
        const auto& src_files = src.list_files();
        if (file_index < 0 || (size_t)file_index >= src_files.size())
            return {};
        std::string mdl_name = src_files[file_index].name;

        auto tmp_body = tmpdir / "body.bin";
        extract_one(bnk_path, file_index, tmp_body.string());
        body = read_all_bytes(tmp_body);
        std::filesystem::remove(tmp_body, ec);
        if (body.empty()) return {};

        // Locate paired _model_headers.bnk via name swap; fall back
        // to globals_model_headers; final fallback is body-only.
        std::string base = std::filesystem::path(bnk_path)
                               .filename().string();
        std::transform(base.begin(), base.end(), base.begin(), ::tolower);
        std::optional<std::string> p_headers;
        const std::string suffix = "_models.bnk";
        if (base.size() >= suffix.size() &&
            base.compare(base.size() - suffix.size(),
                         suffix.size(), suffix) == 0) {
            std::string paired = base.substr(0, base.size() - suffix.size())
                               + "_model_headers.bnk";
            p_headers = find_bnk_by_filename(paired);
        }
        if (!p_headers) {
            p_headers = find_bnk_by_filename("globals_model_headers.bnk");
        }
        if (!p_headers) return body;   // body alone

        BNKReader hr(*p_headers);
        const auto& h_files = hr.list_files();
        std::string mdl_leaf = std::filesystem::path(mdl_name)
                                   .filename().string();
        std::transform(mdl_leaf.begin(), mdl_leaf.end(),
                       mdl_leaf.begin(), ::tolower);
        int h_idx = -1;
        for (size_t i = 0; i < h_files.size(); ++i) {
            std::string hn = std::filesystem::path(h_files[i].name)
                                 .filename().string();
            std::transform(hn.begin(), hn.end(), hn.begin(), ::tolower);
            if (hn == mdl_leaf) { h_idx = (int)i; break; }
        }
        if (h_idx < 0) return body;

        auto tmp_h = tmpdir / "header.bin";
        extract_one(*p_headers, h_idx, tmp_h.string());
        header_bytes = read_all_bytes(tmp_h);
        std::filesystem::remove(tmp_h, ec);
        if (header_bytes.empty()) return body;
    } catch (...) {
        return body;
    }

    std::vector<unsigned char> out;
    out.reserve(header_bytes.size() + body.size());
    out.insert(out.end(), header_bytes.begin(), header_bytes.end());
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

const char* mdl_fmt_label(MdlExportFormat fmt) {
    switch (fmt) {
        case MdlExportFormat::GLB: return "GLB";
        case MdlExportFormat::FBX: return "FBX";
        case MdlExportFormat::RAW: return "MDL";
    }
    return "?";
}

const char* mdl_fmt_ext(MdlExportFormat fmt) {
    switch (fmt) {
        case MdlExportFormat::GLB: return ".glb";
        case MdlExportFormat::FBX: return ".fbx";
        case MdlExportFormat::RAW: return ".mdl";
    }
    return ".bin";
}

} // anonymous

void mdl_export_begin_named(MdlExportFormat fmt,
                            const std::string& bnk_path,
                            int file_index,
                            const std::string& display_path,
                            bool /*from_nested*/)
{
    if (bnk_path.empty() || file_index < 0) {
        OutputLog::error("MDL export: missing bnk / index arg.");
        return;
    }

    // Resolve the BNK entry's stored asset path — that's what we use
    // for the output destination so the file tree mirrors the source
    // archive layout (`art/characters/.../foo.mdl`). Caller can
    // override via `display_path` (the file tree / flat tabs pass
    // their indexed full_path here when they have it; the right-
    // click drill view passes the same string the BNK has).
    std::string entry_name;
    try {
        BNKReader r(bnk_path);
        const auto& files = r.list_files();
        if ((size_t)file_index < files.size()) {
            entry_name = files[file_index].name;
        }
    } catch (...) { /* fall through — entry_name stays empty */ }

    // Pick the most specific path we have for the output filename:
    // caller-supplied display_path > BNK's stored name > generic
    // "model_<idx>.mdl". The display string in log lines uses the
    // same fall-back chain.
    std::string out_rel;
    if (!display_path.empty() && display_path.find('/') != std::string::npos) {
        out_rel = display_path;
    } else if (!entry_name.empty()) {
        out_rel = entry_name;
    } else if (!display_path.empty()) {
        out_rel = display_path;
    } else {
        out_rel = std::string("model_") + std::to_string(file_index) + ".mdl";
    }
    std::string log_label = display_path.empty()
        ? std::filesystem::path(out_rel).filename().string()
        : std::filesystem::path(display_path).filename().string();

    auto buf = reconstruct_one_mdl_for_export(bnk_path, file_index);
    if (buf.empty()) {
        OutputLog::error(std::string("MDL export: rebuild failed for ")
                         + log_label);
        return;
    }

    // Output path mirrors the convention all the other exporters use
    // — replace_extension instead of += so .mdl source paths come
    // out as `.glb` / `.fbx` rather than `.mdl.glb` / `.mdl.fbx`.
    std::string rel = out_rel;
    while (!rel.empty() && (rel.front() == '/' || rel.front() == '\\'))
        rel.erase(rel.begin());
    std::filesystem::path root =
        S.export_dir.empty() ? std::filesystem::path("extracted")
                             : std::filesystem::path(S.export_dir);
    auto out = root / std::filesystem::path(rel);
    out.replace_extension(mdl_fmt_ext(fmt));

    std::error_code ec;
    if (auto parent = out.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            OutputLog::error(std::string("MDL export: cannot create ") +
                             parent.string() + " — " + ec.message());
            return;
        }
    }

    bool ok = false;
    std::string err;
    try {
        switch (fmt) {
            case MdlExportFormat::GLB:
                ok = mdl_to_glb_full(buf, out.string(), out_rel, err);
                break;
            case MdlExportFormat::FBX:
                ok = mdl_to_fbx_full(buf, out.string(), out_rel, err);
                break;
            case MdlExportFormat::RAW: {
                std::ofstream f(out, std::ios::binary | std::ios::trunc);
                if (f) {
                    f.write((const char*)buf.data(),
                            (std::streamsize)buf.size());
                    ok = f.good();
                } else err = "cannot open output for writing";
                break;
            }
        }
    } catch (const std::exception& ex) {
        err = ex.what();
    } catch (...) {
        err = "unknown exception";
    }

    if (ok) {
        OutputLog::success(std::string("Exported ") + log_label +
                           " as " + mdl_fmt_label(fmt) + " → " +
                           out.string());
    } else {
        OutputLog::error(std::string("MDL export failed (") +
                         mdl_fmt_label(fmt) + "): " + log_label +
                         (err.empty() ? "" : " — " + err));
    }
}

void dump_mdl_files_as(MdlExportFormat fmt) {
    if (fmt == MdlExportFormat::RAW) {
        // Raw bytes path is exactly what dump_mdl_files() does.
        dump_mdl_files();
        return;
    }
    if (S.all_mdl_files.empty()) {
        OutputLog::warn("Dump MDL: no .mdl files indexed (open a "
                        "Fable 2 root first).");
        return;
    }

    std::vector<FlatAssetEntry> targets = S.all_mdl_files;
    const int total = (int)targets.size();
    const std::string export_root =
        S.export_dir.empty() ? std::filesystem::absolute("extracted").string()
                             : S.export_dir;

    OutputLog::info(std::string("Exporting ") + std::to_string(total) +
                    " .mdl file(s) as " + mdl_fmt_label(fmt) + " → " +
                    export_root);
    progress_open(total,
                  std::string("Exporting MDLs as ") + mdl_fmt_label(fmt) +
                  " → " + export_root);
    progress_update(0, total, "Starting...");

    std::thread([targets = std::move(targets), total, fmt]() {
        struct PG { ~PG() { progress_done(); } } pg;
        std::atomic<int> done{0};
        std::vector<std::string> failed;
        std::mutex fail_m;

        for (const auto& e : targets) {
            if (S.cancel_requested.load() || S.exiting.load()) break;
            try {
                mdl_export_begin_named(fmt, e.bnk_path, e.file_index,
                                       e.full_path, e.from_nested);
            } catch (...) {
                std::lock_guard<std::mutex> lk(fail_m);
                failed.push_back(e.full_path);
            }
            int cur = ++done;
            progress_update(cur, total,
                            std::filesystem::path(e.name)
                                .filename().string());
        }

        if (S.cancel_requested.load()) {
            OutputLog::warn(std::string("MDL export cancelled (") +
                            std::to_string(done.load()) + "/" +
                            std::to_string(total) + ").");
            S.cancel_requested = false;
            return;
        }
        const int n_failed = (int)failed.size();
        if (n_failed > 0) {
            OutputLog::warn(std::string("MDL export finished as ") +
                            mdl_fmt_label(fmt) + ": " +
                            std::to_string(done.load() - n_failed) +
                            "/" + std::to_string(total) + " written, " +
                            std::to_string(n_failed) + " failed.");
        } else {
            OutputLog::success(std::string("MDL export complete as ") +
                               mdl_fmt_label(fmt) + ": " +
                               std::to_string(total) + " files written.");
        }
    }).detach();
}

// ---------------------------------------------------------------------------
// TEX dump
// ---------------------------------------------------------------------------
// Same shape as the MDL dump (group by source BNK, open paired
// _texture_headers BNK once, walk every tex inside) but textures
// have a third optional component: a globally-shared
// `1024mip0_textures.bnk` carrying just the largest mip for textures
// that need it. Concatenation order — header + mip0 + body — matches
// `build_tex_buffer_for_name` in TexParser.cpp, so the dumped bytes
// are byte-for-byte the buffer the in-app preview decodes.
//
// Pairing rule: source basename `*_textures.bnk` → header
// `*_texture_headers.bnk` (suffix substitution), with a fallback to
// `globals_texture_headers.bnk` for unpaired nested archives. The
// `gui_*` pair is handled by the same suffix rule because both
// names already end in `_textures.bnk` / `_texture_headers.bnk`.

static bool reconstruct_one_tex(
    BnkCacheEntry* body_ce,
    const std::string& body_bnk_path,
    int file_index,
    BnkCacheEntry* header_ce,
    const std::string& header_bnk_path,
    BnkCacheEntry* mip0_ce,
    const std::string& mip0_bnk_path,
    std::vector<unsigned char>& out)
{
    if (!body_ce || !body_ce->reader) return false;
    const auto& body_files = body_ce->reader->list_files();
    if (file_index < 0 || (size_t)file_index >= body_files.size())
        return false;

    auto tmpdir = std::filesystem::temp_directory_path() / "f2_tex_dump";
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

    // Look up the matching header by leaf filename (sibling BNKs
    // sometimes use slightly different folder hierarchies for the
    // same logical asset — match build_tex_buffer_for_name's rule).
    std::string leaf = std::filesystem::path(body_files[file_index].name)
                           .filename().string();
    std::transform(leaf.begin(), leaf.end(), leaf.begin(), ::tolower);

    std::vector<unsigned char> header_bytes;
    if (header_ce && header_ce->reader) {
        auto h_it = header_ce->by_leaf.find(leaf);
        if (h_it != header_ce->by_leaf.end()) {
            auto tmp_h = tmpdir /
                ("hdr_" + std::to_string(h_it->second) + ".bin");
            try {
                extract_one(header_bnk_path, h_it->second, tmp_h.string());
                header_bytes = read_all_bytes(tmp_h);
                std::filesystem::remove(tmp_h, ec);
            } catch (...) {
                std::filesystem::remove(tmp_h, ec);
                // Header extraction failed — keep going with body alone
                // rather than failing the whole entry.
            }
        }
    }

    // Optional mip0 — the global `1024mip0_textures.bnk` only carries
    // the largest mip for high-res textures. Most textures don't have
    // an entry there; absence is normal, not an error.
    std::vector<unsigned char> mip0_bytes;
    if (mip0_ce && mip0_ce->reader) {
        auto m_it = mip0_ce->by_leaf.find(leaf);
        if (m_it != mip0_ce->by_leaf.end()) {
            auto tmp_m = tmpdir /
                ("mip_" + std::to_string(m_it->second) + ".bin");
            try {
                extract_one(mip0_bnk_path, m_it->second, tmp_m.string());
                mip0_bytes = read_all_bytes(tmp_m);
                std::filesystem::remove(tmp_m, ec);
            } catch (...) {
                std::filesystem::remove(tmp_m, ec);
            }
        }
    }

    // Concat header + mip0 + body in the same order
    // build_tex_buffer_for_name uses. If header is missing we still
    // emit body alone — better an unpaired body than a failed entry.
    out.clear();
    out.reserve(header_bytes.size() + mip0_bytes.size() + body.size());
    out.insert(out.end(), header_bytes.begin(), header_bytes.end());
    out.insert(out.end(), mip0_bytes.begin(), mip0_bytes.end());
    out.insert(out.end(), body.begin(), body.end());
    return !out.empty();
}

void dump_tex_files() {
    if (S.all_tex_files.empty()) {
        OutputLog::warn("Dump TEX: no .tex files indexed (open a "
                        "Fable 2 root first).");
        return;
    }

    std::vector<FlatAssetEntry> targets = S.all_tex_files;

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
                    " .tex file(s) from " +
                    std::to_string(by_bnk.size()) + " BNK(s) → " +
                    export_root);
    progress_open(total, std::string("Dumping TEXs → ") + export_root);
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

        std::unordered_map<std::string, BnkCacheEntry> bnk_cache;

        // Resolve the global mip0 BNK once — it's shared across every
        // body BNK we visit. Empty path means "no mip0 BNK on disc"
        // which is normal for some asset roots / GUI-only mounts.
        std::string mip0_path;
        BnkCacheEntry* mip0_ce = nullptr;
        if (auto p = find_bnk_by_filename("1024mip0_textures.bnk")) {
            mip0_path = *p;
            mip0_ce = get_or_open_bnk(bnk_cache, mip0_path);
        }

        try {
            for (const auto& [bnk_path, indices] : by_bnk) {
                if (S.cancel_requested.load() || S.exiting.load()) break;

                BnkCacheEntry* body_ce = get_or_open_bnk(bnk_cache, bnk_path);
                if (!body_ce) {
                    OutputLog::error(std::string("TEX dump: cannot open ")
                                     + bnk_path);
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

                std::string header_path;
                BnkCacheEntry* header_ce = nullptr;
                if (auto p = derive_paired_texture_headers_bnk(bnk_path)) {
                    header_path = *p;
                    header_ce = get_or_open_bnk(bnk_cache, header_path);
                }

                for (int ti : indices) {
                    if (S.cancel_requested.load() || S.exiting.load()) break;
                    const auto& e = targets[(size_t)ti];

                    std::vector<unsigned char> buf;
                    bool ok = false;
                    try {
                        ok = reconstruct_one_tex(body_ce, bnk_path,
                                                 e.file_index,
                                                 header_ce, header_path,
                                                 mip0_ce, mip0_path,
                                                 buf);
                    } catch (const std::exception& ex) {
                        OutputLog::error(std::string("TEX exception on ") +
                                         e.full_path + ": " + ex.what());
                        ok = false;
                    } catch (...) {
                        OutputLog::error(std::string("TEX exception on ") +
                                         e.full_path);
                        ok = false;
                    }

                    if (ok && !buf.empty()) {
                        auto out = build_asset_out_path(e, ".tex");
                        ok = write_buf_to_disk(out, buf);
                        if (!ok) {
                            OutputLog::error(std::string("TEX write failed: ")
                                             + out.string());
                        }
                    } else {
                        OutputLog::error(std::string("TEX rebuild failed: ") +
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
            OutputLog::error(std::string("TEX dump worker aborted: ") +
                             ex.what());
            return;
        } catch (...) {
            OutputLog::error("TEX dump worker aborted (unknown exception).");
            return;
        }

        if (S.cancel_requested.load()) {
            OutputLog::warn(std::string("TEX dump cancelled (")
                          + std::to_string(done.load()) + "/"
                          + std::to_string(total) + " written).");
            S.cancel_requested = false;
            return;
        }

        const int n_failed = (int)failed.size();
        if (n_failed > 0) {
            OutputLog::warn("TEX dump finished: " +
                            std::to_string(done.load() - n_failed) + "/" +
                            std::to_string(total) + " written, " +
                            std::to_string(n_failed) + " failed.");
        } else {
            OutputLog::success("TEX dump complete: " +
                               std::to_string(total) + " files written.");
        }
    }).detach();
}

// ---------------------------------------------------------------------------
// TEX dump — decoded variant
// ---------------------------------------------------------------------------
// Walks every entry in S.all_tex_files and dispatches each to
// `tex_export_begin_named(fmt, …)` — the same per-file export the
// right-click "Export to" submenu uses. The whole batch runs on a
// worker thread with the standard progress modal so the UI stays
// responsive even on a 5000-texture root.
//
// Output paths land at `${S.export_dir}/<asset_path>.<ext>` with the
// extension swapped to match `fmt`. Routes TEX (raw) through
// `dump_tex_files()` because that's what raw means here — re-running
// the decoder for raw output would just discard the decode and write
// the same bytes the raw dump already produces.

void dump_tex_files_as(TexExportFormat fmt) {
    if (fmt == TexExportFormat::TEX) {
        dump_tex_files();
        return;
    }
    if (S.all_tex_files.empty()) {
        OutputLog::warn("Dump TEX as: no .tex files indexed (open a "
                        "Fable 2 root first).");
        return;
    }

    // Snapshot the flat list — the file-tree builder could in
    // principle rebuild during the dump and we'd otherwise iterate
    // over a moving target.
    std::vector<FlatAssetEntry> targets = S.all_tex_files;
    const int total = (int)targets.size();

    const std::string export_root =
        S.export_dir.empty() ? std::filesystem::absolute("extracted").string()
                             : S.export_dir;
    const char* fmt_name =
        fmt == TexExportFormat::PNG  ? "PNG"  :
        fmt == TexExportFormat::JPG  ? "JPG"  :
        fmt == TexExportFormat::TIFF ? "TIFF" :
        fmt == TexExportFormat::DDS  ? "DDS"  : "?";

    OutputLog::info(std::string("Exporting ") + std::to_string(total) +
                    " .tex file(s) as " + fmt_name + " → " +
                    export_root);
    progress_open(total,
                  std::string("Exporting TEXs as ") + fmt_name +
                  " → " + export_root);
    progress_update(0, total, "Starting...");

    std::thread([targets = std::move(targets), total, fmt, fmt_name]() {
        struct PG {
            ~PG() { progress_done(); }
        } pg;

        std::atomic<int> done{0};
        std::vector<std::string> failed;
        std::mutex fail_m;

        for (const auto& e : targets) {
            if (S.cancel_requested.load() || S.exiting.load()) break;
            try {
                // The named-export helper takes care of: build_export_path,
                // mkdir, build_any_tex_buffer_for_name + decode +
                // tex_export_rgba, log line. Mip 0 is the highest-
                // resolution mip per the .tex format convention, same
                // default the right-click menu uses.
                tex_export_begin_named(fmt, e.full_path, e.bnk_path,
                                       /*mip_index=*/0);
            } catch (const std::exception& ex) {
                std::lock_guard<std::mutex> lk(fail_m);
                failed.push_back(e.full_path);
                OutputLog::error(std::string("TEX export exception (") +
                                 e.full_path + "): " + ex.what());
            } catch (...) {
                std::lock_guard<std::mutex> lk(fail_m);
                failed.push_back(e.full_path);
                OutputLog::error(std::string("TEX export exception (") +
                                 e.full_path + ")");
            }
            int cur = ++done;
            progress_update(cur, total,
                            std::filesystem::path(e.name)
                                .filename().string());
        }

        if (S.cancel_requested.load()) {
            OutputLog::warn(std::string("TEX export cancelled (") +
                            std::to_string(done.load()) + "/" +
                            std::to_string(total) + ").");
            S.cancel_requested = false;
            return;
        }
        const int n_failed = (int)failed.size();
        if (n_failed > 0) {
            OutputLog::warn(std::string("TEX export finished as ") +
                            fmt_name + ": " +
                            std::to_string(done.load() - n_failed) +
                            "/" + std::to_string(total) + " written, " +
                            std::to_string(n_failed) + " failed.");
        } else {
            OutputLog::success(std::string("TEX export complete as ") +
                               fmt_name + ": " +
                               std::to_string(total) + " files written.");
        }
    }).detach();
}

// ---------------------------------------------------------------------------
// WAV dump
// ---------------------------------------------------------------------------
// Wavs aren't split across BNKs — each .wav is a complete file
// inside its source BNK. So this is the trivial case: open each BNK
// once, extract every indexed .wav, write it to the export tree. No
// reconstruction, no pairing, no mip0.

void dump_wav_files() {
    if (S.all_wav_files.empty()) {
        OutputLog::warn("Dump WAV: no .wav files indexed (open a "
                        "Fable 2 root first).");
        return;
    }

    std::vector<FlatAssetEntry> targets = S.all_wav_files;

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
                    " .wav file(s) from " +
                    std::to_string(by_bnk.size()) + " BNK(s) → " +
                    export_root);
    progress_open(total, std::string("Dumping WAVs → ") + export_root);
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

        // We don't need the leaf-index map for wavs (no pair lookup),
        // but reusing get_or_open_bnk keeps a single open per source
        // BNK and avoids any per-asset re-init cost.
        std::unordered_map<std::string, BnkCacheEntry> bnk_cache;

        try {
            for (const auto& [bnk_path, indices] : by_bnk) {
                if (S.cancel_requested.load() || S.exiting.load()) break;

                BnkCacheEntry* body_ce = get_or_open_bnk(bnk_cache, bnk_path);
                if (!body_ce) {
                    OutputLog::error(std::string("WAV dump: cannot open ")
                                     + bnk_path);
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

                for (int ti : indices) {
                    if (S.cancel_requested.load() || S.exiting.load()) break;
                    const auto& e = targets[(size_t)ti];

                    auto out = build_asset_out_path(e, ".wav");
                    bool ok = false;
                    try {
                        std::error_code ec;
                        if (auto parent = out.parent_path(); !parent.empty()) {
                            std::filesystem::create_directories(parent, ec);
                        }
                        // extract_one streams the file directly to disk,
                        // no intermediate buffer needed (wavs in retail
                        // can be tens of MB — going through a vector
                        // would double the working-set for nothing).
                        extract_one(bnk_path, e.file_index, out.string());
                        ok = std::filesystem::exists(out, ec) && !ec;
                    } catch (const std::exception& ex) {
                        OutputLog::error(std::string("WAV exception on ") +
                                         e.full_path + ": " + ex.what());
                    } catch (...) {
                        OutputLog::error(std::string("WAV exception on ") +
                                         e.full_path);
                    }

                    if (!ok) {
                        OutputLog::error(std::string("WAV write failed: ") +
                                         e.full_path);
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
            OutputLog::error(std::string("WAV dump worker aborted: ") +
                             ex.what());
            return;
        } catch (...) {
            OutputLog::error("WAV dump worker aborted (unknown exception).");
            return;
        }

        if (S.cancel_requested.load()) {
            OutputLog::warn(std::string("WAV dump cancelled (")
                          + std::to_string(done.load()) + "/"
                          + std::to_string(total) + " written).");
            S.cancel_requested = false;
            return;
        }

        const int n_failed = (int)failed.size();
        if (n_failed > 0) {
            OutputLog::warn("WAV dump finished: " +
                            std::to_string(done.load() - n_failed) + "/" +
                            std::to_string(total) + " written, " +
                            std::to_string(n_failed) + " failed.");
        } else {
            OutputLog::success("WAV dump complete: " +
                               std::to_string(total) + " files written.");
        }
    }).detach();
}

// ---------------------------------------------------------------------------
// WAV dump — decoded variant
// ---------------------------------------------------------------------------
// Same shape as dump_wav_files() but with a per-asset XMA decode
// step. WAV_RAW short-circuits to dump_wav_files (raw bytes, no
// decode round-trip). WAV_PCM extracts the XMA bytes, runs them
// through XmaDecoder::decode_xma_wav_file_to_pcm_wav, and overwrites
// the .wav with the PCM result — the same in-place trick
// `extract_file_one(..., convert_audio=true)` uses for single-file
// right-click exports.
//
// MP3 / AAC are scaffolded but stubbed: our libavcodec build
// (xma_codec_list.cpp) only links the XMA1/XMA2/WMAPro decoders.
// Wiring up the libavcodec aac/mp3 encoders means extending that
// codec list and bringing in the encoder symbols. Until that's
// done, those branches log a clear "not implemented" line so the
// user gets feedback rather than silent failures.

void dump_wav_files_as(AudioExportFormat fmt) {
    if (fmt == AudioExportFormat::WAV_RAW) {
        // Raw bytes path is exactly what dump_wav_files() already
        // does. Re-running the loop here would just duplicate code
        // and risk drifting behaviour.
        dump_wav_files();
        return;
    }
    // WAV_PCM, MP3, AAC all share the same outer loop: extract the
    // XMA bytes, decode to PCM, then dispatch to the format-specific
    // writer. Inline branches below pick between the WAV writer
    // (XmaDecoder built-in) and the MF-based MP3 / AAC encoders.

    if (S.all_wav_files.empty()) {
        OutputLog::warn("Dump WAV (PCM/MP3/AAC): no .wav files indexed "
                        "(open a Fable 2 root first).");
        return;
    }

    std::vector<FlatAssetEntry> targets = S.all_wav_files;

    std::unordered_map<std::string, std::vector<int>> by_bnk;
    by_bnk.reserve(64);
    for (size_t i = 0; i < targets.size(); ++i) {
        by_bnk[targets[i].bnk_path].push_back((int)i);
    }
    const int total = (int)targets.size();

    const std::string export_root =
        S.export_dir.empty() ? std::filesystem::absolute("extracted").string()
                             : S.export_dir;

    // Per-format display label and output extension. Drives the log
    // lines, the progress modal title, and the destination filename
    // suffix. AAC uses .m4a (MP4 container) — that's what
    // MFCreateSinkWriterFromURL recognises; raw `.aac` would yield
    // E_FAIL on writer creation.
    const char* fmt_label =
        (fmt == AudioExportFormat::WAV_PCM) ? "PCM"  :
        (fmt == AudioExportFormat::MP3)     ? "MP3"  :
        (fmt == AudioExportFormat::AAC)     ? "AAC"  : "?";
    const char* fmt_ext =
        (fmt == AudioExportFormat::WAV_PCM) ? ".wav" :
        (fmt == AudioExportFormat::MP3)     ? ".mp3" :
        (fmt == AudioExportFormat::AAC)     ? ".m4a" : ".bin";

    OutputLog::info(std::string("Exporting ") + std::to_string(total) +
                    " .wav file(s) as " + fmt_label + " → " + export_root);
    progress_open(total,
                  std::string("Exporting WAVs as ") + fmt_label +
                  " → " + export_root);
    progress_update(0, total, "Starting...");

    std::thread([targets = std::move(targets),
                 by_bnk = std::move(by_bnk),
                 total, fmt, fmt_label, fmt_ext]() {
        struct PG {
            ~PG() { progress_done(); }
        } pg;

        std::atomic<int> done{0};
        std::vector<std::string> failed;
        std::mutex fail_m;

        try {
            for (const auto& [bnk_path, indices] : by_bnk) {
                if (S.cancel_requested.load() || S.exiting.load()) break;

                for (int ti : indices) {
                    if (S.cancel_requested.load() || S.exiting.load()) break;
                    const auto& e = targets[(size_t)ti];

                    // Output path for the encoded result.
                    auto out_final = build_asset_out_path(e, fmt_ext);
                    // Scratch path for the raw XMA extract — we always
                    // need a temp file because XmaDecoder reads from
                    // disk. Use a sibling .raw.wav next to the final
                    // output so it lives on the same drive (matters
                    // for atomic-rename semantics on the WAV path).
                    auto out_scratch = out_final;
                    out_scratch += ".xma.tmp";

                    bool ok = false;
                    try {
                        std::error_code ec;
                        if (auto parent = out_final.parent_path();
                            !parent.empty()) {
                            std::filesystem::create_directories(parent, ec);
                        }

                        // Step 1 — extract the XMA-encoded bytes.
                        extract_one(bnk_path, e.file_index,
                                    out_scratch.string());
                        if (!std::filesystem::exists(out_scratch, ec) || ec) {
                            throw std::runtime_error(
                                "extract_one produced no file");
                        }

                        // Step 2 — format-specific encode/conversion.
                        if (fmt == AudioExportFormat::WAV_PCM) {
                            // PCM .wav: XmaDecoder handles file→file in
                            // one call. Decode-failure mode keeps the
                            // raw .wav on disk so the user still gets
                            // something usable.
                            auto raw = read_all_bytes(out_scratch);
                            std::filesystem::remove(out_scratch, ec);
                            if (raw.empty()) {
                                throw std::runtime_error(
                                    "extracted .wav is empty");
                            }
                            std::vector<uint8_t> src(raw.begin(), raw.end());
                            std::string err;
                            if (!XmaDecoder::decode_xma_wav_file_to_pcm_wav(
                                    src, out_final.string(), &err)) {
                                // Soft failure — write the raw bytes
                                // to the final path so we don't end
                                // up empty-handed.
                                std::ofstream f(out_final, std::ios::binary |
                                                          std::ios::trunc);
                                if (f) {
                                    f.write((const char*)raw.data(),
                                            (std::streamsize)raw.size());
                                }
                                OutputLog::warn(std::string(
                                    "PCM decode failed for ") + e.full_path +
                                    ": " + err + " — kept raw bytes.");
                            }
                            ok = true;
                        } else {
                            // MP3 / AAC: XMA → PCM (in-memory) → MF
                            // encode at out_final. Drops the scratch
                            // file regardless of outcome so we don't
                            // leave .xma.tmp turds around.
                            auto raw = read_all_bytes(out_scratch);
                            std::filesystem::remove(out_scratch, ec);
                            if (raw.empty()) {
                                throw std::runtime_error(
                                    "extracted .wav is empty");
                            }
                            std::vector<uint8_t> src(raw.begin(), raw.end());
                            std::vector<int16_t> pcm;
                            int sr = 0, ch = 0;
                            std::string err;
                            if (!XmaDecoder::decode_xma_to_pcm(
                                    src, pcm, sr, ch, &err) || pcm.empty()) {
                                throw std::runtime_error(
                                    std::string("XMA→PCM decode failed: ")
                                    + err);
                            }
                            bool encoded = false;
                            if (fmt == AudioExportFormat::MP3) {
                                encoded = MfAudio::encode_pcm_to_mp3(
                                    pcm, sr, ch, out_final.string(), &err);
                            } else {
                                encoded = MfAudio::encode_pcm_to_aac(
                                    pcm, sr, ch, out_final.string(), &err);
                            }
                            if (!encoded) {
                                throw std::runtime_error(
                                    std::string(fmt_label) +
                                    " encode failed: " + err);
                            }
                            ok = true;
                        }
                    } catch (const std::exception& ex) {
                        std::error_code rmec;
                        std::filesystem::remove(out_scratch, rmec);
                        OutputLog::error(std::string("WAV ") + fmt_label +
                                         " exception on " + e.full_path +
                                         ": " + ex.what());
                    } catch (...) {
                        std::error_code rmec;
                        std::filesystem::remove(out_scratch, rmec);
                        OutputLog::error(std::string("WAV ") + fmt_label +
                                         " exception on " + e.full_path);
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
            OutputLog::error(std::string("WAV ") + fmt_label +
                             " dump worker aborted: " + ex.what());
            return;
        } catch (...) {
            OutputLog::error(std::string("WAV ") + fmt_label +
                             " dump worker aborted (unknown).");
            return;
        }

        if (S.cancel_requested.load()) {
            OutputLog::warn(std::string("WAV ") + fmt_label +
                            " dump cancelled (" +
                            std::to_string(done.load()) + "/" +
                            std::to_string(total) + ").");
            S.cancel_requested = false;
            return;
        }
        const int n_failed = (int)failed.size();
        if (n_failed > 0) {
            OutputLog::warn(std::string("WAV ") + fmt_label +
                            " dump finished: " +
                            std::to_string(done.load() - n_failed) + "/" +
                            std::to_string(total) + " written, " +
                            std::to_string(n_failed) + " failed.");
        } else {
            OutputLog::success(std::string("WAV ") + fmt_label +
                               " dump complete: " +
                               std::to_string(total) + " files written.");
        }
    }).detach();
}

// ---------------------------------------------------------------------------
// Full per-BNK dump
// ---------------------------------------------------------------------------
// Extracts every file from every BNK the asset browser has indexed
// (top-level + nested). No reconstruction — paired bodies and headers
// land in separate per-BNK subdirectories so they don't overwrite
// each other on the way out. Useful for dev / inspection workflows
// where the user wants the raw archive contents, not a reassembled
// MDL/TEX. The MDL/TEX/WAV dumpers above are still the right call
// when the goal is decoder-ready files.
//
// Output layout: `${export_dir}/<bnk_stem>/<asset_path>`. The BNK
// stem is the source BNK's filename without the `.bnk` extension —
// `globals_models.bnk` → `globals_models/`,
// `globals_model_headers.bnk` → `globals_model_headers/`. That gives
// the user a clean side-by-side view of paired archives without
// having to unpack each one manually in another tool.

void dump_bnk_contents() {
    // Collect every BNK path the file-tree builder discovered. Includes
    // nested-archive temp paths so the contents inside region archives
    // get extracted too — the parent BNK's listing also dumps the
    // nested .bnk wrapper as a raw file, which is the right thing in
    // case the user wants to feed it to another tool.
    std::vector<std::string> all_bnks;
    all_bnks.reserve(S.bnk_paths.size() + S.nested_bnk_paths.size());
    all_bnks.insert(all_bnks.end(),
                    S.bnk_paths.begin(), S.bnk_paths.end());
    all_bnks.insert(all_bnks.end(),
                    S.nested_bnk_paths.begin(), S.nested_bnk_paths.end());

    if (all_bnks.empty()) {
        OutputLog::warn("Dump BNK contents: no BNKs indexed (open a "
                        "Fable 2 root first).");
        return;
    }

    const std::string export_root =
        S.export_dir.empty() ? std::filesystem::absolute("extracted").string()
                             : S.export_dir;

    OutputLog::info(std::string("Dumping every file in ") +
                    std::to_string(all_bnks.size()) + " BNK(s) → " +
                    export_root);
    // We don't know the total file count yet — opening every BNK on
    // the UI thread to count would stall the click for a couple
    // seconds. Open the progress modal with an estimate and bump
    // the total inside the worker once we've indexed.
    progress_open(0, std::string("Dumping BNK contents → ") + export_root);
    progress_update(0, 0, "Indexing...");

    std::thread([all_bnks = std::move(all_bnks), export_root]() {
        struct DumpGuard {
            ~DumpGuard() { progress_done(); }
        } pg;

        // First pass — open each BNK, materialise nested temps, count
        // its files. We keep the readers cached afterward so the
        // second-pass extract loop doesn't re-open and re-parse each
        // archive.
        std::unordered_map<std::string, BnkCacheEntry> bnk_cache;
        int total = 0;
        for (const auto& bp : all_bnks) {
            if (S.cancel_requested.load() || S.exiting.load()) break;
            auto* ce = get_or_open_bnk(bnk_cache, bp);
            if (!ce) {
                OutputLog::error(std::string(
                    "BNK contents dump: cannot open ") + bp);
                continue;
            }
            total += (int)ce->reader->list_files().size();
        }
        if (total <= 0) {
            OutputLog::warn("BNK contents dump: every BNK was empty / "
                            "unreadable.");
            return;
        }

        // Resize the progress modal's bounds to match what we found.
        // progress_update calls below all carry the same `total` so
        // the bar fills correctly.
        progress_update(0, total, "Starting...");

        std::atomic<int> done{0};
        std::vector<std::string> failed;
        std::mutex fail_m;

        try {
            for (const auto& bp : all_bnks) {
                if (S.cancel_requested.load() || S.exiting.load()) break;
                auto it = bnk_cache.find(bp);
                if (it == bnk_cache.end() || !it->second.reader) continue;
                auto& ce = it->second;
                const auto& files = ce.reader->list_files();

                // Per-BNK output subdirectory: drop the `.bnk` extension
                // and use the stem so paired bodies / headers don't
                // collide. Some nested temp paths have an additional
                // hash-tag prefix the materialiser appended; the stem
                // of those is still distinct enough to keep contents
                // separate.
                std::string stem = std::filesystem::path(bp)
                                       .stem().string();
                std::filesystem::path bnk_root =
                    std::filesystem::path(export_root) / stem;

                for (size_t i = 0; i < files.size(); ++i) {
                    if (S.cancel_requested.load() || S.exiting.load()) break;
                    const auto& fe = files[i];

                    // Sanitise leading slashes the same way build_out_path
                    // does — without this the `/` joins below would
                    // root-anchor the path on Windows.
                    std::string rel = fe.name;
                    while (!rel.empty() &&
                           (rel.front() == '/' || rel.front() == '\\'))
                        rel.erase(rel.begin());
                    auto out = bnk_root / rel;

                    bool ok = false;
                    try {
                        std::error_code ec;
                        if (auto parent = out.parent_path(); !parent.empty()) {
                            std::filesystem::create_directories(parent, ec);
                        }
                        // extract_one streams straight to disk; for big
                        // wavs / textures this avoids a multi-MB
                        // intermediate buffer.
                        extract_one(bp, (int)i, out.string());
                        ok = std::filesystem::exists(out, ec) && !ec;
                    } catch (const std::exception& ex) {
                        OutputLog::error(std::string(
                            "BNK contents exception on ") + bp + " :: " +
                            fe.name + ": " + ex.what());
                    } catch (...) {
                        OutputLog::error(std::string(
                            "BNK contents exception on ") + bp + " :: " +
                            fe.name);
                    }

                    if (!ok) {
                        std::lock_guard<std::mutex> lk(fail_m);
                        failed.push_back(bp + " :: " + fe.name);
                    }

                    int cur = ++done;
                    progress_update(cur, total,
                                    std::filesystem::path(fe.name)
                                        .filename().string());
                }
            }
        } catch (const std::exception& ex) {
            OutputLog::error(std::string(
                "BNK contents dump worker aborted: ") + ex.what());
            return;
        } catch (...) {
            OutputLog::error(
                "BNK contents dump worker aborted (unknown exception).");
            return;
        }

        if (S.cancel_requested.load()) {
            OutputLog::warn(std::string("BNK contents dump cancelled (")
                          + std::to_string(done.load()) + "/"
                          + std::to_string(total) + " written).");
            S.cancel_requested = false;
            return;
        }

        const int n_failed = (int)failed.size();
        if (n_failed > 0) {
            OutputLog::warn("BNK contents dump finished: " +
                            std::to_string(done.load() - n_failed) + "/" +
                            std::to_string(total) + " written, " +
                            std::to_string(n_failed) + " failed.");
        } else {
            OutputLog::success("BNK contents dump complete: " +
                               std::to_string(total) +
                               " file(s) written.");
        }
    }).detach();
}

} // namespace ISO
