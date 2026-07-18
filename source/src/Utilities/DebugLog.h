#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>

namespace DebugLog {

bool Enabled() noexcept;
bool SetEnabled(bool enabled, std::string& error) noexcept;
std::filesystem::path CurrentPath();
void Write(std::string_view category, std::string_view message) noexcept;

class Scope {
public:
    explicit Scope(std::string_view operation) noexcept;
    Scope(std::string_view operation, std::string_view detail) noexcept;
    ~Scope();

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

    void Result(std::string_view result) noexcept;

private:
    bool active_ = false;
    std::string operation_;
    std::string result_;
    std::chrono::steady_clock::time_point started_;
};

}
