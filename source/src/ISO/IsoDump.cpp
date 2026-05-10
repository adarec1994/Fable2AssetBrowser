#include "IsoDump.h"

#include "IsoMount.h"
#include "../Utilities/State.h"
#include "../Utilities/Progress.h"
#include "../UI/OutputLog.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
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

} // namespace ISO
