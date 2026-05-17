#include "VfsConfig.h"

#include <algorithm>
#include <cstring>

namespace Level {

namespace {

bool ci_starts_with(const std::string& s, size_t off, const char* needle) {
    size_t i = 0;
    while (needle[i] != '\0') {
        if (off + i >= s.size()) return false;
        char a = s[off + i];
        char b = needle[i];
        if (a >= 'A' && a <= 'Z') a = char(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = char(b - 'A' + 'a');
        if (a != b) return false;
        ++i;
    }
    return true;
}

std::string extract_attr(const std::string& s, size_t tag_open,
                         const char* attr)
{
    size_t end = s.find('>', tag_open);
    if (end == std::string::npos) return {};
    const size_t attr_len = std::strlen(attr);
    size_t p = tag_open + 1;
    while (p < end) {
        if (ci_starts_with(s, p, attr) &&
            p + attr_len < end &&
            (s[p + attr_len] == '=' || s[p + attr_len] == ' '))
        {
            p += attr_len;
            while (p < end && s[p] == ' ') ++p;
            if (p >= end || s[p] != '=') return {};
            ++p;
            while (p < end && s[p] == ' ') ++p;
            if (p >= end || s[p] != '"') return {};
            ++p;
            const size_t val_start = p;
            while (p < end && s[p] != '"') ++p;
            if (p >= end) return {};
            return s.substr(val_start, p - val_start);
        }
        ++p;
    }
    return {};
}

}

VfsConfig ParseVfsConfig(const std::vector<uint8_t>& bytes) {
    VfsConfig out;
    if (bytes.empty()) return out;

    std::string s(reinterpret_cast<const char*>(bytes.data()), bytes.size());

    bool inside_textures_group = false;
    int  group_depth           = 0;

    size_t pos = 0;
    while (pos < s.size()) {
        const size_t lt = s.find('<', pos);
        if (lt == std::string::npos) break;
        const size_t gt = s.find('>', lt);
        if (gt == std::string::npos) break;

        if (lt + 3 < s.size() && s[lt + 1] == '!' &&
            s[lt + 2] == '-' && s[lt + 3] == '-')
        {
            const size_t close = s.find("-->", lt + 4);
            pos = (close == std::string::npos) ? s.size() : close + 3;
            continue;
        }

        if (lt + 1 < s.size() && s[lt + 1] == '/') {
            if (ci_starts_with(s, lt + 2, "Group")) {
                --group_depth;
                if (group_depth <= 0) inside_textures_group = false;
            }
            pos = gt + 1;
            continue;
        }

        size_t name_start = lt + 1;
        size_t name_end   = name_start;
        while (name_end < gt &&
               s[name_end] != ' ' && s[name_end] != '/' &&
               s[name_end] != '>' && s[name_end] != '\t' &&
               s[name_end] != '\n' && s[name_end] != '\r')
        {
            ++name_end;
        }
        std::string tag(s, name_start, name_end - name_start);
        std::string tag_lower = tag;
        std::transform(tag_lower.begin(), tag_lower.end(),
                       tag_lower.begin(), ::tolower);

        if (tag_lower == "group") {
            ++group_depth;
            std::string id_attr = extract_attr(s, lt, "ID");
            std::string id_lower = id_attr;
            std::transform(id_lower.begin(), id_lower.end(),
                           id_lower.begin(), ::tolower);
            if (id_lower == "level_textures") {
                inside_textures_group = true;
            }
        } else if (tag_lower == "bank") {
            std::string path = extract_attr(s, lt, "Path");
            if (!path.empty()) {
                out.bnk_paths.push_back(path);
                std::string lower = path;
                std::transform(lower.begin(), lower.end(),
                               lower.begin(), ::tolower);
                if (inside_textures_group) {
                    if (lower.find("texture_headers") == std::string::npos &&
                        lower.find("1024mip0") == std::string::npos)
                    {
                        out.texture_body_bnks.push_back(path);
                    }
                } else {
                    if (lower.find("streaming") != std::string::npos) {
                        out.streaming_bnks.push_back(path);
                    } else if (lower.find("model") != std::string::npos) {
                        out.model_bnks.push_back(path);
                    }
                }
            }
        }

        pos = gt + 1;
    }
    return out;
}

}
