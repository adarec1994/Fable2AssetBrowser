#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <optional>
#include "BNKReader.cpp"

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
    BNKReader reader(bnk_path);
    const auto& files = reader.list_files();
    if (index < 0 || static_cast<size_t>(index) >= files.size()) throw std::runtime_error("index out of range");
    reader.extract_file(files[static_cast<size_t>(index)].name, out_path);
}

static std::vector<std::string> find_bnks(const std::string& root, const std::vector<std::string>& exts = std::vector<std::string>{".bnk"}) {
    std::vector<std::string> hits;
    std::vector<std::string> exts_lower;
    exts_lower.reserve(exts.size());
    for (auto& e : exts) exts_lower.push_back(to_lower(e));

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
