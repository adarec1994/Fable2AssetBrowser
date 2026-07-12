#include "IsoDump.h"

#include "IsoMount.h"
#include "../Utilities/State.h"
#include "../Utilities/Progress.h"
#include "../Utilities/Utils.h"
#include "../Utilities/Files.h"
#include "../UI/OutputLog.h"
#include "../UI/Panels/PanelInternal.h"
#include "../MDL/ModelParser.h"
#include "../MDL/mdl_converter.h"
#include "../MDL/MdlFbxExport.h"
#include "../Audio/XmaDecoder.h"
#include "../Audio/MfAudioEncoder.h"
#include "../BNKCore.cpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
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
#include <unordered_set>
#include <vector>

namespace ISO {

namespace {

constexpr size_t kChunkBytes = 4 * 1024 * 1024;

std::filesystem::path build_out_path(const std::string& virtual_path) {
    std::string rel = virtual_path;
    while (!rel.empty() && (rel.front() == '/' || rel.front() == '\\'))
        rel.erase(rel.begin());
    std::filesystem::path root =
        S.export_dir.empty() ? std::filesystem::path("extracted")
                             : std::filesystem::path(S.export_dir);
    return root / rel;
}

bool stream_copy_one(int , int ,
                     const MountedFile& mf,
                     const std::filesystem::path& out,
                     std::vector<uint8_t>& buf) {
    std::error_code ec;
    if (auto parent = out.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) return false;
    }

    std::ofstream f(out, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    if (mf.size == 0) return f.good();

    uint64_t remaining = mf.size;
    uint64_t offset    = 0;
    while (remaining > 0) {
        size_t n = (remaining < (uint64_t)kChunkBytes)
                       ? (size_t)remaining
                       : kChunkBytes;
        if (!IsoMount::instance().read_at(mf.path, offset, buf.data(), n)) {
            return false;
        }
        f.write(reinterpret_cast<const char*>(buf.data()),
                (std::streamsize)n);
        if (!f.good()) return false;
        offset    += n;
        remaining -= n;
    }
    return f.good();
}

}

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

    OutputLog::info(std::string("Dumping ") + std::to_string(total) +
                    " BNK(s) → " + export_root);

    progress_open(total, std::string("Dumping BNKs → ") + export_root);
    progress_update(0, total, "Starting...");

    std::thread([targets = std::move(targets), total]() {
        struct DumpGuard {
            ~DumpGuard() { progress_done(); }
        } pg;

        std::atomic<int> done{0};
        std::vector<std::string> failed;
        std::mutex fail_m;

        std::vector<uint8_t> buf;
        try {
            buf.resize(kChunkBytes);
        } catch (const std::exception& e) {
            OutputLog::error(std::string("Dump: cannot allocate I/O "
                                         "buffer: ") + e.what());
            return;
        }

        try {
            int idx = 0;
            for (const auto& mf : targets) {
                ++idx;
                if (S.cancel_requested.load() || S.exiting.load()) {
                    break;
                }

                const auto out = build_out_path(mf.path);
                bool ok = false;
                try {
                    ok = stream_copy_one(idx, total, mf, out, buf);
                } catch (const std::exception& e) {
                    OutputLog::error(std::string("Dump exception on ") +
                                     mf.path + ": " + e.what());
                } catch (...) {
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
            OutputLog::error(std::string("Dump worker aborted: ") + e.what());
            return;
        } catch (...) {
            OutputLog::error("Dump worker aborted (unknown exception).");
            return;
        }

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

static bool ends_with_ci(const std::string& s, const char* suffix) {
    const size_t n = std::strlen(suffix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        unsigned char a = static_cast<unsigned char>(s[s.size() - n + i]);
        unsigned char b = static_cast<unsigned char>(suffix[i]);
        if (std::tolower(a) != std::tolower(b)) return false;
    }
    return true;
}

static std::string nested_asset_prefix_for_bnk(const std::string& bnk_path) {
    auto it = S.nested_bnk_virtual_paths.find(bnk_path);
    if (it == S.nested_bnk_virtual_paths.end()) return {};

    std::string nested_path = it->second;
    std::replace(nested_path.begin(), nested_path.end(), '\\', '/');
    const size_t slash = nested_path.find_last_of('/');
    if (slash == std::string::npos) return {};
    return nested_path.substr(0, slash + 1);
}

struct BnkCacheEntry {
    std::unique_ptr<BNKReader> reader;

    std::unordered_map<std::string, int> by_leaf;
};

static BnkCacheEntry* get_or_open_bnk(
    std::unordered_map<std::string, BnkCacheEntry>& cache,
    const std::string& bnk_path)
{
    auto& ce = cache[bnk_path];
    if (ce.reader) return &ce;
    try {

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

        cache.erase(bnk_path);
        return nullptr;
    }
}

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

    if (globals_fallback) {
        if (auto p = find_bnk_by_filename(globals_fallback)) return p;
    }
    return std::nullopt;
}

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

    return derive_paired_bnk(body_bnk_path, "_textures.bnk",
                             "_texture_headers.bnk",
                             "globals_texture_headers.bnk");
}

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

    std::string leaf = std::filesystem::path(body_files[file_index].name)
                           .filename().string();
    std::transform(leaf.begin(), leaf.end(), leaf.begin(), ::tolower);
    auto h_it = header_ce->by_leaf.find(leaf);
    if (h_it == header_ce->by_leaf.end()) {

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

    std::vector<FlatAssetEntry> targets = S.all_mdl_files;

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

        std::unordered_map<std::string, BnkCacheEntry> bnk_cache;

        try {
            for (const auto& [bnk_path, indices] : by_bnk) {
                if (S.cancel_requested.load() || S.exiting.load()) break;

                BnkCacheEntry* body_ce = get_or_open_bnk(bnk_cache, bnk_path);
                if (!body_ce) {
                    OutputLog::error(std::string("MDL dump: cannot open ")
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
                if (auto p = derive_paired_model_headers_bnk(bnk_path)) {
                    header_path = *p;
                    header_ce = get_or_open_bnk(bnk_cache, header_path);

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

namespace {

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
        if (!p_headers) return body;

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

}

void mdl_export_begin_named(MdlExportFormat fmt,
                            const std::string& bnk_path,
                            int file_index,
                            const std::string& display_path,
                            bool )
{
    if (bnk_path.empty() || file_index < 0) {
        OutputLog::error("MDL export: missing bnk / index arg.");
        return;
    }

    std::string entry_name;
    try {
        BNKReader r(bnk_path);
        const auto& files = r.list_files();
        if ((size_t)file_index < files.size()) {
            entry_name = files[file_index].name;
        }
    } catch (...) {  }

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

            }
        }
    }

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

                tex_export_begin_named(fmt, e.full_path, e.bnk_path,
                                       0);
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

void dump_wav_files_as(AudioExportFormat fmt) {
    if (fmt == AudioExportFormat::WAV_RAW) {

        dump_wav_files();
        return;
    }

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

                    auto out_final = build_asset_out_path(e, fmt_ext);

                    auto out_scratch = out_final;
                    out_scratch += ".xma.tmp";

                    bool ok = false;
                    try {
                        std::error_code ec;
                        if (auto parent = out_final.parent_path();
                            !parent.empty()) {
                            std::filesystem::create_directories(parent, ec);
                        }

                        extract_one(bnk_path, e.file_index,
                                    out_scratch.string());
                        if (!std::filesystem::exists(out_scratch, ec) || ec) {
                            throw std::runtime_error(
                                "extract_one produced no file");
                        }

                        if (fmt == AudioExportFormat::WAV_PCM) {

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

void dump_bnk_contents() {

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

    progress_open(0, std::string("Dumping BNK contents → ") + export_root);
    progress_update(0, 0, "Indexing...");

    std::thread([all_bnks = std::move(all_bnks), export_root]() {
        struct DumpGuard {
            ~DumpGuard() { progress_done(); }
        } pg;

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

                std::string stem = std::filesystem::path(bp)
                                       .stem().string();
                std::filesystem::path bnk_root =
                    std::filesystem::path(export_root) / stem;

                for (size_t i = 0; i < files.size(); ++i) {
                    if (S.cancel_requested.load() || S.exiting.load()) break;
                    const auto& fe = files[i];

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

void dump_gdb_save_files() {
    std::vector<std::string> all_bnks;
    all_bnks.reserve(S.bnk_paths.size() + S.nested_bnk_paths.size());
    all_bnks.insert(all_bnks.end(),
                    S.bnk_paths.begin(), S.bnk_paths.end());
    all_bnks.insert(all_bnks.end(),
                    S.nested_bnk_paths.begin(), S.nested_bnk_paths.end());

    if (all_bnks.empty()) {
        OutputLog::warn("Dump GDB/SAVE: no BNKs indexed (open a Fable 2 root first).");
        return;
    }

    const std::string export_root =
        S.export_dir.empty() ? std::filesystem::absolute("extracted").string()
                             : S.export_dir;

    OutputLog::info("Dumping every .gdb and .save with virtual paths -> " +
                    export_root);
    progress_open(0, "Dumping GDB/SAVE -> " + export_root);
    progress_update(0, 0, "Indexing...");

    std::thread([all_bnks = std::move(all_bnks), export_root]() {
        struct DumpGuard {
            ~DumpGuard() { progress_done(); }
        } pg;

        struct Target {
            std::string bnk_path;
            int         file_index = -1;
            std::string virtual_path;
            uint32_t    size = 0;
        };

        std::vector<Target> targets;
        for (const auto& bp : all_bnks) {
            if (S.cancel_requested.load() || S.exiting.load()) break;
            try {
                const auto ce = ::BnkCache::get(bp);
                const auto& files = ce.reader->list_files();
                const std::string nested_prefix =
                    nested_asset_prefix_for_bnk(bp);

                for (size_t i = 0; i < files.size(); ++i) {
                    std::string rel = nested_prefix + files[i].name;
                    while (!rel.empty() &&
                           (rel.front() == '/' || rel.front() == '\\')) {
                        rel.erase(rel.begin());
                    }

                    if (!ends_with_ci(rel, ".gdb") &&
                        !ends_with_ci(rel, ".save")) {
                        continue;
                    }

                    targets.push_back(Target{
                        bp, static_cast<int>(i), std::move(rel),
                        files[i].uncompressed_size
                    });
                }
            } catch (const std::exception& ex) {
                OutputLog::error("Dump GDB/SAVE: cannot index " + bp +
                                 ": " + ex.what());
            } catch (...) {
                OutputLog::error("Dump GDB/SAVE: cannot index " + bp);
            }
        }

        if (targets.empty()) {
            OutputLog::warn("Dump GDB/SAVE: no .gdb or .save files found.");
            return;
        }

        progress_update(0, static_cast<int>(targets.size()), "Starting...");

        const std::filesystem::path root(export_root);
        {
            std::error_code ec;
            std::filesystem::create_directories(root, ec);
        }

        std::ofstream manifest(root / "gdb_save_manifest.tsv",
                               std::ios::binary | std::ios::trunc);
        if (manifest) {
            manifest << "virtual_path\tbytes\tbnk_path\tfile_index\toutput_path\n";
        }

        std::unordered_set<std::string> seen_virtual_paths;
        seen_virtual_paths.reserve(targets.size() * 2);

        std::vector<std::string> failed;
        int written = 0;
        int duplicate_count = 0;

        for (size_t ti = 0; ti < targets.size(); ++ti) {
            if (S.cancel_requested.load() || S.exiting.load()) break;
            const Target& t = targets[ti];

            std::string key = t.virtual_path;
            std::replace(key.begin(), key.end(), '\\', '/');
            std::transform(key.begin(), key.end(), key.begin(),
                           [](unsigned char c) {
                               return static_cast<char>(std::tolower(c));
                           });

            std::filesystem::path out = root / t.virtual_path;
            if (!seen_virtual_paths.insert(key).second) {
                ++duplicate_count;
                const std::string bnk_stem =
                    std::filesystem::path(t.bnk_path).stem().string();
                out = root / "_gdb_save_duplicates" / bnk_stem /
                      t.virtual_path;
            }

            bool ok = false;
            size_t byte_count = t.size;
            try {
                auto bytes = ::BnkCache::extract_bytes(t.bnk_path,
                                                       t.file_index);
                byte_count = bytes.size();
                ok = write_buf_to_disk(out, bytes);
            } catch (const std::exception& ex) {
                OutputLog::error("Dump GDB/SAVE exception on " +
                                 t.virtual_path + ": " + ex.what());
            } catch (...) {
                OutputLog::error("Dump GDB/SAVE exception on " +
                                 t.virtual_path);
            }

            if (manifest) {
                manifest << t.virtual_path << '\t'
                         << byte_count << '\t'
                         << t.bnk_path << '\t'
                         << t.file_index << '\t'
                         << out.string() << '\n';
            }

            if (ok) {
                ++written;
            } else {
                failed.push_back(t.virtual_path);
            }

            progress_update(static_cast<int>(ti + 1),
                            static_cast<int>(targets.size()),
                            std::filesystem::path(t.virtual_path)
                                .filename().string());
        }

        if (S.cancel_requested.load()) {
            OutputLog::warn("Dump GDB/SAVE cancelled (" +
                            std::to_string(written) + "/" +
                            std::to_string(targets.size()) + " written).");
            S.cancel_requested = false;
            return;
        }

        if (!failed.empty()) {
            OutputLog::warn("Dump GDB/SAVE finished: " +
                            std::to_string(written) + "/" +
                            std::to_string(targets.size()) +
                            " written, " +
                            std::to_string(failed.size()) + " failed.");
        } else {
            OutputLog::success("Dump GDB/SAVE complete: " +
                               std::to_string(written) +
                               " file(s) written.");
        }

        if (duplicate_count > 0) {
            OutputLog::warn("Dump GDB/SAVE: " +
                            std::to_string(duplicate_count) +
                            " duplicate virtual path(s) written under "
                            "_gdb_save_duplicates.");
        }
    }).detach();
}

}
