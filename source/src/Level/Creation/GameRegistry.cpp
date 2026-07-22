#include "GameRegistry.h"

#include "../Database/TextBank.h"
#include "../../Utilities/GameBackup.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace Level {
namespace Creation {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });
    return s;
}

std::string trim(std::string value) {
    size_t first = 0;
    while (first < value.size() &&
           std::isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }
    size_t last = value.size();
    while (last > first &&
           std::isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }
    return value.substr(first, last - first);
}

std::string level_text_tag(const std::string& region) {
    std::string tag = "TEXT_F2AB_REGION_";
    for (unsigned char c : region) {
        tag.push_back(std::isalnum(c) ? char(std::toupper(c)) : '_');
    }
    return tag;
}

std::filesystem::path root_from_data_dir(const std::string& data_dir) {
    const std::filesystem::path data(data_dir);
    return lower(data.filename().string()) == "data" ? data.parent_path()
                                                        : data;
}

bool find_level_text_tag(const std::string& text,
                         const std::string& region,
                         std::string& tag) {
    const std::string key = "\"" + lower(region) + "\"";
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = text.find('\n', start);
        const size_t count = end == std::string::npos
                                 ? text.size() - start
                                 : end - start;
        std::string line = text.substr(start, count);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t first = 0;
        while (first < line.size() &&
               std::isspace(static_cast<unsigned char>(line[first]))) {
            ++first;
        }
        if (lower(line.substr(first)).rfind(key, 0) == 0) {
            const size_t tag_begin = line.find("[\"", first + key.size());
            if (tag_begin == std::string::npos) return false;
            const size_t tag_end = line.find("\"]", tag_begin + 2);
            if (tag_end == std::string::npos) return false;
            tag = line.substr(tag_begin + 2, tag_end - tag_begin - 2);
            return !tag.empty();
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return false;
}

void upsert_level_text_tag(std::string& text, const std::string& region,
                           const std::string& tag) {
    const std::string key = "\"" + lower(region) + "\"";
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = text.find('\n', start);
        const size_t count = end == std::string::npos
                                 ? text.size() - start
                                 : end - start;
        const std::string line = text.substr(start, count);
        size_t first = 0;
        while (first < line.size() &&
               std::isspace(static_cast<unsigned char>(line[first]))) {
            ++first;
        }
        if (lower(line.substr(first)).rfind(key, 0) == 0) {
            const size_t tag_begin = line.find("[\"", first + key.size());
            const size_t tag_end = tag_begin == std::string::npos
                                       ? std::string::npos
                                       : line.find("\"]", tag_begin + 2);
            if (tag_end != std::string::npos) {
                text.replace(start + tag_begin + 2,
                             tag_end - tag_begin - 2, tag);
                return;
            }
            const std::string replacement =
                "\"" + region + "\" [\"" + tag +
                "\"]: FALSE, FALSE;";
            text.replace(start, count, replacement);
            return;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (!text.empty() && text.back() != '\n') text += "\r\n";
    text += "\"" + region + "\" [\"" + tag +
            "\"]: FALSE, FALSE;\r\n";
}

std::string lua_string(std::string value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char c : value) {
        if (c == '\\' || c == '\"') escaped.push_back('\\');
        escaped.push_back(c);
    }
    return escaped;
}

bool read_text_file(const std::filesystem::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool write_text_file(const std::filesystem::path& p, const std::string& text,
                     std::string& error) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) {
        error = "cannot open for writing: " + p.string();
        return false;
    }
    f.write(text.data(), (std::streamsize)text.size());
    if (!f) {
        error = "write failed: " + p.string();
        return false;
    }
    return true;
}


bool append_missing_lines(const std::filesystem::path&    p,
                          const std::vector<std::string>& lines,
                          std::string&                    error) {
    std::string text;
    if (!read_text_file(p, text)) {
        error = "cannot read: " + p.string();
        return false;
    }
    const std::string hay = lower(text);
    std::string added;
    for (const std::string& line : lines) {
        if (hay.find(lower(line)) != std::string::npos) continue;
        added += line;
        added += "\r\n";
    }
    if (added.empty()) return true;
    if (!text.empty() && text.back() != '\n') text += "\r\n";
    text += added;
    return write_text_file(p, text, error);
}

constexpr const char* kMenuBegin =
    "-- BEGIN F2AB CUSTOM LEVELS (auto-generated by Fable2AssetBrowser; do not edit inside markers)";
constexpr const char* kMenuEnd = "-- END F2AB CUSTOM LEVELS";

std::string find_start_entity(const std::string& data_dir,
                              const std::string& region) {
    std::string text;
    const std::filesystem::path save =
        std::filesystem::path(data_dir) / "worlds" / "albion" / region /
        "defaultscenario" / "defaultscenario.save";
    if (!read_text_file(save, text)) return {};
    const size_t at = text.find("StartFrom_");
    if (at == std::string::npos) return {};
    size_t end = at;
    while (end < text.size() &&
           (std::isalnum(static_cast<unsigned char>(text[end])) ||
            text[end] == '_')) {
        ++end;
    }
    return text.substr(at, end - at);
}

}

bool RegisterInScenariosList(const std::string& data_dir,
                             const std::string& region,
                             std::string&       error) {
    const std::filesystem::path p =
        std::filesystem::path(data_dir) / "scenarios.list";
    const std::string line =
        "level:'albion\\" + region + "' 1 'defaultscenario'";
    return append_missing_lines(p, {line}, error);
}

bool RegisterInDirManifest(const std::string&              data_dir,
                           const std::vector<std::string>& virtual_paths,
                           std::string&                    error) {
    const std::filesystem::path p =
        std::filesystem::path(data_dir) / "dir.manifest";
    return append_missing_lines(p, virtual_paths, error);
}

bool UnregisterLevel(const std::string& data_dir,
                     const std::string& region,
                     std::string&       error) {
    const std::string region_low = lower(region);
    auto strip_lines = [&](const std::filesystem::path& p,
                           const std::string& needle) -> bool {
        std::string text;
        if (!read_text_file(p, text)) return true;   
        std::string out;
        out.reserve(text.size());
        size_t start = 0;
        bool changed = false;
        while (start <= text.size()) {
            size_t end = text.find('\n', start);
            const bool last = end == std::string::npos;
            std::string line = text.substr(
                start, last ? std::string::npos : end - start + 1);
            if (lower(line).find(needle) != std::string::npos) {
                changed = true;
            } else {
                out += line;
            }
            if (last) break;
            start = end + 1;
        }
        if (!changed) return true;
        return write_text_file(p, out, error);
    };

    if (!strip_lines(std::filesystem::path(data_dir) / "scenarios.list",
                     "albion\\" + region_low + "'")) {
        return false;
    }
    if (!strip_lines(std::filesystem::path(data_dir) / "dir.manifest",
                     "worlds\\albion\\" + region_low + "\\")) {
        return false;
    }
    if (!strip_lines(std::filesystem::path(data_dir) / "miscellaneous" /
                         "fasttravellist.txt",
                     "\"" + region_low + "\"")) {
        return false;
    }
    return true;
}

std::vector<std::string> ListCustomLevels(const std::string& data_dir) {
    std::vector<std::string> out;
    std::error_code ec;
    const std::filesystem::path albion =
        std::filesystem::path(data_dir) / "worlds" / "albion";
    if (!std::filesystem::is_directory(albion, ec)) return out;
    for (std::filesystem::directory_iterator it(albion, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_directory(ec)) continue;
        const std::filesystem::path lev = it->path() / "defaultscenario" /
                                          "defaultscenario.engine_level";
        if (std::filesystem::is_regular_file(lev, ec)) {
            out.push_back(it->path().filename().string());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string GetCustomLevelDisplayName(const std::string& data_dir,
                                      const std::string& region) {
    std::string text;
    std::string tag;
    const std::filesystem::path fast_travel =
        std::filesystem::path(data_dir) / "miscellaneous" /
        "fasttravellist.txt";
    if (read_text_file(fast_travel, text) &&
        find_level_text_tag(text, region, tag)) {
        const std::filesystem::path root = root_from_data_dir(data_dir);
        TextBank::LoadForRoot(root.string());
        std::string display_name;
        if (TextBank::LookupTag(tag, display_name) &&
            !display_name.empty()) {
            return display_name;
        }
    }
    return region;
}

bool SetCustomLevelDisplayName(const std::string& data_dir,
                               const std::string& region,
                               const std::string& display_name,
                               std::string& error) {
    const std::string name = trim(display_name);
    if (name.empty()) {
        error = "Enter an in-game level name.";
        return false;
    }
    if (name.size() > 128) {
        error = "The in-game level name is too long.";
        return false;
    }
    for (unsigned char c : name) {
        if (c < 0x20 && c != '\t') {
            error = "The in-game level name cannot contain line breaks.";
            return false;
        }
    }
    if (!GameBackup::RequireBackup(error)) return false;

    const std::filesystem::path root = root_from_data_dir(data_dir);
    const std::filesystem::path fast_travel =
        std::filesystem::path(data_dir) / "miscellaneous" /
        "fasttravellist.txt";
    std::vector<std::string> covered{fast_travel.string()};
    std::error_code ec;
    const std::filesystem::path language =
        std::filesystem::path(data_dir) / "language";
    if (std::filesystem::is_directory(language, ec)) {
        for (std::filesystem::directory_iterator it(language, ec), end;
             !ec && it != end; it.increment(ec)) {
            const std::filesystem::path babel =
                it->path() / "text" / "book.babel";
            if (std::filesystem::is_regular_file(babel, ec)) {
                covered.push_back(babel.string());
            }
        }
    }
    if (!GameBackup::EnsureFilesCovered(covered, error)) return false;

    std::string text;
    if (!read_text_file(fast_travel, text)) {
        error = "cannot read: " + fast_travel.string();
        return false;
    }
    const std::string tag = level_text_tag(region);
    std::unordered_map<uint32_t, std::string> edits;
    edits.emplace(TextBank::TagHash(tag), name);
    if (!TextBank::ApplyEdits(root.string(), edits, error)) return false;

    upsert_level_text_tag(text, region, tag);
    if (!write_text_file(fast_travel, text, error)) return false;
    if (!SyncDebugMenuCustomLevels(data_dir, error)) return false;
    return true;
}

bool SyncDebugMenuCustomLevels(const std::string& data_dir,
                               std::string&       error) {
    const std::filesystem::path menu =
        std::filesystem::path(data_dir) / "scripts" / "Mods" /
        "DebugMenuMod" / "DebugMenuEntries.lua";
    std::error_code ec;
    if (!std::filesystem::is_regular_file(menu, ec)) return true;

    std::string text;
    if (!read_text_file(menu, text)) {
        error = "cannot read: " + menu.string();
        return false;
    }

    std::vector<std::string> lines;
    {
        size_t start = 0;
        while (start <= text.size()) {
            const size_t end = text.find('\n', start);
            const size_t count = end == std::string::npos
                                     ? text.size() - start
                                     : end - start;
            std::string line = text.substr(start, count);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(std::move(line));
            if (end == std::string::npos) break;
            start = end + 1;
        }
        if (!lines.empty() && lines.back().empty()) lines.pop_back();
    }

    const std::string hook = "GetCustomLevelMenuEntries}";
    std::vector<std::string> kept;
    kept.reserve(lines.size());
    bool in_block = false;
    for (std::string& line : lines) {
        if (!in_block && line.find(kMenuBegin) != std::string::npos) {
            in_block = true;
            continue;
        }
        if (in_block) {
            if (line.find(kMenuEnd) != std::string::npos) in_block = false;
            continue;
        }
        if (line.find(hook) != std::string::npos) continue;
        kept.push_back(std::move(line));
    }
    if (in_block) {
        error = "corrupt custom-levels block in " + menu.string();
        return false;
    }

    size_t insert_at = std::string::npos;
    for (size_t i = 0; i < kept.size(); ++i) {
        if (kept[i].find("function GetOverworldLevelMenuEntries()") ==
            std::string::npos) {
            continue;
        }
        for (size_t j = i; j < kept.size(); ++j) {
            if (kept[j].find("return {") != std::string::npos) {
                insert_at = j + 1;
                break;
            }
        }
        break;
    }
    if (insert_at == std::string::npos) {
        error = "GetOverworldLevelMenuEntries not found in " + menu.string();
        return false;
    }

    std::vector<std::string> block;
    block.push_back(std::string("\t\t") + kMenuBegin);
    const std::vector<std::string> levels = ListCustomLevels(data_dir);
    for (const std::string& level : levels) {
        const std::string display_name =
            GetCustomLevelDisplayName(data_dir, level);
        std::string label = display_name;
        if (label.find("(custom)") == std::string::npos) {
            label += " (custom)";
        }
        block.push_back("\t\tNewActionEntry(\"" + lua_string(label) +
                        "\", true, Debug.LoadLevel, {'Albion', '" + level +
                        "', '" + lua_string(find_start_entity(data_dir,
                                                              level)) +
                        "'}),");
    }
    block.push_back(std::string("\t\t") + kMenuEnd);
    kept.insert(kept.begin() + (ptrdiff_t)insert_at, block.begin(),
                block.end());

    std::string out;
    for (const std::string& line : kept) {
        out += line;
        out += "\r\n";
    }
    return write_text_file(menu, out, error);
}

}
}
