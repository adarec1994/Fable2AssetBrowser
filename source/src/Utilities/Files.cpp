#include "Files.h"
#include "../ISO/IsoMount.h"
#include <fstream>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

std::string resolve_asset_path(const std::string& rel) {
    if (std::filesystem::exists(rel)) return rel;

    std::string exe_dir;
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        std::string p(buf, buf + n);
        size_t slash = p.find_last_of("\\/");
        if (slash != std::string::npos) exe_dir = p.substr(0, slash);
    }
#else
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        std::string p(buf);
        size_t slash = p.find_last_of('/');
        if (slash != std::string::npos) exe_dir = p.substr(0, slash);
    }
#endif

    if (!exe_dir.empty()) {
        for (const char* prefix : {"/", "/../", "/../source/"}) {
            std::string p = exe_dir + prefix + rel;
            if (std::filesystem::exists(p)) return p;
        }
    }
    return rel;
}

std::string load_last_dir() {
    std::ifstream f("last_dir.txt");
    std::string s;
    if (f) std::getline(f, s);
    return s;
}

void save_last_dir(const std::string &p) {
    std::ofstream f("last_dir.txt", std::ios::trunc);
    if (f) f << p;
}

static bool ci_path_less(const std::string& a, const std::string& b) {
    std::string la = a, lb = b;
    std::transform(la.begin(), la.end(), la.begin(), ::tolower);
    std::transform(lb.begin(), lb.end(), lb.begin(), ::tolower);
    return la < lb;
}

std::vector<std::string> scan_bnks_recursive(const std::string &root) {
    std::vector<std::string> out;

    if (ISO::IsoMount::instance().is_mounted()) {
        for (const auto& mf : ISO::IsoMount::instance().list_recursive(".bnk")) {
            out.push_back(ISO::IsoMount::make_iso_path(mf.path));
        }
        std::sort(out.begin(), out.end(), ci_path_less);
        return out;
    }

    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(
             root, std::filesystem::directory_options::skip_permission_denied, ec);
         it != std::filesystem::recursive_directory_iterator(); ++it) {
        if (it->is_regular_file(ec)) {
            auto p = it->path();
            auto ext = p.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".bnk") out.push_back(p.string());
        }
    }
    std::sort(out.begin(), out.end(), ci_path_less);
    return out;
}

std::vector<std::string> scan_adbs_recursive(const std::string &root) {
    std::vector<std::string> out;
    if (ISO::IsoMount::instance().is_mounted()) {
        for (const auto& mf : ISO::IsoMount::instance().list_recursive(".adb")) {
            out.push_back(ISO::IsoMount::make_iso_path(mf.path));
        }
        std::sort(out.begin(), out.end(), ci_path_less);
        return out;
    }
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(
             root, std::filesystem::directory_options::skip_permission_denied, ec);
         it != std::filesystem::recursive_directory_iterator(); ++it) {
        if (it->is_regular_file(ec)) {
            auto p = it->path();
            auto ext = p.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".adb") out.push_back(p.string());
        }
    }
    std::sort(out.begin(), out.end(), ci_path_less);
    return out;
}

std::vector<std::string> scan_luas_recursive(const std::string &root) {
    std::vector<std::string> out;
    if (ISO::IsoMount::instance().is_mounted()) {
        for (const auto& mf : ISO::IsoMount::instance().list_recursive(".lua")) {
            out.push_back(ISO::IsoMount::make_iso_path(mf.path));
        }
        std::sort(out.begin(), out.end(), ci_path_less);
        return out;
    }
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(
             root, std::filesystem::directory_options::skip_permission_denied, ec);
         it != std::filesystem::recursive_directory_iterator(); ++it) {
        if (it->is_regular_file(ec)) {
            auto p = it->path();
            auto ext = p.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".lua") out.push_back(p.string());
        }
    }
    std::sort(out.begin(), out.end(), ci_path_less);
    return out;
}

std::vector<unsigned char> read_all_bytes(const std::filesystem::path &p) {
    std::vector<unsigned char> v;

    std::string spath = p.string();
    if (ISO::IsoMount::is_iso_path(spath)) {
        auto bytes = ISO::IsoMount::instance().read_file(
            ISO::IsoMount::strip_iso_prefix(spath));
        v.assign(bytes.begin(), bytes.end());
        return v;
    }
    std::error_code ec;
    auto sz = std::filesystem::file_size(p, ec);
    if (ec) return v;
    v.resize((size_t) sz);
    std::ifstream f(p, std::ios::binary);
    f.read((char *) v.data(), (std::streamsize) sz);
    return v;
}

bool rd32be(const std::vector<unsigned char> &d, size_t o, uint32_t &v) {
    if (o + 4 > d.size()) return false;
    v = (uint32_t(d[o]) << 24) | (uint32_t(d[o + 1]) << 16) | (uint32_t(d[o + 2]) << 8) | uint32_t(d[o + 3]);
    return true;
}

bool rd16be(const std::vector<unsigned char> &d, size_t o, uint16_t &v) {
    if (o + 2 > d.size()) return false;
    v = (uint16_t(d[o]) << 8) | uint16_t(d[o + 1]);
    return true;
}
