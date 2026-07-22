#include "HeroDesigner.h"

#include "../ContentTabs.h"
#include "../EntityModelResolver.h"
#include "../OutputLog.h"
#include "../../Level/Core/LevelLoader.h"
#include "../../Utilities/State.h"
#include "../../Utilities/Utils.h"
#include "../../animations/AnimBank.h"
#include "../../BNKReader.cpp"

#include "IconsFontAwesome6.h"
#include "imgui.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace HeroDesigner {
namespace {

enum class Sex { Male, Female };

enum class Slot : std::size_t {
    Hair,
    Beard,
    Moustache,
    Hat,
    Coat,
    Shirt,
    Gloves,
    Trousers,
    Boots,
    Mask,
    Suit,
    Accessories,
    Melee,
    Ranged,
    Count,
};

struct Option {
    std::string label;
    std::string path;
    std::uint32_t hash = 0;
    std::uint32_t body_areas_covered = 0;
    int cluster_sort_layer = -100;
    std::vector<std::string> clusters_covered;
    int weapon_type = 0;
    bool name_localized = false;
    bool is_pistol = false;
};

struct SlotState {
    const char* label = "";
    std::vector<Option> options;
    int selected = 0;
};

struct MorphState {
    float strong = 0.0f;
    float fat = 0.0f;
    float tall = 0.0f;
    float young = 0.0f;
    float old = 0.0f;
    float impure = 0.0f;
    float evil_pure = 0.0f;
    float evil_impure = 0.0f;
};

struct BoneOffset {
    std::string name;
    float position[3]{};
    float scale[3]{1.0f, 1.0f, 1.0f};
};

struct AppearanceLayer {
    std::uint32_t model_hash = 0;
    int sort_layer = -100;
    std::unordered_set<std::string> clusters_covered;
};

enum class MorphKind {
    Strong,
    Fat,
    Young,
    Old,
    Impure,
    EvilPure,
    EvilImpure,
};

struct MorphVariant {
    MorphKind kind = MorphKind::Young;
    EntityModels::ResolvedModel model;
};

struct MorphCache {
    EntityModels::ResolvedModel neutral;
    std::vector<MorphVariant> variants;
    std::vector<BoneOffset> strong;
    std::vector<BoneOffset> fat;
    std::vector<BoneOffset> tall;
    std::vector<AppearanceLayer> appearance;
    std::uint32_t animation_source_hash = 0;
    std::vector<std::uint32_t> animation_model_hashes;
};

struct PreviewResult {
    std::uint64_t request = 0;
    std::shared_ptr<MorphCache> cache;
    std::string error;
};

struct DesignerState {
    Sex sex = Sex::Male;
    std::array<SlotState, static_cast<std::size_t>(Slot::Count)> slots{};
    MorphState morph;
    std::size_t catalog_model_count = 0;
    std::size_t catalog_item_count = 0;
    bool catalog_ready = false;
    bool opened = false;
    bool loading = false;
    bool preview_queued = false;
    std::shared_ptr<MorphCache> morph_cache;
    std::string status = "Choose a game data folder to build the Hero.";
    std::atomic<std::uint64_t> next_request{0};
    std::mutex completion_mutex;
    std::vector<PreviewResult> completions;
};

DesignerState& state() {
    static DesignerState value;
    static bool labels_set = false;
    if (!labels_set) {
        auto& s = value.slots;
        s[static_cast<std::size_t>(Slot::Hair)].label = "Hair";
        s[static_cast<std::size_t>(Slot::Beard)].label = "Beard";
        s[static_cast<std::size_t>(Slot::Moustache)].label = "Moustache";
        s[static_cast<std::size_t>(Slot::Hat)].label = "Hat";
        s[static_cast<std::size_t>(Slot::Coat)].label = "Coat";
        s[static_cast<std::size_t>(Slot::Shirt)].label = "Shirt";
        s[static_cast<std::size_t>(Slot::Gloves)].label = "Gloves";
        s[static_cast<std::size_t>(Slot::Trousers)].label = "Trousers";
        s[static_cast<std::size_t>(Slot::Boots)].label = "Boots";
        s[static_cast<std::size_t>(Slot::Mask)].label = "Mask";
        s[static_cast<std::size_t>(Slot::Suit)].label = "Suit";
        s[static_cast<std::size_t>(Slot::Accessories)].label = "Accessories";
        s[static_cast<std::size_t>(Slot::Melee)].label = "Melee weapon";
        s[static_cast<std::size_t>(Slot::Ranged)].label = "Ranged weapon";
        labels_set = true;
    }
    return value;
}

std::string lower_slash(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    std::replace(value.begin(), value.end(), '\\', '/');
    return value;
}

std::string leaf_label(const std::string& path) {
    std::string result = std::filesystem::path(path).stem().string();
    for (char& c : result) {
        if (c == '_' || c == '-') c = ' ';
    }
    return result.empty() ? path : result;
}

bool has_any(const std::string& text,
             std::initializer_list<const char*> needles) {
    for (const char* needle : needles) {
        if (text.find(needle) != std::string::npos) return true;
    }
    return false;
}

bool opposite_sex_asset(const std::string& text, Sex sex) {
    if (sex == Sex::Male) {
        return has_any(text, {"female", "herofemale", "fhero"});
    }
    if (has_any(text, {"female", "herofemale", "fhero"})) return false;
    return has_any(text, {"heromale", "hero_male", "_male_"});
}

Slot classify_named_option(const std::string& source) {
    if (has_any(source, {"moustache", "mustache"})) return Slot::Moustache;
    if (source.find("beard") != std::string::npos) return Slot::Beard;
    if (has_any(source, {"hairstyle", "hair_", "_hair", "/hair"})) {
        return Slot::Hair;
    }
    if (has_any(source, {"rifle", "pistol", "blunderbuss", "crossbow",
                         "firearm", "ranged"})) {
        return Slot::Ranged;
    }
    if (has_any(source, {"weapon", "sword", "katana", "hammer", "mace",
                         "cleaver", "axe"})) {
        return Slot::Melee;
    }
    if (has_any(source, {"accessor", "heroaccess", "bandolier",
                         "guildseal", "belt", "necklace", "earring",
                         "eyepatch"})) {
        return Slot::Accessories;
    }
    if (source.find("mask") != std::string::npos) return Slot::Mask;
    if (has_any(source, {"boots", "shoes", "footwear"})) return Slot::Boots;
    if (has_any(source, {"trouser", "pants", "leggings", "skirt"})) {
        return Slot::Trousers;
    }
    if (has_any(source, {"glove", "gauntlet"})) return Slot::Gloves;
    if (has_any(source, {"shirt", "upperbody", "waistcoat", "torso"})) {
        return Slot::Shirt;
    }
    if (has_any(source, {"coat", "jacket", "overcoat"})) return Slot::Coat;
    if (has_any(source, {"hat", "headwear", "hood", "helmet", "helm"})) {
        return Slot::Hat;
    }
    if (has_any(source, {"suit", "outfit", "costume"})) return Slot::Suit;
    return Slot::Count;
}

Slot classify_authored_item(const Gdb::ItemDetail& item,
                            const std::string& source) {
    const bool has_worn_model = item.worn_model_path_hash != 0 ||
                                item.female_worn_model_path_hash != 0;
    switch (item.category) {
        case 1:
            if (item.weapon_type >= 2 && item.weapon_type <= 4) {
                return Slot::Melee;
            }
            if (item.weapon_type >= 5 && item.weapon_type <= 8) {
                return Slot::Ranged;
            }
            return Slot::Count;
        case 9: return Slot::Hair;
        case 10: return Slot::Beard;
        case 11: return Slot::Moustache;
        case 17: return Slot::Hat;
        case 18: return Slot::Coat;
        case 19: return Slot::Shirt;
        case 20: return Slot::Gloves;
        case 21: return Slot::Trousers;
        case 22: return Slot::Boots;
        case 23:
            return source.find("mask") != std::string::npos
                ? Slot::Mask : Slot::Accessories;
        case 25: return Slot::Suit;
        default: break;
    }
    if (item.category == 0 && has_worn_model) {
        const Slot named = classify_named_option(source);
        if (named == Slot::Accessories) return named;
    }
    return Slot::Count;
}

void add_option(SlotState& slot, Option option) {
    if (!option.hash) return;
    for (Option& current : slot.options) {
        if (current.hash != option.hash) continue;
        if (lower_slash(current.label) == lower_slash(option.label)) {
            if (option.name_localized && !current.name_localized) {
                current = std::move(option);
            }
            return;
        }
        if (current.name_localized && !option.name_localized) return;
        if (!current.name_localized && option.name_localized) {
            current = std::move(option);
            return;
        }
        if (!current.name_localized && !option.name_localized) {
            if (option.label.size() < current.label.size()) {
                current = std::move(option);
            }
            return;
        }
    }
    slot.options.push_back(std::move(option));
}

void reset_options(DesignerState& designer) {
    for (SlotState& slot : designer.slots) {
        slot.options.clear();
        slot.options.push_back({"None", "", 0});
        slot.selected = 0;
    }
}

void rebuild_catalog(bool preserve_selection) {
    DesignerState& designer = state();
    std::array<std::uint32_t, static_cast<std::size_t>(Slot::Count)> selected{};
    if (preserve_selection) {
        for (std::size_t i = 0; i < designer.slots.size(); ++i) {
            const SlotState& slot = designer.slots[i];
            if (slot.selected >= 0 &&
                slot.selected < static_cast<int>(slot.options.size())) {
                selected[i] = slot.options[static_cast<std::size_t>(
                    slot.selected)].hash;
            }
        }
    }
    reset_options(designer);

    for (const Gdb::ItemDetail& item : g_item_details) {
        if (item.is_money ||
            (!item.model_path_hash && !item.worn_model_path_hash &&
             !item.female_worn_model_path_hash)) continue;
        std::string source = item.internal_name + " " + item.label + " " +
                             item.display_name + " " + item.model_path + " ";
        source += designer.sex == Sex::Female
            ? item.female_worn_model_path : item.worn_model_path;
        source = lower_slash(std::move(source));
        if (opposite_sex_asset(source, designer.sex)) continue;
        const Slot slot_id = classify_authored_item(item, source);
        if (slot_id == Slot::Count) continue;
        std::string option_path = item.model_path;
        std::uint32_t option_hash = item.model_path_hash;
        std::vector<std::string> clusters_covered =
            designer.sex == Sex::Female &&
                    !item.female_clusters_covered.empty()
                ? item.female_clusters_covered
                : item.clusters_covered;
        for (std::string& cluster : clusters_covered) {
            cluster = lower_slash(std::move(cluster));
        }
        if (slot_id != Slot::Melee && slot_id != Slot::Ranged) {
            if (designer.sex == Sex::Female &&
                item.female_worn_model_path_hash) {
                option_hash = item.female_worn_model_path_hash;
                option_path = item.female_worn_model_path;
            } else if (item.worn_model_path_hash) {
                option_hash = item.worn_model_path_hash;
                option_path = item.worn_model_path;
            }
            if (const FlatAssetEntry* authored =
                    FindGlobalModelAssetByPathHash(option_hash)) {
                option_path = authored->full_path;
            } else {
                const std::string item_path = lower_slash(item.model_path);
                const std::string item_leaf = item_path.substr(
                    item_path.find_last_of('/') + 1);
                std::string core = item_leaf;
                if (core.rfind("ob_", 0) == 0) core.replace(0, 3, "ch_");
                const FlatAssetEntry* worn = nullptr;
                for (const FlatAssetEntry& asset : S.all_mdl_files) {
                    const std::string candidate = lower_slash(asset.full_path);
                    if (candidate.find("art/characters/heros/") ==
                            std::string::npos ||
                        candidate.find("morph_") != std::string::npos) {
                        continue;
                    }
                    const std::string leaf = candidate.substr(
                        candidate.find_last_of('/') + 1);
                    if (leaf == core) {
                        worn = &asset;
                        break;
                    }
                }
                if (worn) {
                    option_path = worn->full_path;
                    option_hash = Anim::gdb_model_path_hash(option_path);
                } else if (item_path.find("art/characters/heros/") ==
                           std::string::npos) {
                    continue;
                }
            }
        }
        std::string label = item.display_name.empty() ? item.label
                                                       : item.display_name;
        if (label.empty()) label = leaf_label(item.model_path);
        add_option(designer.slots[static_cast<std::size_t>(slot_id)],
                   {std::move(label), std::move(option_path), option_hash,
                    item.body_areas_covered,
                    item.cluster_sort_layer,
                    std::move(clusters_covered), item.weapon_type,
                    item.name_localized,
                    source.find("pistol") != std::string::npos});
    }

    for (std::size_t i = 0; i < designer.slots.size(); ++i) {
        SlotState& slot = designer.slots[i];
        std::sort(slot.options.begin() + 1, slot.options.end(),
                  [](const Option& a, const Option& b) {
                      return lower_slash(a.label) < lower_slash(b.label);
                  });
        if (selected[i]) {
            for (std::size_t j = 1; j < slot.options.size(); ++j) {
                if (slot.options[j].hash == selected[i]) {
                    slot.selected = static_cast<int>(j);
                    break;
                }
            }
        }
    }

    SlotState& hair = designer.slots[static_cast<std::size_t>(Slot::Hair)];
    if (!preserve_selection && hair.options.size() > 1) hair.selected = 1;

    designer.catalog_model_count = S.all_mdl_files.size();
    designer.catalog_item_count = g_item_details.size();
    designer.catalog_ready = !S.all_mdl_files.empty();
}

const FlatAssetEntry* find_asset(
    const std::function<int(const std::string&)>& score) {
    const FlatAssetEntry* best = nullptr;
    int best_score = 0;
    for (const FlatAssetEntry& asset : S.all_mdl_files) {
        const int current = score(lower_slash(asset.full_path));
        if (current > best_score) {
            best = &asset;
            best_score = current;
        }
    }
    return best;
}

const FlatAssetEntry* hero_body_asset(Sex sex, const char* morph) {
    const std::string gender = sex == Sex::Male ? "heromale" : "herofemale";
    const std::string token = std::string("morph_") + morph;
    return find_asset([&](const std::string& path) {
        if (path.find(gender) == std::string::npos ||
            path.find(token) == std::string::npos) {
            return 0;
        }
        int score = 100;
        if (path.find("ch_" + gender) != std::string::npos) score += 30;
        if (path.find("/body/") != std::string::npos ||
            path.find("_body") != std::string::npos) score += 10;
        return score;
    });
}

void append_unique(std::vector<std::uint32_t>& hashes, std::uint32_t hash) {
    if (hash && std::find(hashes.begin(), hashes.end(), hash) == hashes.end()) {
        hashes.push_back(hash);
    }
}

std::vector<std::uint32_t> base_hero_hashes(Sex sex,
                                            std::uint32_t body_hash,
                                            std::uint32_t face_hash,
                                            std::uint32_t* animation_source) {
    std::vector<std::uint32_t> hashes;
    if (animation_source) *animation_source = 0;
    append_unique(hashes, body_hash);
    append_unique(hashes, face_hash);

    const std::string wanted = sex == Sex::Male
        ? "creatureheromannequinmale" : "creatureheromannequinfemale";
    for (const Gdb::CreatureCatalogEntry& entity : g_global_entity_catalog) {
        const std::string name = lower_slash(entity.name + " " +
                                             entity.display_name);
        if (name.find(wanted) == std::string::npos ||
            name.find("child") != std::string::npos) {
            continue;
        }
        if (animation_source) *animation_source = entity.entity_hash;
        for (std::uint32_t hash : entity.model_hashes) {
            const FlatAssetEntry* asset = FindGlobalModelAssetByPathHash(hash);
            if (!asset) continue;
            const std::string path = lower_slash(asset->full_path);


            if (path.find("ch_eye_") == std::string::npos &&
                has_any(path, {"unclothed", "_morph_", "morph_body"})) {
                continue;
            }
            append_unique(hashes, hash);
        }
        break;
    }

    bool have_left_eye = false;
    bool have_right_eye = false;
    for (std::uint32_t hash : hashes) {
        const FlatAssetEntry* asset = FindGlobalModelAssetByPathHash(hash);
        if (!asset) continue;
        const std::string path = lower_slash(asset->full_path);
        if (path.find("ch_eye_") == std::string::npos) continue;
        const std::string leaf = path.substr(path.find_last_of('/') + 1);
        have_left_eye |= leaf.find("_l.mdl") != std::string::npos;
        have_right_eye |= leaf.find("_r.mdl") != std::string::npos;
    }

    auto add_eye = [&](const char* side) {
        const FlatAssetEntry* found = find_asset([&](const std::string& path) {
            if (path.find("ch_eye_adult_") == std::string::npos ||
                path.find(side) == std::string::npos ||
                has_any(path, {"blind", "turned", "lucien", "franken",
                               "child", "eyecase"})) return 0;
            int score = 40;
            if (path.find("adult_blue") != std::string::npos) score += 30;
            else if (path.find("adult_brown") != std::string::npos) score += 20;
            return score;
        });
        if (found) append_unique(hashes,
            Anim::gdb_model_path_hash(found->full_path));
    };

    if (!have_left_eye) add_eye("_l/");
    if (!have_right_eye) add_eye("_r/");
    return hashes;
}

const Option* selected_option(Slot slot_id) {
    const SlotState& slot = state().slots[static_cast<std::size_t>(slot_id)];
    if (slot.selected < 0 ||
        slot.selected >= static_cast<int>(slot.options.size())) return nullptr;
    const Option& option = slot.options[static_cast<std::size_t>(slot.selected)];
    return option.hash ? &option : nullptr;
}

std::vector<std::string> hide_region_tokens(const std::string& value) {
    std::vector<std::string> result;
    std::size_t begin = 0;
    while (begin <= value.size()) {
        const std::size_t end = value.find(';', begin);
        std::string token = lower_slash(value.substr(
            begin, end == std::string::npos ? std::string::npos : end - begin));
        while (!token.empty() && std::isspace(
            static_cast<unsigned char>(token.front()))) token.erase(token.begin());
        while (!token.empty() && std::isspace(
            static_cast<unsigned char>(token.back()))) token.pop_back();
        if (!token.empty() && token != "(default)") {
            result.push_back(std::move(token));
        }
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return result;
}

void cull_covered_clusters(EntityModels::ResolvedModel& model,
                           const std::vector<AppearanceLayer>& appearance) {
    if (appearance.empty()) return;
    auto layer_for_model = [&](std::uint32_t model_hash) {
        for (const AppearanceLayer& item : appearance) {
            if (item.model_hash == model_hash) return item.sort_layer;
        }

        return -100;
    };

    model.meshes.erase(
        std::remove_if(model.meshes.begin(), model.meshes.end(),
                       [&](const MDLMeshGeom& mesh) {
            const std::vector<std::string> regions =
                hide_region_tokens(mesh.hide_region);
            if (regions.empty()) return false;
            const int source_layer = layer_for_model(mesh.source_model_hash);
            for (const AppearanceLayer& cover : appearance) {
                if (cover.model_hash == mesh.source_model_hash ||
                    cover.sort_layer <= source_layer ||
                    cover.clusters_covered.empty()) {
                    continue;
                }
                for (const std::string& region : regions) {
                    if (cover.clusters_covered.find(region) !=
                        cover.clusters_covered.end()) {
                        return true;
                    }
                }
            }
            return false;
        }),
        model.meshes.end());
}

std::uint32_t authored_morph_target(std::uint32_t original_hash,
                                    std::uint32_t morph_type,
                                    Sex sex) {
    const FlatAssetEntry* original =
        FindGlobalModelAssetByPathHash(original_hash);
    const std::string original_path = original
        ? lower_slash(original->full_path) : std::string();
    const std::size_t slash = original_path.find_last_of('/');
    const std::string original_directory = slash == std::string::npos
        ? std::string() : original_path.substr(0, slash + 1);

    std::uint32_t best_hash = 0;
    int best_score = -100000;
    for (const Gdb::MorphTargetPair& pair : g_hero_morph_targets) {
        if (pair.original_model_hash != original_hash ||
            pair.morph_type != morph_type) continue;
        const FlatAssetEntry* target =
            FindGlobalModelAssetByPathHash(pair.target_model_hash);
        if (!target) continue;
        const std::string target_path = lower_slash(target->full_path);
        if (opposite_sex_asset(target_path, sex)) continue;
        int score = 100;
        if (!original_directory.empty() &&
            target_path.rfind(original_directory, 0) == 0) score += 30;
        if (sex == Sex::Female &&
            has_any(target_path, {"female", "herofemale", "fhero"})) {
            score += 10;
        } else if (sex == Sex::Male &&
                   has_any(target_path, {"heromale", "hero_male"})) {
            score += 10;
        }
        if (score > best_score) {
            best_score = score;
            best_hash = pair.target_model_hash;
        }
    }
    return best_hash;
}

bool blend_geometry(EntityModels::ResolvedModel& base,
                    const std::vector<MDLMeshGeom>& neutral,
                    const EntityModels::ResolvedModel& target,
                    float weight) {
    if (weight <= 0.0001f || base.meshes.size() != target.meshes.size() ||
        base.meshes.size() != neutral.size()) {
        return false;
    }
    bool blended = false;
    for (std::size_t mesh_index = 0; mesh_index < base.meshes.size();
         ++mesh_index) {
        MDLMeshGeom& dst = base.meshes[mesh_index];
        const MDLMeshGeom& origin = neutral[mesh_index];
        const MDLMeshGeom& src = target.meshes[mesh_index];
        if (dst.positions.size() != src.positions.size() ||
            dst.positions.size() != origin.positions.size() ||
            dst.normals.size() != src.normals.size() ||
            dst.normals.size() != origin.normals.size()) continue;
        for (std::size_t i = 0; i < dst.positions.size(); ++i) {
            dst.positions[i] +=
                (src.positions[i] - origin.positions[i]) * weight;
        }
        for (std::size_t i = 0; i + 2 < dst.normals.size(); i += 3) {
            dst.normals[i + 0] +=
                (src.normals[i + 0] - origin.normals[i + 0]) * weight;
            dst.normals[i + 1] +=
                (src.normals[i + 1] - origin.normals[i + 1]) * weight;
            dst.normals[i + 2] +=
                (src.normals[i + 2] - origin.normals[i + 2]) * weight;
            const float length = std::sqrt(
                dst.normals[i + 0] * dst.normals[i + 0] +
                dst.normals[i + 1] * dst.normals[i + 1] +
                dst.normals[i + 2] * dst.normals[i + 2]);
            if (length > 1.0e-6f) {
                dst.normals[i + 0] /= length;
                dst.normals[i + 1] /= length;
                dst.normals[i + 2] /= length;
            }
        }
        blended = true;
    }
    return blended;
}

bool parse_triplet(const std::string& line, float value[3]) {
    std::stringstream stream(line);
    char comma_a = 0;
    char comma_b = 0;
    return bool(stream >> value[0] >> comma_a >> value[1] >> comma_b >> value[2]) &&
           comma_a == ',' && comma_b == ',';
}

bool parse_labeled_float(const std::string& line, const char* label,
                         float& value) {
    const std::string prefix = std::string(label) + ":";
    if (line.rfind(prefix, 0) != 0) return false;
    try {
        value = std::stof(line.substr(prefix.size()));
        return true;
    } catch (...) {
        return false;
    }
}

std::vector<BoneOffset> load_skeletal_morph(Sex sex, const char* kind) {
    std::vector<BoneOffset> result;
    const auto bank_path = find_bnk_by_filename("skeletalmorphs.bnk");
    if (!bank_path) return result;
    try {
        BNKReader reader(*bank_path);
        const auto& files = reader.list_files();
        int selected = -1;
        for (std::size_t i = 0; i < files.size(); ++i) {
            const std::string path = lower_slash(files[i].name);
            if (path.find(kind) == std::string::npos ||
                path.find(".mof") == std::string::npos) continue;
            const bool female_file = path.find("fhero") != std::string::npos;
            if ((sex == Sex::Female) != female_file) continue;
            selected = static_cast<int>(i);
            break;
        }
        if (selected < 0) return result;
        const std::vector<std::uint8_t> bytes =
            reader.extract_index_bytes(selected);
        std::stringstream lines(std::string(
            reinterpret_cast<const char*>(bytes.data()), bytes.size()));
        std::string line;
        while (std::getline(lines, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.rfind("ShadowBone:", 0) != 0) continue;
            BoneOffset offset;
            offset.name = line.substr(11);
            std::string first;
            if (!std::getline(lines, first)) {
                break;
            }
            if (!first.empty() && first.back() == '\r') first.pop_back();

            bool parsed = false;
            if (first.rfind("PosX:", 0) == 0) {
                std::array<std::string, 5> remaining;
                bool have_lines = true;
                for (std::string& value : remaining) {
                    if (!std::getline(lines, value)) {
                        have_lines = false;
                        break;
                    }
                    if (!value.empty() && value.back() == '\r') value.pop_back();
                }
                parsed = have_lines &&
                    parse_labeled_float(first, "PosX", offset.position[0]) &&
                    parse_labeled_float(remaining[0], "PosY", offset.position[1]) &&
                    parse_labeled_float(remaining[1], "PosZ", offset.position[2]) &&
                    parse_labeled_float(remaining[2], "SclX", offset.scale[0]) &&
                    parse_labeled_float(remaining[3], "SclY", offset.scale[1]) &&
                    parse_labeled_float(remaining[4], "SclZ", offset.scale[2]);
            } else {
                std::string scale;
                if (!std::getline(lines, scale)) break;
                if (!scale.empty() && scale.back() == '\r') scale.pop_back();
                parsed = parse_triplet(first, offset.position) &&
                         parse_triplet(scale, offset.scale);
            }
            if (parsed) {


                offset.position[0] *= -0.1f;
                offset.position[1] *= 0.1f;
                offset.position[2] *= 0.1f;
                result.push_back(std::move(offset));
            }
        }
    } catch (...) {
        result.clear();
    }
    return result;
}

void apply_skeletal_morphs(
    MDLInfo& info,
    std::initializer_list<
        std::pair<const std::vector<BoneOffset>*, float>> morphs) {
    if (info.BoneTransforms.size() != info.Bones.size()) return;
    std::unordered_map<std::string, std::size_t> by_name;
    for (std::size_t i = 0; i < info.Bones.size(); ++i) {
        by_name.emplace(lower_slash(info.Bones[i].Name), i);
    }

    struct AccumulatedOffset {
        float position[3]{};
        float scale[3]{1.0f, 1.0f, 1.0f};
    };
    std::unordered_map<std::size_t, AccumulatedOffset> accumulated;
    for (const auto& weighted : morphs) {
        const std::vector<BoneOffset>* offsets = weighted.first;
        const float weight = weighted.second;
        if (!offsets || offsets->empty() || weight <= 0.0001f) continue;
        for (const BoneOffset& offset : *offsets) {
            const auto found = by_name.find(lower_slash(offset.name));
            if (found == by_name.end()) continue;
            AccumulatedOffset& value = accumulated[found->second];
            for (int axis = 0; axis < 3; ++axis) {
                value.position[axis] += offset.position[axis] * weight;
                value.scale[axis] *=
                    1.0f + (offset.scale[axis] - 1.0f) * weight;
            }
        }
    }

    for (const auto& item : accumulated) {
        std::vector<float>& transform = info.BoneTransforms[item.first];
        if (transform.size() < 10) continue;
        const AccumulatedOffset& offset = item.second;
        for (int axis = 0; axis < 3; ++axis) {





            transform[4 + axis] =
                transform[4 + axis] * offset.scale[axis] +
                offset.position[axis];
            transform[7 + axis] *= offset.scale[axis];
        }
    }
}

using Matrix4 = std::array<float, 16>;

Matrix4 identity_matrix() {
    Matrix4 result{};
    result[0] = result[5] = result[10] = result[15] = 1.0f;
    return result;
}

Matrix4 multiply_matrix(const Matrix4& left, const Matrix4& right) {
    Matrix4 result{};
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            for (int k = 0; k < 4; ++k) {
                result[static_cast<std::size_t>(row * 4 + column)] +=
                    left[static_cast<std::size_t>(row * 4 + k)] *
                    right[static_cast<std::size_t>(k * 4 + column)];
            }
        }
    }
    return result;
}

Matrix4 bone_matrix(const std::vector<float>& transform) {
    if (transform.size() < 10) return identity_matrix();
    float x = transform[0];
    float y = transform[1];
    float z = transform[2];
    float w = transform[3];
    const float length = std::sqrt(x * x + y * y + z * z + w * w);
    if (length > 1.0e-7f) {
        x /= length;
        y /= length;
        z /= length;
        w /= length;
    } else {
        x = y = z = 0.0f;
        w = 1.0f;
    }

    Matrix4 result = identity_matrix();
    result[0] = (1.0f - 2.0f * (y * y + z * z)) * transform[7];
    result[1] = (2.0f * (x * y + z * w)) * transform[7];
    result[2] = (2.0f * (x * z - y * w)) * transform[7];
    result[4] = (2.0f * (x * y - z * w)) * transform[8];
    result[5] = (1.0f - 2.0f * (x * x + z * z)) * transform[8];
    result[6] = (2.0f * (y * z + x * w)) * transform[8];
    result[8] = (2.0f * (x * z + y * w)) * transform[9];
    result[9] = (2.0f * (y * z - x * w)) * transform[9];
    result[10] = (1.0f - 2.0f * (x * x + y * y)) * transform[9];
    result[12] = transform[4];
    result[13] = transform[5];
    result[14] = transform[6];
    return result;
}

std::vector<Matrix4> bone_world_matrices(const MDLInfo& info) {
    const std::size_t count = std::min(info.Bones.size(),
                                       info.BoneTransforms.size());
    std::vector<Matrix4> local(count);
    std::vector<Matrix4> world(count, identity_matrix());
    std::vector<std::uint8_t> done(count, 0);
    for (std::size_t i = 0; i < count; ++i) {
        local[i] = bone_matrix(info.BoneTransforms[i]);
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (done[i]) continue;
        std::vector<std::size_t> chain;
        int current = static_cast<int>(i);
        while (current >= 0 && static_cast<std::size_t>(current) < count &&
               !done[static_cast<std::size_t>(current)]) {
            chain.push_back(static_cast<std::size_t>(current));
            current = info.Bones[static_cast<std::size_t>(current)].ParentID;
        }
        Matrix4 accumulated = current >= 0 &&
            static_cast<std::size_t>(current) < count
            ? world[static_cast<std::size_t>(current)] : identity_matrix();
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            accumulated = multiply_matrix(local[*it], accumulated);
            world[*it] = accumulated;
            done[*it] = 1;
        }
    }
    return world;
}

bool inverse_matrix(const Matrix4& input, Matrix4& output) {
    float augmented[4][8]{};
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            augmented[row][column] =
                input[static_cast<std::size_t>(row * 4 + column)];
        }
        augmented[row][row + 4] = 1.0f;
    }
    for (int column = 0; column < 4; ++column) {
        int pivot = column;
        for (int row = column + 1; row < 4; ++row) {
            if (std::fabs(augmented[row][column]) >
                std::fabs(augmented[pivot][column])) pivot = row;
        }
        if (std::fabs(augmented[pivot][column]) < 1.0e-8f) return false;
        if (pivot != column) {
            for (int k = 0; k < 8; ++k) {
                std::swap(augmented[pivot][k], augmented[column][k]);
            }
        }
        const float divisor = augmented[column][column];
        for (float& value : augmented[column]) value /= divisor;
        for (int row = 0; row < 4; ++row) {
            if (row == column) continue;
            const float factor = augmented[row][column];
            for (int k = 0; k < 8; ++k) {
                augmented[row][k] -= factor * augmented[column][k];
            }
        }
    }
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            output[static_cast<std::size_t>(row * 4 + column)] =
                augmented[row][column + 4];
        }
    }
    return true;
}

void transform_point(const Matrix4& matrix, const float input[3],
                     float output[3]) {
    output[0] = input[0] * matrix[0] + input[1] * matrix[4] +
                input[2] * matrix[8] + matrix[12];
    output[1] = input[0] * matrix[1] + input[1] * matrix[5] +
                input[2] * matrix[9] + matrix[13];
    output[2] = input[0] * matrix[2] + input[1] * matrix[6] +
                input[2] * matrix[10] + matrix[14];
}

void transform_direction(const Matrix4& matrix, const float input[3],
                         float output[3]) {
    output[0] = input[0] * matrix[0] + input[1] * matrix[4] +
                input[2] * matrix[8];
    output[1] = input[0] * matrix[1] + input[1] * matrix[5] +
                input[2] * matrix[9];
    output[2] = input[0] * matrix[2] + input[1] * matrix[6] +
                input[2] * matrix[10];
}

void apply_skeletal_geometry(std::vector<MDLMeshGeom>& meshes,
                             const MDLInfo& neutral,
                             const MDLInfo& morphed) {
    const std::vector<Matrix4> neutral_world = bone_world_matrices(neutral);
    const std::vector<Matrix4> morphed_world = bone_world_matrices(morphed);
    const std::size_t bone_count = std::min(neutral_world.size(),
                                            morphed_world.size());
    if (bone_count == 0) return;
    std::vector<Matrix4> deltas(bone_count, identity_matrix());
    for (std::size_t bone = 0; bone < bone_count; ++bone) {
        Matrix4 inverse{};
        if (inverse_matrix(neutral_world[bone], inverse)) {
            deltas[bone] = multiply_matrix(inverse, morphed_world[bone]);
        }
    }

    for (MDLMeshGeom& mesh : meshes) {
        const std::size_t vertex_count = mesh.positions.size() / 3;
        if (mesh.bone_ids.size() < vertex_count * 4 ||
            mesh.bone_weights.size() < vertex_count * 4) continue;
        for (std::size_t vertex = 0; vertex < vertex_count; ++vertex) {
            const float position[3] = {
                mesh.positions[vertex * 3],
                mesh.positions[vertex * 3 + 1],
                mesh.positions[vertex * 3 + 2],
            };
            const bool have_normal = mesh.normals.size() >= vertex * 3 + 3;
            const float normal[3] = {
                have_normal ? mesh.normals[vertex * 3] : 0.0f,
                have_normal ? mesh.normals[vertex * 3 + 1] : 0.0f,
                have_normal ? mesh.normals[vertex * 3 + 2] : 0.0f,
            };
            float blended_position[3]{};
            float blended_normal[3]{};
            float total_weight = 0.0f;
            for (std::size_t influence = 0; influence < 4; ++influence) {
                const std::size_t offset = vertex * 4 + influence;
                const float weight = mesh.bone_weights[offset];
                const std::size_t bone = mesh.bone_ids[offset];
                if (weight <= 0.0f || bone >= bone_count) continue;
                float point[3];
                float direction[3];
                transform_point(deltas[bone], position, point);
                transform_direction(deltas[bone], normal, direction);
                for (int axis = 0; axis < 3; ++axis) {
                    blended_position[axis] += point[axis] * weight;
                    blended_normal[axis] += direction[axis] * weight;
                }
                total_weight += weight;
            }
            if (total_weight <= 1.0e-6f) continue;
            for (int axis = 0; axis < 3; ++axis) {
                mesh.positions[vertex * 3 + static_cast<std::size_t>(axis)] =
                    blended_position[axis] / total_weight;
            }
            if (have_normal) {
                const float length = std::sqrt(
                    blended_normal[0] * blended_normal[0] +
                    blended_normal[1] * blended_normal[1] +
                    blended_normal[2] * blended_normal[2]);
                if (length > 1.0e-6f) {
                    for (int axis = 0; axis < 3; ++axis) {
                        mesh.normals[vertex * 3 +
                            static_cast<std::size_t>(axis)] =
                            blended_normal[axis] / length;
                    }
                }
            }
        }
    }
}

float morph_weight(const MorphState& morph, MorphKind kind) {
    switch (kind) {
        case MorphKind::Strong: return morph.strong;
        case MorphKind::Fat: return morph.fat;
        case MorphKind::Young: return morph.young;
        case MorphKind::Old: return morph.old;
        case MorphKind::Impure: return morph.impure;
        case MorphKind::EvilPure: return morph.evil_pure;
        case MorphKind::EvilImpure: return morph.evil_impure;
    }
    return 0.0f;
}

EntityModels::ResolvedModel build_morphed_model(
    const MorphCache& cache, const MorphState& morph) {
    EntityModels::ResolvedModel model = cache.neutral;
    const std::vector<MDLMeshGeom>& neutral = cache.neutral.meshes;
    const MDLInfo& neutral_rig = cache.neutral.info;
    for (const MorphVariant& variant : cache.variants) {
        const float weight = morph_weight(morph, variant.kind);
        if (weight > 0.0001f) {
            blend_geometry(model, neutral, variant.model, weight);
        }
    }

    apply_skeletal_morphs(
        model.info,
        {{&cache.strong, morph.strong},
         {&cache.fat, morph.fat},
         {&cache.tall, morph.tall}});
    if (morph.strong > 0.0001f || morph.fat > 0.0001f ||
        morph.tall > 0.0001f) {
        apply_skeletal_geometry(model.meshes, neutral_rig, model.info);
    }
    cull_covered_clusters(model, cache.appearance);
    return model;
}

void publish_cached_preview(bool rebuild_gpu) {
    DesignerState& designer = state();
    if (!designer.morph_cache) return;
    const MorphCache& cache = *designer.morph_cache;
    EntityModels::ResolvedModel model =
        build_morphed_model(cache, designer.morph);
    if (model.meshes.empty()) {
        designer.status = "Hero preview contained no geometry.";
        return;
    }
    designer.status.clear();
    if (rebuild_gpu) {
        ContentTabs::StoreHeroModel(
            model.info, model.meshes, model.primary_model_path,
            model.primary_model_hash, cache.animation_source_hash,
            cache.animation_model_hashes);
    } else {
        ContentTabs::UpdateHeroModel(
            model.info, model.meshes, model.primary_model_path,
            model.primary_model_hash, cache.animation_source_hash,
            cache.animation_model_hashes);
    }
}

void request_preview() {
    DesignerState& designer = state();
    if (designer.loading) {
        designer.preview_queued = true;
        return;
    }
    designer.preview_queued = false;
    if (!designer.catalog_ready) {
        designer.status = "Model catalog is still being indexed.";
        return;
    }

    const Sex sex = designer.sex;
    const FlatAssetEntry* body = hero_body_asset(sex, "body");
    const FlatAssetEntry* face = hero_body_asset(sex, "thin");
    if (!body || !face) {
        designer.status = sex == Sex::Male
            ? "Could not find the male Hero body and face morph models."
            : "Could not find the female Hero body and face morph models.";
        return;
    }
    const std::uint32_t body_hash =
        Anim::gdb_model_path_hash(body->full_path);
    const std::uint32_t face_hash =
        Anim::gdb_model_path_hash(face->full_path);
    std::uint32_t animation_source_hash = 0;
    std::vector<std::uint32_t> hashes = base_hero_hashes(
        sex, body_hash, face_hash, &animation_source_hash);
    const std::vector<std::uint32_t> animation_model_hashes = hashes;
    std::vector<AppearanceLayer> appearance;

    struct SelectedPart {
        const Option* option;
    };
    std::vector<SelectedPart> selected_parts;
    for (Slot slot_id : {Slot::Hair, Slot::Beard, Slot::Moustache,
                         Slot::Hat, Slot::Coat, Slot::Shirt, Slot::Gloves,
                         Slot::Trousers, Slot::Boots, Slot::Mask, Slot::Suit,
                         Slot::Accessories, Slot::Melee, Slot::Ranged}) {
        if (sex == Sex::Female &&
            (slot_id == Slot::Beard || slot_id == Slot::Moustache)) continue;
        if (const Option* option = selected_option(slot_id)) {
            selected_parts.push_back({option});
        }
    }
    for (const SelectedPart& selected : selected_parts) {
        bool occupied_by_outer_item = false;
        if (selected.option->body_areas_covered) {
            for (const SelectedPart& other : selected_parts) {
                if (other.option == selected.option ||
                    other.option->cluster_sort_layer <=
                        selected.option->cluster_sort_layer) continue;
                if ((other.option->body_areas_covered &
                     selected.option->body_areas_covered) != 0) {
                    occupied_by_outer_item = true;
                    break;
                }
            }
        }
        if (occupied_by_outer_item) continue;

        append_unique(hashes, selected.option->hash);
        AppearanceLayer layer;
        layer.model_hash = selected.option->hash;
        layer.sort_layer = selected.option->cluster_sort_layer;
        layer.clusters_covered.insert(
            selected.option->clusters_covered.begin(),
            selected.option->clusters_covered.end());
        appearance.push_back(std::move(layer));
    }

    EntityModels::ResolveOptions options;
    options.preferred_primary_hash = body_hash;
    if (const Option* melee = selected_option(Slot::Melee)) {
        std::vector<std::string> bones;
        if (melee->weapon_type == 2) {
            bones = {"Carry_SheathWeaponFront_DummyObject",
                     "Carry_SheathWeaponBack_DummyObject"};
        } else {
            bones = {"Carry_SheathWeaponBack_DummyObject",
                     "Carry_SheathWeaponFront_DummyObject"};
        }
        options.attachments.push_back({
            melee->hash, std::move(bones)});
    }
    if (const Option* ranged = selected_option(Slot::Ranged)) {
        std::vector<std::string> bones;
        if (ranged->is_pistol) {
            bones = {"Carry_SheathWeaponFront_DummyObject",
                     "Carry_SheathRangedWeaponBack_DummyObject"};
        } else {
            bones = {"Carry_SheathRangedWeaponBack_DummyObject"};
        }
        options.attachments.push_back({
            ranged->hash, std::move(bones)});
    }

    struct NamedTarget { const char* name; MorphKind kind; };
    const std::array<NamedTarget, 5> targets{{
        {"young", MorphKind::Young}, {"old", MorphKind::Old},
        {"impure", MorphKind::Impure},
        {"evilpure", MorphKind::EvilPure},
        {"evilimpure", MorphKind::EvilImpure},
    }};

    struct TargetRequest {
        MorphKind kind = MorphKind::Young;
        std::vector<std::uint32_t> hashes;
    };
    std::vector<TargetRequest> target_requests;
    auto add_authored_target = [&](MorphKind kind,
                                   std::uint32_t morph_type) {
        std::vector<std::uint32_t> target_hashes = hashes;
        bool replaced = false;
        for (std::uint32_t& hash : target_hashes) {
            const std::uint32_t target =
                authored_morph_target(hash, morph_type, sex);
            if (!target) continue;
            hash = target;
            replaced = true;
        }
        if (replaced) {
            target_requests.push_back(
                {kind, std::move(target_hashes)});
        }
    };



    add_authored_target(MorphKind::Strong, 5);
    add_authored_target(MorphKind::Fat, 4);
    for (const NamedTarget& target : targets) {
        if (const FlatAssetEntry* asset = hero_body_asset(sex, target.name)) {
            std::vector<std::uint32_t> target_hashes = hashes;
            const auto face_it = std::find(target_hashes.begin(),
                                           target_hashes.end(), face_hash);
            if (face_it == target_hashes.end()) continue;
            *face_it = Anim::gdb_model_path_hash(asset->full_path);
            target_requests.push_back(
                {target.kind, std::move(target_hashes)});
        }
    }

    const std::uint64_t request = ++designer.next_request;
    designer.loading = true;
    designer.morph_cache.reset();
    designer.status = "Building Hero preview...";
    std::thread([request, sex, hashes = std::move(hashes), body_hash,
                 animation_source_hash, animation_model_hashes,
                 appearance = std::move(appearance),
                 options = std::move(options),
                 target_requests = std::move(target_requests)]() mutable {
        PreviewResult completion;
        completion.request = request;
        completion.cache = std::make_shared<MorphCache>();
        MorphCache& cache = *completion.cache;
        cache.appearance = std::move(appearance);
        cache.animation_source_hash = animation_source_hash;
        cache.animation_model_hashes = animation_model_hashes;
        if (!EntityModels::Resolve(hashes, cache.neutral,
                                   &completion.error, &options)) {
            if (completion.error.empty()) completion.error =
                "No renderable Hero model parts resolved.";
        } else {
            for (const TargetRequest& target : target_requests) {
                EntityModels::ResolveOptions target_options = options;
                EntityModels::ResolvedModel target_model;
                std::string ignored;
                if (EntityModels::Resolve(target.hashes, target_model,
                                          &ignored, &target_options)) {
                    cache.variants.push_back(
                        {target.kind, std::move(target_model)});
                }
            }
            cache.strong = load_skeletal_morph(sex, "strong");
            cache.fat = load_skeletal_morph(sex, "fat");
            cache.tall = load_skeletal_morph(sex, "tall");
        }

        DesignerState& designer = state();
        std::lock_guard<std::mutex> lock(designer.completion_mutex);
        designer.completions.push_back(std::move(completion));
    }).detach();
}

void apply_completions() {
    DesignerState& designer = state();
    std::vector<PreviewResult> completions;
    {
        std::lock_guard<std::mutex> lock(designer.completion_mutex);
        completions.swap(designer.completions);
    }
    for (PreviewResult& completion : completions) {
        if (completion.request != designer.next_request.load()) continue;
        designer.loading = false;
        if (designer.preview_queued) continue;
        if (!completion.error.empty() || !completion.cache ||
            completion.cache->neutral.meshes.empty()) {
            designer.status = completion.error.empty()
                ? "Hero preview contained no geometry." : completion.error;
            OutputLog::warn("hero designer: " + designer.status);
            continue;
        }
        designer.morph_cache = std::move(completion.cache);
        publish_cached_preview(true);
    }
    if (!designer.loading && designer.preview_queued) request_preview();
}

bool sex_button(const char* icon, const char* text, bool selected,
                const ImVec2& size) {
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button,
                              ImGui::GetStyleColorVec4(ImGuiCol_TabActive));
    }
    const std::string label = std::string(icon) + "  " + text;
    const bool clicked = ImGui::Button(label.c_str(), size);
    if (selected) ImGui::PopStyleColor();
    return clicked;
}

bool draw_slot_combo(Slot slot_id) {
    SlotState& slot = state().slots[static_cast<std::size_t>(slot_id)];
    const char* preview = "None";
    if (slot.selected >= 0 &&
        slot.selected < static_cast<int>(slot.options.size())) {
        preview = slot.options[static_cast<std::size_t>(slot.selected)]
                      .label.c_str();
    }
    ImGui::TextUnformatted(slot.label);
    ImGui::SetNextItemWidth(-1.0f);
    bool changed = false;
    const std::string id = "##hero_" +
        std::to_string(static_cast<std::size_t>(slot_id));
    if (ImGui::BeginCombo(id.c_str(), preview)) {
        for (std::size_t i = 0; i < slot.options.size(); ++i) {
            const bool selected = slot.selected == static_cast<int>(i);
            const std::string option_id = slot.options[i].label +
                "##hero_option_" + std::to_string(i);
            if (ImGui::Selectable(option_id.c_str(), selected)) {
                slot.selected = static_cast<int>(i);
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool morph_slider(const char* label, float& value) {
    ImGui::SetNextItemWidth(-1.0f);
    return ImGui::SliderFloat(label, &value, 0.0f, 1.0f, "%.2f");
}

}

void Open() {
    DesignerState& designer = state();
    ContentTabs::OpenHero();
    const bool catalog_changed =
        designer.catalog_model_count != S.all_mdl_files.size() ||
        designer.catalog_item_count != g_item_details.size();
    if (!designer.catalog_ready || catalog_changed) {
        rebuild_catalog(designer.catalog_ready);
    }
    if (!designer.opened) {
        designer.opened = true;
        request_preview();
    }
}

void DrawControls() {
    DesignerState& designer = state();
    apply_completions();
    const bool catalog_changed =
        designer.catalog_model_count != S.all_mdl_files.size() ||
        designer.catalog_item_count != g_item_details.size();
    if (catalog_changed) {
        rebuild_catalog(true);
        if (designer.catalog_ready) request_preview();
    }

    ImGui::TextUnformatted("Hero Designer");

    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float half = (ImGui::GetContentRegionAvail().x - gap) * 0.5f;
    if (sex_button(ICON_FA_MARS, "Male", designer.sex == Sex::Male,
                   ImVec2(half, 0.0f)) && designer.sex != Sex::Male) {
        designer.sex = Sex::Male;
        rebuild_catalog(false);
        request_preview();
    }
    ImGui::SameLine();
    if (sex_button(ICON_FA_VENUS, "Female", designer.sex == Sex::Female,
                   ImVec2(half, 0.0f)) && designer.sex != Sex::Female) {
        designer.sex = Sex::Female;
        rebuild_catalog(false);
        request_preview();
    }

    ImGui::BeginChild("##hero_designer_scroll", ImVec2(0, 0), false);
    bool changed = false;
    ImGui::SeparatorText("Hair");
    changed |= draw_slot_combo(Slot::Hair);
    if (designer.sex == Sex::Male) {
        changed |= draw_slot_combo(Slot::Beard);
        changed |= draw_slot_combo(Slot::Moustache);
    }

    ImGui::SeparatorText("Clothing");
    for (Slot slot_id : {Slot::Hat, Slot::Coat, Slot::Shirt, Slot::Gloves,
                         Slot::Trousers, Slot::Boots, Slot::Mask, Slot::Suit,
                         Slot::Accessories}) {
        changed |= draw_slot_combo(slot_id);
    }

    ImGui::SeparatorText("Weapons");
    changed |= draw_slot_combo(Slot::Melee);
    changed |= draw_slot_combo(Slot::Ranged);

    ImGui::SeparatorText("Morphs");
    bool morph_changed = false;
    morph_changed |= morph_slider("Strength", designer.morph.strong);
    morph_changed |= morph_slider("Fatness", designer.morph.fat);
    morph_changed |= morph_slider("Height", designer.morph.tall);
    morph_changed |= morph_slider("Young", designer.morph.young);
    morph_changed |= morph_slider("Old", designer.morph.old);
    morph_changed |= morph_slider("Impure", designer.morph.impure);
    morph_changed |= morph_slider("Evil / pure", designer.morph.evil_pure);
    morph_changed |= morph_slider("Evil / impure", designer.morph.evil_impure);

    if (changed) request_preview();
    if (morph_changed && !changed && designer.morph_cache &&
        !designer.loading) {
        publish_cached_preview(false);
    }
    ImGui::Spacing();
    if (!designer.status.empty()) {
        if (designer.loading) {
            ImGui::TextDisabled("%s", designer.status.c_str());
        } else {
            ImGui::TextWrapped("%s", designer.status.c_str());
        }
    }
    if (ImGui::Button("Rebuild preview", ImVec2(-1, 0))) {
        request_preview();
    }
    ImGui::EndChild();
}

}
