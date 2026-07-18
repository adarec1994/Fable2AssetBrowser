#include "DebugLog.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <mutex>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

namespace DebugLog {
namespace {

struct LogState {
    std::atomic<bool> enabled{false};
    std::mutex mutex;
    std::ofstream stream;
    std::filesystem::path path;
    std::chrono::steady_clock::time_point started;
};

LogState& state() {
    static LogState value;
    return value;
}

std::filesystem::path executable_directory() {
#ifdef _WIN32
    wchar_t buffer[32768] = {};
    const DWORD length = GetModuleFileNameW(
        nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    if (length > 0 && length < std::size(buffer)) {
        return std::filesystem::path(
            std::wstring(buffer, buffer + length)).parent_path();
    }
#endif
    std::error_code error;
    const std::filesystem::path current =
        std::filesystem::current_path(error);
    return error ? std::filesystem::path(".") : current;
}

std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    const std::time_t value = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &value);
#else
    localtime_r(&value, &local);
#endif
    std::ostringstream output;
    output << std::put_time(&local, "%Y-%m-%d_%H-%M-%S")
           << '_' << std::setw(3) << std::setfill('0') << millis.count();
    return output.str();
}

std::string display_time() {
    const auto now = std::chrono::system_clock::now();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    const std::time_t value = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &value);
#else
    localtime_r(&value, &local);
#endif
    std::ostringstream output;
    output << std::put_time(&local, "%Y-%m-%d %H:%M:%S")
           << '.' << std::setw(3) << std::setfill('0') << millis.count();
    return output.str();
}

void write_line(LogState& log, std::string_view category,
                std::string_view message, unsigned depth) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - log.started);
#ifdef _WIN32
    const unsigned long thread = GetCurrentThreadId();
#else
    const std::size_t thread =
        std::hash<std::thread::id>{}(std::this_thread::get_id());
#endif
    log.stream << '[' << std::setw(12) << elapsed.count() << " us]"
               << " [T" << thread << "] "
               << std::string(depth * 2, ' ')
               << '[' << category << "] " << message << '\n';
    log.stream.flush();
}

thread_local unsigned operation_depth = 0;

}

bool Enabled() noexcept {
    return state().enabled.load(std::memory_order_acquire);
}

bool SetEnabled(bool enabled, std::string& error) noexcept {
    error.clear();
    LogState& log = state();
    try {
        if (!enabled) {
            log.enabled.store(false, std::memory_order_release);
            std::lock_guard<std::mutex> lock(log.mutex);
            if (log.stream) {
                write_line(log, "SESSION", "logging disabled", 0);
                log.stream.close();
            }
            log.path.clear();
            return true;
        }
        if (log.enabled.load(std::memory_order_acquire)) return true;

        std::lock_guard<std::mutex> lock(log.mutex);
        const std::filesystem::path directory =
            executable_directory() / "debug_logs";
        std::error_code filesystem_error;
        std::filesystem::create_directories(directory, filesystem_error);
        if (filesystem_error) {
            error = "Could not create debug log folder: " +
                    filesystem_error.message();
            return false;
        }
        log.path = directory /
            ("Fable2AssetBrowser_Debug_" + timestamp() + ".txt");
        log.stream.open(log.path, std::ios::out | std::ios::trunc);
        if (!log.stream) {
            error = "Could not create debug log: " + log.path.string();
            log.path.clear();
            return false;
        }
        log.started = std::chrono::steady_clock::now();
        log.stream << "Fable 2 Asset Browser debug log\n"
                   << "Started: " << display_time() << '\n'
                   << "File: " << log.path.string() << "\n\n";
        log.stream.flush();
        log.enabled.store(true, std::memory_order_release);
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
    } catch (...) {
        error = "Could not change debug logging state.";
    }
    log.enabled.store(false, std::memory_order_release);
    return false;
}

std::filesystem::path CurrentPath() {
    LogState& log = state();
    std::lock_guard<std::mutex> lock(log.mutex);
    return log.path;
}

void Write(std::string_view category, std::string_view message) noexcept {
    LogState& log = state();
    if (!log.enabled.load(std::memory_order_acquire)) return;
    try {
        std::lock_guard<std::mutex> lock(log.mutex);
        if (log.enabled.load(std::memory_order_relaxed) && log.stream) {
            write_line(log, category, message, operation_depth);
        }
    } catch (...) {
    }
}

Scope::Scope(std::string_view operation) noexcept
    : Scope(operation, {}) {
}

Scope::Scope(std::string_view operation, std::string_view detail) noexcept {
    if (!Enabled()) return;
    try {
        operation_.assign(operation);
        started_ = std::chrono::steady_clock::now();
        active_ = true;
        std::string message = operation_;
        if (!detail.empty()) {
            message.append(" | ");
            message.append(detail);
        }
        Write("BEGIN", message);
        ++operation_depth;
    } catch (...) {
        active_ = false;
    }
}

Scope::~Scope() {
    if (!active_) return;
    if (operation_depth > 0) --operation_depth;
    try {
        const auto elapsed = std::chrono::duration_cast<
            std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started_);
        std::string message = operation_ + " | " +
            std::to_string(elapsed.count()) + " ms";
        if (!result_.empty()) {
            message.append(" | ");
            message.append(result_);
        }
        Write("END", message);
    } catch (...) {
    }
}

void Scope::Result(std::string_view result) noexcept {
    if (!active_) return;
    try {
        result_.assign(result);
    } catch (...) {
    }
}

}
