#include "Progress.h"
#include "State.h"
#include "DebugTrace.h"
#include <chrono>
#include <fstream>
#include <mutex>



namespace {
std::string g_stage_label;
std::chrono::steady_clock::time_point g_stage_start;

void note_stage(const std::string& label) {
    const auto now = std::chrono::steady_clock::now();
    if (!g_stage_label.empty() && label != g_stage_label) {
        const long long ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now - g_stage_start)
                .count();
        if (ms >= 20) {
            const std::string& base = DebugTrace::log_path();
            const size_t slash = base.find_last_of("\\/");
            const std::string dir =
                slash == std::string::npos ? std::string()
                                           : base.substr(0, slash + 1);
            std::ofstream f(dir + "load_stages.log", std::ios::app);
            if (f) {
                f << g_stage_label << "\t" << ms << " ms\n";
            }
        }
    }
    if (label != g_stage_label) {
        g_stage_label = label;
        g_stage_start = now;
    }
}
}

void progress_open(int total, const std::string &title) {
    std::lock_guard<std::mutex> lk(S.progress_mutex);
    S.cancel_requested = false;
    S.progress_total = total;
    S.progress_current = 0;
    S.progress_label = title;
    S.show_progress.store(true);
    note_stage(title);
}

void progress_update(int current, int total, const std::string &fname) {
    std::lock_guard<std::mutex> lk(S.progress_mutex);
    S.progress_current = current;
    S.progress_total = total;
    S.progress_label = fname;
    note_stage(fname);
}

void progress_done() {
    std::lock_guard<std::mutex> lk(S.progress_mutex);
    S.show_progress.store(false);
    S.progress_total = 0;
    S.progress_current = 0;
    note_stage("(done)");
    S.progress_label.clear();
}

void show_error_box(const std::string &msg) {
    S.error_text = msg;
    S.show_error = true;
}

void show_completion_box(const std::string &msg) {
    S.completion_text = msg;
    S.show_completion = true;
}
