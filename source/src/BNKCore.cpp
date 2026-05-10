#ifndef BNKCORE_CPP_INCLUDED
#define BNKCORE_CPP_INCLUDED

#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <mutex>
#include <stdexcept>
#include <optional>
#include <unordered_map>
#include "BNKReader.cpp"
#include "ISO/IsoMount.h"

namespace LazyNested {

struct PendingExtract {
    std::string parent_bnk_path;
    int         nested_index;
};

inline std::mutex& mutex() {
    static std::mutex m;
    return m;
}
inline std::unordered_map<std::string, PendingExtract>& table() {
    static std::unordered_map<std::string, PendingExtract> t;
    return t;
}

inline void register_pending(const std::string& temp_path,
                             const std::string& parent_bnk_path,
                             int nested_index) {
    std::lock_guard<std::mutex> lk(mutex());
    table()[temp_path] = {parent_bnk_path, nested_index};
}

inline void clear_all() {
    std::lock_guard<std::mutex> lk(mutex());
    table().clear();
}

inline bool materialize(const std::string& temp_path);

}

struct BNKItem {
    int index;
    std::string name;
    uint32_t size;
};

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
    return s;
}

static std::vector<BNKItem> list_bnk(const std::string& bnk_path) {
    BNKReader reader(bnk_path);
    const auto& files = reader.list_files();
    std::vector<BNKItem> out;
    out.reserve(files.size());
    for (size_t i = 0; i < files.size(); ++i) {
        BNKItem it;
        it.index = static_cast<int>(i);
        it.name  = files[i].name;
        it.size  = files[i].size();
        out.push_back(it);
    }
    return out;
}

static void extract_one(const std::string& bnk_path, int index, const std::string& out_path) {

    LazyNested::materialize(bnk_path);
    BNKReader reader(bnk_path);
    const auto& files = reader.list_files();
    if (index < 0 || static_cast<size_t>(index) >= files.size()) throw std::runtime_error("index out of range");
    reader.extract_file(files[static_cast<size_t>(index)].name, out_path);
}

namespace LazyNested {
inline bool materialize(const std::string& temp_path) {
    PendingExtract pe;
    {
        std::lock_guard<std::mutex> lk(mutex());
        auto it = table().find(temp_path);
        if (it == table().end()) {

            return std::filesystem::exists(temp_path);
        }
        pe = it->second;

        table().erase(it);
    }
    if (std::filesystem::exists(temp_path)) return true;
    try {
        std::filesystem::create_directories(std::filesystem::path(temp_path).parent_path());
        extract_one(pe.parent_bnk_path, pe.nested_index, temp_path);
        return true;
    } catch (...) {

        std::lock_guard<std::mutex> lk(mutex());
        table()[temp_path] = pe;
        return false;
    }
}
}

static std::vector<std::string> find_bnks(const std::string& root, const std::vector<std::string>& exts = std::vector<std::string>{".bnk"}) {
    std::vector<std::string> hits;
    std::vector<std::string> exts_lower;
    exts_lower.reserve(exts.size());
    for (auto& e : exts) exts_lower.push_back(to_lower(e));

    if (ISO::IsoMount::instance().is_mounted()) {
        for (const auto& ext : exts_lower) {
            for (const auto& mf : ISO::IsoMount::instance().list_recursive(ext)) {
                hits.push_back(ISO::IsoMount::make_iso_path(mf.path));
            }
        }
        return hits;
    }

    std::filesystem::path base = std::filesystem::absolute(root);
    if (!std::filesystem::exists(base)) return hits;

    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(base, std::filesystem::directory_options::skip_permission_denied, ec);
         it != std::filesystem::recursive_directory_iterator(); ++it)
    {
        if (ec) { ec.clear(); continue; }
        if (!it->is_regular_file(ec)) { if (ec) ec.clear(); continue; }
        std::string ext = to_lower(it->path().extension().string());
        if (std::find(exts_lower.begin(), exts_lower.end(), ext) != exts_lower.end()) {
            hits.push_back(it->path().string());
        }
    }
    return hits;
}

#endif
