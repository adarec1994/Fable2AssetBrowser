#include "Utils.h"
#include "State.h"
#include <algorithm>
#include <filesystem>

bool is_audio_file(const std::string &n) {
    std::string s = n;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s.size() >= 4 && s.rfind(".wav") == s.size() - 4;
}

bool is_tex_file(const std::string &n) {
    std::string s = n;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s.size() >= 4 && s.rfind(".tex") == s.size() - 4;
}

bool is_mdl_file(const std::string &n) {
    std::string s = n;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s.size() >= 4 && s.rfind(".mdl") == s.size() - 4;
}

bool is_model_bnk_selected() {
    if (S.selected_bnk.empty()) return false;
    std::string b = std::filesystem::path(S.selected_bnk).filename().string();
    std::transform(b.begin(), b.end(), b.begin(), ::tolower);
    return b == "globals_model_headers.bnk" || b == "globals_models.bnk";
}

bool is_texture_bnk_selected() {
    if (S.selected_bnk.empty()) return false;
    std::string b = std::filesystem::path(S.selected_bnk).filename().string();
    std::transform(b.begin(), b.end(), b.begin(), ::tolower);
    return b == "globals_texture_headers.bnk" || b == "1024mip0_textures.bnk" || b == "globals_textures.bnk" ||
           b == "gui_texture_headers.bnk" || b == "gui_textures.bnk" || b == "textures.bnk" ||
           b.find("_texture_headers.bnk") != std::string::npos;
}

std::vector<std::string> filtered_bnk_paths() {
    if (S.bnk_filter.empty()) return S.bnk_paths;
    std::vector<std::string> out;
    out.reserve(S.bnk_paths.size());
    std::string q = S.bnk_filter;
    std::transform(q.begin(), q.end(), q.begin(), ::tolower);
    for (auto &p: S.bnk_paths) {
        auto b = std::filesystem::path(p).filename().string();
        std::string t = b;
        std::transform(t.begin(), t.end(), t.begin(), ::tolower);
        if (t.find(q) != std::string::npos) out.push_back(p);
    }
    return out;
}

bool name_matches_filter(const std::string &name, const std::string &filter) {
    if (filter.empty()) return true;
    std::string n = name, f = filter;
    std::transform(n.begin(), n.end(), n.begin(), ::tolower);
    std::transform(f.begin(), f.end(), f.begin(), ::tolower);
    return n.find(f) != std::string::npos;
}

bool name_matches_ext(const std::string &name, const std::string &ext) {
    if (ext.empty()) return true;
    std::string n = name;
    std::transform(n.begin(), n.end(), n.begin(), ::tolower);
    size_t last_sep = n.find_last_of("/\\");
    if (last_sep != std::string::npos) n = n.substr(last_sep + 1);
    size_t last_dot = n.find_last_of('.');
    if (last_dot == std::string::npos) return false;
    return n.substr(last_dot) == ext;
}

std::vector<std::string> unique_file_extensions() {
    std::vector<std::string> out;
    std::vector<std::string> seen;
    seen.reserve(S.files.size());
    for (auto &f : S.files) {
        std::string n = f.name;
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        size_t last_sep = n.find_last_of("/\\");
        if (last_sep != std::string::npos) n = n.substr(last_sep + 1);
        size_t last_dot = n.find_last_of('.');
        if (last_dot == std::string::npos || last_dot == n.size() - 1) continue;
        std::string ext = n.substr(last_dot);
        if (std::find(seen.begin(), seen.end(), ext) == seen.end()) {
            seen.push_back(ext);
        }
    }
    std::sort(seen.begin(), seen.end());
    out = std::move(seen);
    return out;
}

int count_visible_files() {
    int c = 0;
    for (auto &f: S.files)
        if (name_matches_filter(f.name, S.file_filter) && name_matches_ext(f.name, S.ext_filter)) ++c;
    return c;
}

bool any_wav_in_bnk() {
    for (auto &f: S.files) if (is_audio_file(f.name)) return true;
    return false;
}

bool any_tex_in_bnk() {
    for (auto &f: S.files) if (is_tex_file(f.name)) return true;
    return false;
}

bool any_mdl_in_bnk() {
    for (auto &f : S.files) if (is_mdl_file(f.name)) return true;
    return false;
}

std::optional<std::string> find_bnk_by_filename(const std::string &fname_lower) {
    for (auto &p: S.bnk_paths) {
        std::string b = std::filesystem::path(p).filename().string();
        std::transform(b.begin(), b.end(), b.begin(), ::tolower);
        if (b == fname_lower) return p;
    }

    for (auto &p: S.nested_bnk_paths) {
        std::string b = std::filesystem::path(p).filename().string();
        std::transform(b.begin(), b.end(), b.begin(), ::tolower);
        if (b == fname_lower) return p;
    }
    return std::nullopt;
}
