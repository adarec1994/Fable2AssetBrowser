#include "Progress.h"
#include "State.h"
#include <mutex>

void progress_open(int total, const std::string &title) {
    std::lock_guard<std::mutex> lk(S.progress_mutex);
    S.cancel_requested = false;
    S.progress_total = total;
    S.progress_current = 0;
    S.progress_label = title;
    S.show_progress.store(true);
}

void progress_update(int current, int total, const std::string &fname) {
    std::lock_guard<std::mutex> lk(S.progress_mutex);
    S.progress_current = current;
    S.progress_total = total;
    S.progress_label = fname;
}

void progress_done() {
    std::lock_guard<std::mutex> lk(S.progress_mutex);
    S.show_progress.store(false);
    S.progress_total = 0;
    S.progress_current = 0;
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