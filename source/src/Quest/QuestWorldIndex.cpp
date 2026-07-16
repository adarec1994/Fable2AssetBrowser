#include "QuestWorldIndex.h"

#include "../BNKCore.cpp"
#include "../GDB/GdbParser.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <unordered_set>

namespace Quest {
namespace {

std::string lower_slash(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    std::replace(value.begin(), value.end(), '\\', '/');
    return value;
}

std::string sibling_key(const WorldIndexAsset& asset,
                        const char* extension) {
    std::filesystem::path path = asset.full_path.empty()
        ? asset.name : asset.full_path;
    path.replace_extension(extension);
    return lower_slash(path.generic_string());
}

bool read_file(const std::filesystem::path& path,
               std::vector<uint8_t>& bytes) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    input.seekg(0, std::ios::end);
    const std::streamoff length = input.tellg();
    if (length <= 0 || length > std::streamoff(512 * 1024 * 1024)) {
        return false;
    }
    input.seekg(0, std::ios::beg);
    bytes.resize(static_cast<std::size_t>(length));
    input.read(reinterpret_cast<char*>(bytes.data()), length);
    if (!input) bytes.clear();
    return !bytes.empty();
}

bool extract_sibling(const WorldIndexAsset& asset, const char* extension,
                     std::vector<uint8_t>& bytes) {
    bytes.clear();
    const std::string key = sibling_key(asset, extension);
    const std::string leaf =
        lower_slash(std::filesystem::path(key).filename().string());
    if (!asset.bnk_path.empty()) {
        int index = BnkCache::find_index(asset.bnk_path, key);
        if (index < 0) index = BnkCache::find_index(asset.bnk_path, leaf);
        if (index >= 0) {
            try {
                bytes = BnkCache::extract_bytes(asset.bnk_path, index);
            } catch (...) {
                bytes.clear();
            }
            if (!bytes.empty()) return true;
        }
    }

    if (!asset.full_path.empty()) {
        std::filesystem::path direct = asset.full_path;
        direct.replace_extension(extension);
        if (read_file(direct, bytes)) return true;
    }
    return false;
}

bool tag_payload(const std::string& source, const char* tag,
                 std::string& value) {
    const std::string open = std::string("<") + tag;
    const std::string close = std::string("</") + tag + ">";
    std::size_t begin = source.find(open);
    if (begin == std::string::npos) return false;
    begin = source.find('>', begin);
    if (begin == std::string::npos) return false;
    const std::size_t end = source.find(close, begin + 1);
    if (end == std::string::npos) return false;
    value = source.substr(begin + 1, end - begin - 1);
    return true;
}

bool float_tag(const std::string& source, const char* tag, double& value) {
    std::string text;
    if (!tag_payload(source, tag, text)) return false;
    char* end = nullptr;
    const double parsed = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || !std::isfinite(parsed)) return false;
    value = parsed;
    return true;
}

struct SaveEntity {
    uint32_t hash = 0;
    std::string name;
    bool has_position = false;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

std::vector<SaveEntity> parse_wanted_save_entities(
    const std::vector<uint8_t>& bytes,
    const std::unordered_set<std::string>& wanted) {
    std::vector<SaveEntity> result;
    if (bytes.empty()) return result;
    const std::string xml(reinterpret_cast<const char*>(bytes.data()),
                          bytes.size());
    constexpr const char* kOpen = "<Entity name=\"";
    constexpr const char* kClose = "</Entity>";
    std::size_t position = 0;
    while (true) {
        std::size_t begin = xml.find(kOpen, position);
        if (begin == std::string::npos) break;
        begin += std::char_traits<char>::length(kOpen);
        const std::size_t name_end = xml.find('"', begin);
        if (name_end == std::string::npos) break;
        const std::string name = xml.substr(begin, name_end - begin);
        const std::size_t entity_end = xml.find(kClose, name_end);
        if (entity_end == std::string::npos) break;
        position = entity_end + std::char_traits<char>::length(kClose);
        if (!wanted.count(lower_slash(name))) continue;

        const std::size_t hash_begin = xml.find("0x", name_end);
        if (hash_begin == std::string::npos || hash_begin >= entity_end) {
            continue;
        }
        const std::size_t hash_end = xml.find('<', hash_begin);
        if (hash_end == std::string::npos || hash_end > entity_end) continue;
        uint32_t hash = 0;
        try {
            hash = static_cast<uint32_t>(std::stoul(
                xml.substr(hash_begin + 2, hash_end - hash_begin - 2),
                nullptr, 16));
        } catch (...) {
            continue;
        }

        SaveEntity entity;
        entity.hash = hash;
        entity.name = name;
        const std::string entity_xml = xml.substr(
            name_end + 1, entity_end - name_end - 1);
        std::string physics;
        std::string vector;
        if (tag_payload(entity_xml, "PhysicsData", physics) &&
            tag_payload(physics, "Position", vector) &&
            float_tag(vector, "X", entity.x) &&
            float_tag(vector, "Y", entity.y) &&
            float_tag(vector, "Z", entity.z)) {
            entity.has_position = true;
        }
        result.push_back(std::move(entity));
    }
    return result;
}

bool same_placement(const WorldEntityPlacement& a,
                    const WorldEntityPlacement& b) {
    return lower_slash(a.level) == lower_slash(b.level) &&
           std::abs(a.x - b.x) < 0.001 &&
           std::abs(a.y - b.y) < 0.001 &&
           std::abs(a.z - b.z) < 0.001;
}

void append_placement(
    std::unordered_map<std::string, std::vector<WorldEntityPlacement>>& index,
    const std::string& name, WorldEntityPlacement placement) {
    std::vector<WorldEntityPlacement>& values = index[lower_slash(name)];
    for (const WorldEntityPlacement& existing : values) {
        if (same_placement(existing, placement)) return;
    }
    values.push_back(std::move(placement));
}

}

std::vector<std::string> FindWorldReferenceNames(
    const std::string& decompiled_lua) {
    std::set<std::string> unique;
    static const std::regex identifier(R"(^[A-Za-z_][A-Za-z0-9_ .-]*$)");
    for (const std::string& value : FindLuaStringLiterals(decompiled_lua)) {
        const std::string lower = lower_slash(value);
        if (!std::regex_match(value, identifier) ||
            lower.rfind("text_", 0) == 0 ||
            lower.find(".wav") != std::string::npos ||
            lower.find(".bik") != std::string::npos) {
            continue;
        }
        unique.insert(lower);
    }
    return {unique.begin(), unique.end()};
}

std::unordered_map<std::string, std::vector<WorldEntityPlacement>>
IndexWorldPlacements(const std::vector<WorldIndexAsset>& level_assets,
                     const std::vector<std::string>& entity_names) {
    std::unordered_map<std::string, std::vector<WorldEntityPlacement>> result;
    std::unordered_set<std::string> wanted;
    for (const std::string& name : entity_names) {
        if (!name.empty()) wanted.insert(lower_slash(name));
    }
    if (wanted.empty()) return result;

    std::unordered_set<std::string> visited;
    for (const WorldIndexAsset& asset : level_assets) {
        const std::string level = asset.full_path.empty()
            ? asset.name : asset.full_path;
        const std::string visit_key = lower_slash(asset.bnk_path + "|" +
                                                   sibling_key(asset, ""));
        if (!visited.insert(visit_key).second) continue;

        std::vector<uint8_t> save_bytes;
        if (!extract_sibling(asset, ".save", save_bytes)) continue;
        const std::vector<SaveEntity> entities =
            parse_wanted_save_entities(save_bytes, wanted);
        if (entities.empty()) continue;

        std::vector<std::pair<uint32_t, std::string>> hash_to_name;
        hash_to_name.reserve(entities.size());
        for (const SaveEntity& entity : entities) {
            hash_to_name.emplace_back(entity.hash, entity.name);
            if (entity.has_position) {
                append_placement(result, entity.name,
                                 {level, entity.x, entity.y, entity.z});
            }
        }

        std::vector<uint8_t> gdb_bytes;
        if (!extract_sibling(asset, ".gdb", gdb_bytes)) continue;
        const Gdb::GdbInfo gdb = Gdb::ParseWithSaveMap(
            gdb_bytes, hash_to_name);
        for (const Gdb::Placement& placement : gdb.placements) {
            if (placement.entity_name.empty() ||
                !wanted.count(lower_slash(placement.entity_name))) {
                continue;
            }
            append_placement(result, placement.entity_name,
                             {level, placement.x, placement.y, placement.z});
        }
    }
    return result;
}

}
