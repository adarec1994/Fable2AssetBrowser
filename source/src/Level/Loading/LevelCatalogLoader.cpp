#include "Level/Core/LevelLoader.h"
#include "Level/Loading/LevelCatalogLoaderInternal.h"
#include "Level/Database/TextBank.h"
#include "Entity/StaticPropAuthoring.h"
#include "GDB/GdbParser.h"
#include "BNKCore.cpp"
#include "ISO/IsoMount.h"
#include "UI/OutputLog.h"
#include "Utilities/State.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Level {

using Loading::AuthoredEntityBinding;

namespace {


std::unordered_map<uint32_t, AuthoredEntityBinding>
    g_global_entity_by_authored_name_hash;
}

static bool load_game_gdb_standalone(const std::string& rel_path,
                                     std::vector<uint8_t>& out) {
    out.clear();
    std::string key = rel_path;
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    std::replace(key.begin(), key.end(), '\\', '/');

    if (ISO::IsoMount::instance().is_mounted()) {
        auto bytes = ISO::IsoMount::instance().read_file(key);
        if (!bytes.empty()) {
            out = std::move(bytes);
            return true;
        }
    }
    if (!S.root_dir.empty() &&
        !ISO::IsoMount::is_iso_path(S.root_dir)) {
        std::error_code ec;
        std::filesystem::path root_p(S.root_dir);
        if (std::filesystem::is_regular_file(root_p, ec)) {
            root_p = root_p.parent_path();
        }
        std::string rp = rel_path;
        std::replace(rp.begin(), rp.end(), '\\', '/');
        std::filesystem::path fp = root_p / rp;
        std::ifstream f(fp, std::ios::binary | std::ios::ate);
        if (f) {
            const std::streamoff sz = f.tellg();
            if (sz > 0) {
                out.resize(size_t(sz));
                f.seekg(0);
                f.read(reinterpret_cast<char*>(out.data()), sz);
                if (f) return true;
                out.clear();
            }
        }
    }
    const std::string leaf =
        key.substr(key.find_last_of('/') + 1);
    for (const auto& bnk_path : S.bnk_paths) {
        int idx = BnkCache::find_index(bnk_path, key);
        if (idx < 0) idx = BnkCache::find_index(bnk_path, leaf);
        if (idx < 0) continue;
        try {
            auto v = BnkCache::extract_bytes(bnk_path, idx);
            if (!v.empty()) {
                out.assign(v.begin(), v.end());
                return true;
            }
        } catch (...) {
        }
    }
    return false;
}

void BuildGlobalItemCatalog() {
    static const char* kGameGdbs[] = {
        "data/Globals/Globals.gdb",
        "data/Entity/Entity.gdb",
        "data/InteractiveCutscenes/InteractiveCutscenes.gdb",
        "data/EnvironmentThemes/EnvironmentThemes.gdb",
    };
    std::vector<std::vector<uint8_t>> owned;
    std::vector<const std::vector<uint8_t>*> gdbs;
    for (const char* p : kGameGdbs) {
        std::vector<uint8_t> bytes;
        if (load_game_gdb_standalone(p, bytes)) {
            owned.push_back(std::move(bytes));
        }
    }
    for (const auto& b : owned) gdbs.push_back(&b);
    if (gdbs.empty()) return;

    auto details = Gdb::BuildItemDetails(gdbs);
    if (details.empty()) return;
    TextBank::LoadForRoot(S.root_dir);
    for (auto& d : details) {
        if (d.is_money) {
            d.display_name = "Money (" +
                             std::to_string(d.money < 0 ? 0 : d.money) +
                             " gold)";
            continue;
        }
        std::string nm;
        if (d.name_tag && TextBank::Lookup(d.name_tag, nm) &&
            !nm.empty()) {

            while (!nm.empty() && nm.back() == '\0') nm.pop_back();
            d.display_name = nm;
        } else {
            d.display_name = d.label;
        }
    }
    auto ci_less = [](const std::string& an, const std::string& bn) {
        const size_t n = std::min(an.size(), bn.size());
        for (size_t i = 0; i < n; ++i) {
            const int ca = std::tolower((unsigned char)an[i]);
            const int cb = std::tolower((unsigned char)bn[i]);
            if (ca != cb) return ca < cb;
        }
        return an.size() < bn.size();
    };


    auto sort_key = [](const Gdb::ItemDetail& d) -> std::string {
        if (d.is_money) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "money %010d",
                          d.money < 0 ? 0 : d.money);
            return buf;
        }
        return d.display_name;
    };
    std::sort(details.begin(), details.end(),
              [&](const Gdb::ItemDetail& a, const Gdb::ItemDetail& b) {
                  return ci_less(sort_key(a), sort_key(b));
              });




    auto ci_eq = [](const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            if (std::tolower((unsigned char)a[i]) !=
                std::tolower((unsigned char)b[i])) {
                return false;
            }
        }
        return true;
    };
    std::vector<Gdb::ItemDetail> merged;
    merged.reserve(details.size());
    for (auto& d : details) {
        if (!merged.empty() &&
            !d.display_name.empty() &&
            ci_eq(merged.back().display_name, d.display_name)) {
            Gdb::ItemDetail& m = merged.back();
            if (m.model_path.empty() && !d.model_path.empty()) {
                m.model_path = d.model_path;
            }
            if (!m.model_path_hash) m.model_path_hash = d.model_path_hash;
            if (!m.desc_tag) m.desc_tag = d.desc_tag;
            if (m.icon_tex.empty()) m.icon_tex = d.icon_tex;
            if (m.money < 0) m.money = d.money;
            std::unordered_set<std::string> have;
            for (auto& s : m.stats) have.insert(s.first);
            for (auto& s : d.stats) {
                if (have.insert(s.first).second) {
                    m.stats.push_back(s);
                }
            }
            continue;
        }
        merged.push_back(std::move(d));
    }

    g_item_details = std::move(merged);
    OutputLog::info("items: indexed " +
                    std::to_string(g_item_details.size()) +
                    " item(s) from the game GDBs");
}

void BuildGlobalEntityCatalog() {
    static const char* kGameGdbs[] = {
        "data/Globals/Globals.gdb",
        "data/Globals/SpeechAction.gdb",
        "data/Entity/Entity.gdb",
        "data/InteractiveCutscenes/InteractiveCutscenes.gdb",
    };
    std::vector<std::vector<uint8_t>> owned;
    owned.reserve(std::size(kGameGdbs) + S.all_gdb_files.size());
    std::vector<const std::vector<uint8_t>*> gdbs;
    std::unordered_set<std::string> loaded_sources;
    loaded_sources.reserve(S.all_gdb_files.size() * 2 + 8);
    for (const char* path : kGameGdbs) {
        std::vector<uint8_t> bytes;
        if (load_game_gdb_standalone(path, bytes)) {
            owned.push_back(std::move(bytes));
        }
    }
    size_t indexed_gdbs = 0;
    size_t failed_gdbs = 0;
    for (const FlatAssetEntry& entry : S.all_gdb_files) {
        const std::string source_key =
            entry.bnk_path + "#" + std::to_string(entry.file_index);
        if (!loaded_sources.insert(source_key).second) continue;
        try {
            std::vector<uint8_t> bytes =
                BnkCache::extract_bytes(entry.bnk_path, entry.file_index);
            if (bytes.size() >= 4 &&
                bytes[0] == 'G' && bytes[1] == 'D' &&
                bytes[2] == 'B' && bytes[3] == 0) {
                owned.push_back(std::move(bytes));
                ++indexed_gdbs;
            } else {
                ++failed_gdbs;
            }
        } catch (...) {
            ++failed_gdbs;
        }
    }
    for (const auto& bytes : owned) gdbs.push_back(&bytes);
    if (gdbs.empty()) return;

    OutputLog::info(
        "entities: scanning " + std::to_string(gdbs.size()) +
        " GDB(s), including " + std::to_string(indexed_gdbs) +
        " indexed archive GDB(s)" +
        (failed_gdbs ? " (" + std::to_string(failed_gdbs) +
                           " unreadable)" : std::string()));

    std::vector<std::pair<uint32_t, std::string>> named_entities;
    std::unordered_set<uint32_t> named_hashes;
    auto collect_save_names = [&](const std::vector<uint8_t>& bytes) {
        const std::string xml(reinterpret_cast<const char*>(bytes.data()),
                              bytes.size());
        constexpr const char* kOpen = "<Entity name=\"";
        size_t pos = 0;
        while ((pos = xml.find(kOpen, pos)) != std::string::npos) {
            const size_t name_begin = pos + std::strlen(kOpen);
            const size_t name_end = xml.find('"', name_begin);
            if (name_end == std::string::npos) break;
            const size_t tag_end = xml.find('>', name_end);
            if (tag_end == std::string::npos) break;
            const size_t close = xml.find("</Entity>", tag_end + 1);
            if (close == std::string::npos) break;
            const size_t hex = xml.find("0x", tag_end + 1);
            if (hex != std::string::npos && hex < close) {
                size_t hex_end = hex + 2;
                while (hex_end < close &&
                       std::isxdigit(static_cast<unsigned char>(
                           xml[hex_end])) &&
                       hex_end - (hex + 2) < 8) {
                    ++hex_end;
                }
                if (hex_end > hex + 2) {
                    try {
                        const uint32_t hash = static_cast<uint32_t>(
                            std::stoul(xml.substr(hex + 2,
                                                  hex_end - hex - 2),
                                       nullptr, 16));
                        if (hash != 0 && named_hashes.insert(hash).second) {
                            named_entities.emplace_back(
                                hash,
                                xml.substr(name_begin,
                                           name_end - name_begin));
                        }
                    } catch (...) {
                    }
                }
            }
            pos = close + std::strlen("</Entity>");
        }
    };
    size_t save_failures = 0;
    for (const FlatAssetEntry& entry : S.all_save_files) {
        try {
            const std::vector<uint8_t> bytes =
                BnkCache::extract_bytes(entry.bnk_path, entry.file_index);
            collect_save_names(bytes);
        } catch (...) {
            ++save_failures;
        }
    }
    OutputLog::info(
        "entities: indexed " + std::to_string(named_entities.size()) +
        " named level entity reference(s) from " +
        std::to_string(S.all_save_files.size()) + " save map(s)" +
        (save_failures ? " (" + std::to_string(save_failures) +
                             " unreadable)" : std::string()));

    std::vector<Gdb::CreatureCatalogEntry> entities =
        Gdb::CollectCreatureNames(gdbs, named_entities);
    TextBank::LoadForRoot(S.root_dir);
    for (auto& entity : entities) {
        std::string localized;
        if (entity.name_tag_hash != 0 &&
            TextBank::Lookup(entity.name_tag_hash, localized)) {
            while (!localized.empty() && localized.back() == '\0') {
                localized.pop_back();
            }
            if (!localized.empty()) entity.display_name = localized;
        }
        if (entity.display_name.empty()) entity.display_name = entity.name;
        if (entity.display_name == "Balverine Sire") {
            entity.display_name = "White Balverine (Balverine Sire)";
        }
    }





    std::unordered_map<uint32_t, std::vector<uint32_t>>
        exact_model_hashes;
    exact_model_hashes.reserve(entities.size() * 2 + 1);
    for (const auto& entity : entities) {
        if (entity.entity_hash == 0 || entity.model_hashes.empty()) continue;
        auto& models = exact_model_hashes[entity.entity_hash];
        for (uint32_t model_hash : entity.model_hashes) {
            if (model_hash != 0 &&
                std::find(models.begin(), models.end(), model_hash) ==
                    models.end()) {
                models.push_back(model_hash);
            }
        }
    }





    const auto identity_key = [](const std::string& value) {
        std::string key;
        key.reserve(value.size());
        for (const unsigned char c : value) {
            if (std::isalnum(c)) {
                key.push_back(static_cast<char>(std::tolower(c)));
            }
        }
        return key;
    };
    const auto model_path_hash = [](std::string path) {
        std::transform(path.begin(), path.end(), path.begin(),
                       [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
        std::replace(path.begin(), path.end(), '/', '\\');
        uint32_t hash = 0x811C9DC5u;
        for (const unsigned char c : path) {
            hash *= 0x01000193u;
            hash ^= uint32_t(c);
        }
        return hash;
    };
    std::unordered_map<uint32_t, std::string> model_path_by_hash;
    model_path_by_hash.reserve(S.all_mdl_files.size() * 2 + 1);
    for (const FlatAssetEntry& model : S.all_mdl_files) {
        std::string path = model.full_path;
        std::transform(path.begin(), path.end(), path.begin(),
                       [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
        std::replace(path.begin(), path.end(), '/', '\\');
        model_path_by_hash.try_emplace(model_path_hash(path), std::move(path));
    }
    const auto authored_model_score = [&model_path_by_hash](
        const Gdb::CreatureCatalogEntry& entry) {
        int score = 0;
        for (const uint32_t hash : entry.model_hashes) {
            const auto found = model_path_by_hash.find(hash);
            if (found == model_path_by_hash.end()) continue;
            const std::string& path = found->second;
            if (path.find("\\specific npc\\") != std::string::npos) {
                score += 1000;
            }
            if (path.find("\\random villagers\\") != std::string::npos) {
                score -= 250;
            }
            if (path.find("\\shared assets\\") != std::string::npos) {
                score -= 5;
            }
        }
        return score;
    };
    const auto is_better_representative = [&](
        const Gdb::CreatureCatalogEntry& candidate,
        const Gdb::CreatureCatalogEntry& current) {
        const int candidate_authored_score = authored_model_score(candidate);
        const int current_authored_score = authored_model_score(current);
        if (candidate_authored_score != current_authored_score) {
            return candidate_authored_score > current_authored_score;
        }
        if (candidate.model_hashes.size() != current.model_hashes.size()) {
            return candidate.model_hashes.size() > current.model_hashes.size();
        }
        const bool candidate_is_base =
            identity_key(candidate.name) == identity_key(candidate.display_name);
        const bool current_is_base =
            identity_key(current.name) == identity_key(current.display_name);
        if (candidate_is_base != current_is_base) return candidate_is_base;
        if (candidate.name.size() != current.name.size()) {
            return candidate.name.size() < current.name.size();
        }
        return candidate.entity_hash < current.entity_hash;
    };

    std::unordered_map<uint32_t, size_t> entity_by_authored_name_hash;
    entity_by_authored_name_hash.reserve(entities.size() * 2 + 1);
    for (size_t i = 0; i < entities.size(); ++i) {
        const uint32_t name_hash = entities[i].authored_name_hash;
        if (name_hash == 0) continue;
        const auto [it, inserted] =
            entity_by_authored_name_hash.emplace(name_hash, i);
        if (!inserted &&
            is_better_representative(entities[i], entities[it->second])) {
            it->second = i;
        }
    }
    std::unordered_map<uint32_t, AuthoredEntityBinding>
        authored_entity_bindings;
    authored_entity_bindings.reserve(entity_by_authored_name_hash.size());
    for (const auto& [name_hash, index] : entity_by_authored_name_hash) {
        const auto& entity = entities[index];
        authored_entity_bindings.emplace(
            name_hash,
            AuthoredEntityBinding{entity.entity_hash, entity.name,
                                  entity.model_hashes});
    }

    const size_t authored_entity_count = entities.size();
    std::unordered_map<std::string, size_t> entity_by_identity;
    std::vector<Gdb::CreatureCatalogEntry> unique_entities;
    unique_entities.reserve(entities.size());
    for (auto& entity : entities) {
        std::string key = identity_key(entity.display_name);
        if (key.empty()) key = "#" + std::to_string(entity.entity_hash);

        const auto [it, inserted] =
            entity_by_identity.emplace(key, unique_entities.size());
        if (inserted) {
            unique_entities.push_back(std::move(entity));
        } else if (is_better_representative(
                       entity, unique_entities[it->second])) {
            unique_entities[it->second] = std::move(entity);
        }
    }
    entities = std::move(unique_entities);
    const size_t duplicate_entity_count = authored_entity_count - entities.size();
    if (duplicate_entity_count != 0) {
        OutputLog::info(
            "entities: collapsed " +
            std::to_string(duplicate_entity_count) +
            " duplicate quest/scenario actor record(s)");
    }

    std::vector<StaticPropAuthoring::CatalogEntry> static_props;
    std::string static_prop_error;
    if (StaticPropAuthoring::LoadCatalog(
            S.root_dir, static_props, static_prop_error)) {
        for (const auto& prop : static_props) {
            Gdb::CreatureCatalogEntry entity;
            entity.name = prop.internal_name;
            entity.display_name = prop.internal_name;
            entity.entity_hash = prop.entity_hash;
            entity.kind = Gdb::EntityCatalogKind::StaticProp;
            entity.model_hashes.push_back(prop.model_path_hash);
            entity.transform_component_field =
                prop.transform_component_field;
            entity.transform_component_template =
                prop.transform_component_template;
            entity.position_template = prop.position_template;
            entity.rotation_template = prop.rotation_template;
            entities.push_back(std::move(entity));

            if (prop.entity_hash != 0 && prop.model_path_hash != 0) {
                exact_model_hashes[prop.entity_hash] = {
                    prop.model_path_hash};
            }
        }
    } else if (!static_prop_error.empty() &&
               !S.root_dir.empty() &&
               !ISO::IsoMount::is_iso_path(S.root_dir)) {
        OutputLog::warn(
            "entities: could not index custom static props: " +
            static_prop_error);
    }

    std::sort(entities.begin(), entities.end(),
              [](const Gdb::CreatureCatalogEntry& a,
                 const Gdb::CreatureCatalogEntry& b) {
                  std::string left = a.display_name;
                  std::string right = b.display_name;
                  std::transform(left.begin(), left.end(), left.begin(),
                                 [](unsigned char c) {
                                     return char(std::tolower(c));
                                 });
                  std::transform(right.begin(), right.end(), right.begin(),
                                 [](unsigned char c) {
                                     return char(std::tolower(c));
                                 });
                  if (left != right) return left < right;
                  return a.name < b.name;
              });
    std::vector<std::pair<uint32_t, std::string>> names;
    names.reserve(entities.size());
    for (const auto& entity : entities) {
        names.emplace_back(entity.entity_hash, entity.name);
    }
    auto gameplay = Gdb::ExtractEntityGameplayDetails(gdbs, names);
    auto gameplay_options = Gdb::CollectEntityGameplayOptions(gdbs);

    g_global_entity_catalog = std::move(entities);
    g_global_entity_model_hashes = std::move(exact_model_hashes);
    g_global_entity_by_authored_name_hash =
        std::move(authored_entity_bindings);
    g_global_entity_gameplay = std::move(gameplay);
    g_global_entity_gameplay_options = std::move(gameplay_options);
    ++g_global_entity_catalog_revision;
    OutputLog::info(
        "entities: indexed " +
        std::to_string(g_global_entity_catalog.size()) +
        " entity definition(s) from all game and level GDBs (" +
        std::to_string(g_global_entity_gameplay.size()) +
        " NPC/creature definitions with gameplay details, " +
        std::to_string(static_props.size()) + " custom static prop(s))");
}

namespace Loading {

const AuthoredEntityBinding* FindAuthoredEntityBinding(
    uint32_t authored_name_hash)
{
    const auto found =
        g_global_entity_by_authored_name_hash.find(authored_name_hash);
    return found == g_global_entity_by_authored_name_hash.end()
        ? nullptr : &found->second;
}

}

}
