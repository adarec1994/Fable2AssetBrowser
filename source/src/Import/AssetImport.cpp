#include "AssetImport.h"

#include "GlbImport.h"
#include "ImageLoad.h"
#include "MdlWriter.h"

#include "Entity/StaticPropAuthoring.h"
#include "MDL/ModelParser.h"
#include "textures/TexParser.h"
#include "UI/OutputLog.h"
#include "Utilities/DebugTrace.h"
#include "Utilities/Progress.h"
#include "Utilities/GameBackup.h"
#include "Utilities/State.h"
#include "Utilities/Utils.h"

#include "BNKCore.cpp"
#include "Level/IO/BnkWriter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>


extern void tree_register_injected_file(const std::string& bnk_path,
                                        const std::string& virtual_path);

namespace AssetImport {

namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

std::string normalize_folder(std::string f) {
    std::replace(f.begin(), f.end(), '/', '\\');
    while (!f.empty() && (f.back() == '\\')) f.pop_back();
    while (!f.empty() && (f.front() == '\\')) f.erase(f.begin());
    return to_lower(f);
}

struct PendingEntry {
    std::string bnk_filename;   
    std::string virtual_path;   
    std::vector<uint8_t> payload;
};



bool inject_entries(const std::vector<PendingEntry>& entries,
                    std::vector<std::pair<std::string, std::string>>& injected,
                    std::string& err)
{
    struct Target {
        std::string path;
        std::vector<BnkWriter::EntryReplacement> repls;
        std::vector<BnkWriter::EntryAddition> adds;
    };
    std::map<std::string, Target> targets;

    for (const auto& e : entries) {
        auto p = find_bnk_by_filename(e.bnk_filename);
        if (!p) {
            err = e.bnk_filename + " was not found in the loaded game data";
            return false;
        }
        Target& t = targets[*p];
        t.path = *p;
        const std::string key = to_lower(e.virtual_path);
        int existing = BnkCache::find_index(t.path, [&]{
            std::string k = key;
            std::replace(k.begin(), k.end(), '\\', '/');
            return k;
        }());
        if (existing >= 0) {
            BnkWriter::EntryReplacement r;
            r.file_index = existing;
            r.payload = e.payload;
            t.repls.push_back(std::move(r));
        } else {
            BnkWriter::EntryAddition a;
            a.name = e.virtual_path;
            a.payload = e.payload;
            t.adds.push_back(std::move(a));
        }
        injected.emplace_back(t.path, e.virtual_path);
    }

    
    struct Snap { std::string path; std::vector<uint8_t> bytes; };
    std::vector<Snap> snaps;
    for (auto& [path, t] : targets) {
        std::ifstream f(path, std::ios::binary);
        if (!f) { err = "could not read " + path; return false; }
        Snap s;
        s.path = path;
        s.bytes.assign((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
        snaps.push_back(std::move(s));
    }

    size_t done = 0;
    for (auto& [path, t] : targets) {
        BnkCache::invalidate(path);
        if (!BnkWriter::RebuildWithChanges(path, t.repls, t.adds, err)) {
            err = std::filesystem::path(path).filename().string() + ": " + err;
            break;
        }
        ++done;
    }

    if (done != targets.size()) {
        for (const Snap& s : snaps) {
            std::ofstream f(s.path, std::ios::binary | std::ios::trunc);
            if (f) f.write((const char*)s.bytes.data(),
                           (std::streamsize)s.bytes.size());
            BnkCache::invalidate(s.path);
        }
        return false;
    }

    for (auto& [path, t] : targets) BnkCache::invalidate(path);
    return true;
}

void register_injected(const std::vector<std::pair<std::string, std::string>>& injected)
{
    for (const auto& [bnk, vpath] : injected)
        tree_register_injected_file(bnk, vpath);
}



bool verify_mdl(const MdlWriter::BuiltMdl& built, std::string& err)
{
    std::vector<unsigned char> whole;
    whole.reserve(built.header.size() + built.body.size());
    whole.insert(whole.end(), built.header.begin(), built.header.end());
    whole.insert(whole.end(), built.body.begin(), built.body.end());
    std::vector<MDLMeshGeom> geoms;
    if (!build_mdl_engine_geometry(whole, geoms) || geoms.empty()) {
        err = "internal: generated .mdl failed the engine-walker round-trip";
        return false;
    }
    return true;
}

bool verify_tex(const TexWriter::BuiltTex& built, std::string& err)
{
    std::vector<unsigned char> whole;
    whole.insert(whole.end(), built.header.begin(), built.header.end());
    whole.insert(whole.end(), built.mip0.begin(), built.mip0.end());
    whole.insert(whole.end(), built.body.begin(), built.body.end());
    TexInfo ti;
    if (!parse_tex_info(whole, ti) || ti.Mips.empty()) {
        err = "internal: generated .tex failed parse_tex_info";
        return false;
    }
    if (ti.TextureWidth != built.width || ti.TextureHeight != built.height ||
        ti.Mips.size() != built.mip_count) {
        err = "internal: generated .tex round-trip mismatch";
        return false;
    }
    return true;
}

void queue_tex(const std::string& vpath, const TexWriter::BuiltTex& built,
               std::vector<PendingEntry>& pending)
{
    pending.push_back({"globals_texture_headers.bnk", vpath, built.header});
    if (!built.mip0.empty())
        pending.push_back({"1024mip0_textures.bnk", vpath, built.mip0});
    pending.push_back({"globals_textures.bnk", vpath, built.body});
}

std::string normalized_path(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    std::replace(value.begin(), value.end(), '\\', '/');
    return value;
}

int texture_bank_role(const std::string& path) {
    const std::string leaf = to_lower(
        std::filesystem::path(path).filename().string());
    const bool header = leaf.find("header") != std::string::npos &&
                        leaf.find("texture") != std::string::npos;
    const bool mip0 = leaf.find("1024mip0") != std::string::npos &&
                      leaf.find("texture") != std::string::npos;
    const bool body = leaf.find("texture") != std::string::npos &&
                      !header && !mip0;
    if (header) return 1;
    if (mip0) return 2;
    if (body) return 3;
    return 4;
}

std::string texture_bank_family(const std::string& path) {
    std::string leaf = to_lower(
        std::filesystem::path(path).filename().string());
    if (leaf.size() >= 4 &&
        leaf.compare(leaf.size() - 4, 4, ".bnk") == 0) {
        leaf.resize(leaf.size() - 4);
    }
    for (const std::string suffix :
         {"_texture_headers", "_textures", "_texture", "_headers"}) {
        if (leaf.size() > suffix.size() &&
            leaf.compare(leaf.size() - suffix.size(), suffix.size(),
                         suffix) == 0) {
            leaf.resize(leaf.size() - suffix.size());
            break;
        }
    }
    return leaf;
}

struct TexturePart {
    std::string path;
    int index = -1;
    int rank = 1000;
};

struct TextureTargets {
    std::string virtual_path;
    TexturePart header;
    TexturePart mip0;
    TexturePart body;
};

bool resolve_texture_targets(const std::string& preferred_bnk,
                             int preferred_index, TextureTargets& out,
                             std::string& err) {
    const auto preferred_nested_it =
        S.nested_bnk_parents.find(preferred_bnk);
    const bool preferred_is_nested =
        preferred_nested_it != S.nested_bnk_parents.end();
    const std::string preferred_nested_parent =
        preferred_is_nested ? preferred_nested_it->second : std::string();
    DebugTrace::log(
        "texture-replace: resolve selected='%s' index=%d nested=%d parent='%s'",
        preferred_bnk.c_str(), preferred_index, preferred_is_nested ? 1 : 0,
        preferred_nested_parent.c_str());
    BnkCache::Entry selected;
    try {
        selected = BnkCache::get(preferred_bnk);
    } catch (...) {
        err = "Could not open the selected texture bank.";
        return false;
    }
    const auto& selected_files = selected.reader->list_files();
    if (preferred_index < 0 ||
        preferred_index >= (int)selected_files.size()) {
        err = "Selected texture entry is out of range.";
        return false;
    }
    out.virtual_path = selected_files[(size_t)preferred_index].name;
    DebugTrace::log("texture-replace: selected entry='%s' bank_entries=%zu",
                    out.virtual_path.c_str(), selected_files.size());
    const std::string key = normalized_path(out.virtual_path);
    const std::filesystem::path preferred_parent =
        std::filesystem::path(preferred_bnk).parent_path();
    const std::string preferred_family =
        texture_bank_family(preferred_bnk);

    auto consider = [&](TexturePart& part, const std::string& path,
                        int index, int rank) {
        if (rank < part.rank) part = {path, index, rank};
    };

    auto same_scope = [&](const std::string& path) {
        const auto nested_it = S.nested_bnk_parents.find(path);
        if (preferred_is_nested) {
            return nested_it != S.nested_bnk_parents.end() &&
                   nested_it->second == preferred_nested_parent;
        }
        return nested_it == S.nested_bnk_parents.end() &&
               std::filesystem::path(path).parent_path() ==
                   preferred_parent;
    };

    std::vector<std::string> paths = S.bnk_paths;
    paths.insert(paths.end(), S.nested_bnk_paths.begin(),
                 S.nested_bnk_paths.end());
    if (std::find(paths.begin(), paths.end(), preferred_bnk) == paths.end()) {
        paths.push_back(preferred_bnk);
    }
    for (const std::string& path : paths) {
        const bool path_is_nested = S.nested_bnk_parents.count(path) != 0;
        if (preferred_is_nested != path_is_nested) continue;
        if (preferred_is_nested && !same_scope(path)) continue;
        const int index = BnkCache::find_index(path, key);
        if (index < 0) continue;
        int rank = path == preferred_bnk ? 0 : 3;
        if (rank != 0 && same_scope(path)) {
            rank = 1;
        } else if (rank != 0 &&
                   texture_bank_family(path) == preferred_family) {
            rank = 2;
        }
        const int role = texture_bank_role(path);
        if (role == 1) consider(out.header, path, index, rank);
        else if (role == 2 && same_scope(path)) {
            consider(out.mip0, path, index, rank);
        } else if (role == 3) {
            consider(out.body, path, index, rank);
        } else if (role == 4 &&
                   (path == preferred_bnk || out.body.path.empty())) {
            consider(out.body, path, index,
                     path == preferred_bnk ? 0 : rank + 10);
        }
    }
    if (out.header.path.empty() || out.body.path.empty()) {
        err = "Could not resolve both the header and body banks for '" +
              out.virtual_path + "'.";
        return false;
    }
    if (out.mip0.path.empty()) {
        for (const std::string& path : paths) {
            if (texture_bank_role(path) == 2 && same_scope(path)) {
                out.mip0.path = path;
                out.mip0.index = -1;
                out.mip0.rank = 1;
                break;
            }
        }
    }
    DebugTrace::log(
        "texture-replace: targets header='%s'[%d] mip='%s'[%d] body='%s'[%d]",
        out.header.path.c_str(), out.header.index, out.mip0.path.c_str(),
        out.mip0.index, out.body.path.c_str(), out.body.index);
    return true;
}

bool apply_texture_replacement(const TextureTargets& target,
                               const TexWriter::BuiltTex& built,
                               std::string& err) {
    DebugTrace::log(
        "texture-replace: apply begin header_bytes=%zu mip_bytes=%zu body_bytes=%zu",
        built.header.size(), built.mip0.size(), built.body.size());
    struct TargetChange {
        std::string path;
        std::vector<BnkWriter::EntryReplacement> replacements;
        std::vector<BnkWriter::EntryAddition> additions;
    };
    std::map<std::string, TargetChange> targets;
    auto replace = [&](const TexturePart& part,
                       const std::vector<uint8_t>& payload) {
        TargetChange& change = targets[part.path];
        change.path = part.path;
        change.replacements.push_back({part.index, payload});
    };
    replace(target.header, built.header);

    std::vector<uint8_t> body = built.body;
    if (target.mip0.path.empty()) {
        body.insert(body.begin(), built.mip0.begin(), built.mip0.end());
    } else if (target.mip0.index >= 0) {
        replace(target.mip0, built.mip0);
    } else if (!built.mip0.empty()) {
        TargetChange& change = targets[target.mip0.path];
        change.path = target.mip0.path;
        change.additions.push_back({target.virtual_path, built.mip0});
    }
    replace(target.body, body);

    struct NestedChange {
        std::string path;
        std::string parent_path;
        int parent_index = -1;
    };
    std::vector<NestedChange> nested_changes;
    std::map<std::string, TargetChange> disk_targets;
    for (const auto& [path, change] : targets) {
        const auto parent_it = S.nested_bnk_parents.find(path);
        if (parent_it == S.nested_bnk_parents.end()) {
            TargetChange& disk = disk_targets[path];
            disk = change;
            continue;
        }
        if (S.nested_bnk_parents.count(parent_it->second)) {
            err = "Texture replacement does not support nested BNKs deeper "
                  "than one level.";
            return false;
        }
        const auto virtual_it = S.nested_bnk_virtual_paths.find(path);
        if (virtual_it == S.nested_bnk_virtual_paths.end()) {
            err = "Could not resolve the nested texture bank in its parent.";
            return false;
        }
        const int parent_index = BnkCache::find_index(
            parent_it->second, normalized_path(virtual_it->second));
        if (parent_index < 0) {
            err = "Could not find the nested texture bank entry in its "
                  "parent.";
            return false;
        }
        TargetChange& parent = disk_targets[parent_it->second];
        parent.path = parent_it->second;
        nested_changes.push_back({path, parent_it->second, parent_index});
        DebugTrace::log(
            "texture-replace: nested bank='%s' parent='%s' parent_index=%d replacements=%zu additions=%zu",
            path.c_str(), parent_it->second.c_str(), parent_index,
            change.replacements.size(), change.additions.size());
    }

    std::vector<std::string> paths;
    for (const auto& [path, change] : disk_targets) paths.push_back(path);
    DebugTrace::log("texture-replace: backup check targets=%zu", paths.size());
    if (!GameBackup::EnsureFilesCovered(paths, err)) {
        DebugTrace::log("texture-replace: backup failed error='%s'", err.c_str());
        return false;
    }
    DebugTrace::log("texture-replace: backup ready");

    struct Snapshot {
        std::string path;
        std::vector<uint8_t> bytes;
    };
    auto read_snapshot = [&](const std::string& path,
                             Snapshot& snapshot) {
        std::error_code size_error;
        const uintmax_t size = std::filesystem::file_size(path, size_error);
        DebugTrace::log(
            "texture-replace: snapshot begin path='%s' bytes=%llu size_error=%d",
            path.c_str(), (unsigned long long)(size_error ? 0 : size),
            size_error ? 1 : 0);
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            err = "Could not read " + path;
            return false;
        }
        snapshot.path = path;
        snapshot.bytes.assign(std::istreambuf_iterator<char>(input),
                              std::istreambuf_iterator<char>());
        const bool ok = input.good() || input.eof();
        DebugTrace::log(
            "texture-replace: snapshot end path='%s' captured=%zu ok=%d",
            path.c_str(), snapshot.bytes.size(), ok ? 1 : 0);
        return ok;
    };
    auto restore = [](const std::vector<Snapshot>& snapshots) {
        DebugTrace::log("texture-replace: rollback begin files=%zu",
                        snapshots.size());
        for (const Snapshot& snapshot : snapshots) {
            std::ofstream output(snapshot.path,
                                 std::ios::binary | std::ios::trunc);
            if (output) {
                output.write((const char*)snapshot.bytes.data(),
                             (std::streamsize)snapshot.bytes.size());
            }
            BnkCache::invalidate(snapshot.path);
            DebugTrace::log(
                "texture-replace: rollback file='%s' bytes=%zu",
                snapshot.path.c_str(), snapshot.bytes.size());
        }
        DebugTrace::log("texture-replace: rollback end");
    };

    std::vector<Snapshot> disk_snapshots;
    for (const auto& [path, change] : disk_targets) {
        Snapshot snapshot;
        if (!read_snapshot(path, snapshot)) return false;
        disk_snapshots.push_back(std::move(snapshot));
    }

    std::vector<Snapshot> nested_snapshots;
    for (const NestedChange& nested : nested_changes) {
        Snapshot snapshot;
        if (!read_snapshot(nested.path, snapshot)) {
            restore(nested_snapshots);
            return false;
        }
        nested_snapshots.push_back(std::move(snapshot));
        TargetChange& change = targets[nested.path];
        BnkCache::invalidate(nested.path);
        DebugTrace::log(
            "texture-replace: nested rebuild begin path='%s' replacements=%zu additions=%zu",
            nested.path.c_str(), change.replacements.size(),
            change.additions.size());
        if (!BnkWriter::RebuildWithChanges(
                nested.path, change.replacements, change.additions, err)) {
            err = std::filesystem::path(nested.path).filename().string() +
                  ": " + err;
            restore(nested_snapshots);
            return false;
        }
        DebugTrace::log("texture-replace: nested rebuild end path='%s'",
                        nested.path.c_str());
        BnkCache::invalidate(nested.path);
        Snapshot rebuilt;
        if (!read_snapshot(nested.path, rebuilt)) {
            restore(nested_snapshots);
            return false;
        }
        TargetChange& parent = disk_targets[nested.parent_path];
        parent.replacements.push_back(
            {nested.parent_index, std::move(rebuilt.bytes)});
    }

    size_t applied = 0;
    for (auto& [path, change] : disk_targets) {
        BnkCache::invalidate(path);
        size_t replacement_bytes = 0;
        for (const auto& replacement : change.replacements) {
            replacement_bytes += replacement.payload.size();
        }
        DebugTrace::log(
            "texture-replace: parent rebuild begin path='%s' replacements=%zu replacement_bytes=%zu additions=%zu",
            path.c_str(), change.replacements.size(), replacement_bytes,
            change.additions.size());
        if (!BnkWriter::RebuildWithChanges(
                path, change.replacements, change.additions, err)) {
            err = std::filesystem::path(path).filename().string() + ": " +
                  err;
            break;
        }
        DebugTrace::log("texture-replace: parent rebuild end path='%s'",
                        path.c_str());
        ++applied;
    }
    if (applied != disk_targets.size()) {
        DebugTrace::log(
            "texture-replace: parent rebuild failed applied=%zu targets=%zu error='%s'",
            applied, disk_targets.size(), err.c_str());
        restore(disk_snapshots);
        restore(nested_snapshots);
        return false;
    }
    for (const auto& [path, change] : disk_targets) {
        BnkCache::invalidate(path);
    }
    DebugTrace::log("texture-replace: apply success");
    return true;
}

std::string unique_name(std::set<std::string>& used, std::string base)
{
    if (base.empty()) base = "texture";
    std::string name = base;
    int n = 2;
    while (!used.insert(name).second)
        name = base + "_" + std::to_string(n++);
    return name;
}

std::string derive_entity_id(const std::string& asset)
{
    std::string id = "PROP_" + asset;
    for (char& c : id) {
        if (!std::isalnum((unsigned char)c) && c != '_') c = '_';
    }
    return id;
}

void create_spawn_template(const std::string& mdl_vpath,
                           const Options& opt,
                           const std::string& asset,
                           Result& res)
{
    const bool explicit_id = !opt.entity_id.empty();
    std::string id = explicit_id ? opt.entity_id : derive_entity_id(asset);

    std::vector<StaticPropAuthoring::CatalogEntry> catalog;
    std::string cat_err;
    StaticPropAuthoring::LoadCatalog(S.root_dir, catalog, cat_err);

    auto find_entry = [&](const std::string& name)
        -> const StaticPropAuthoring::CatalogEntry* {
        for (const auto& e : catalog)
            if (e.internal_name == name) return &e;
        return nullptr;
    };

    if (const auto* existing = find_entry(id)) {
        if (existing->model_path == mdl_vpath) {
            res.entity_id = id;
            res.gdb_template_created = true;
            res.notes.push_back("entity " + id +
                                " already exists in globals.gdb and points "
                                "at this model - ready to place");
            return;
        }
        if (explicit_id) {
            res.notes.push_back(
                "GDB TEMPLATE SKIPPED: entity ID '" + id +
                "' already uses a different model (" +
                existing->model_path + "). Pick another ID.");
            return;
        }
        int n = 2;
        std::string candidate;
        do {
            candidate = id + "_" + std::to_string(n++);
        } while (find_entry(candidate));
        id = candidate;
    }

    StaticPropAuthoring::Definition def;
    def.internal_name = id;
    def.model_path = mdl_vpath;
    StaticPropAuthoring::CatalogEntry saved;
    std::string result, error;
    if (StaticPropAuthoring::Save(S.root_dir, def, saved, result, error)) {
        res.entity_id = id;
        res.gdb_template_created = true;
        res.notes.push_back("spawnable entity " + id +
                            " created in globals.gdb");
    } else {
        res.notes.push_back("GDB TEMPLATE FAILED: " + error);
    }
}

}

std::string sanitize_name(const std::string& raw)
{
    std::string out;
    out.reserve(raw.size());
    for (unsigned char c : raw) {
        if (std::isalnum(c)) out.push_back((char)std::tolower(c));
        else if (c == '_' || c == '-' || c == '.') out.push_back((char)c);
        else if (c == ' ') out.push_back('_');
    }
    while (!out.empty() && out.front() == '.') out.erase(out.begin());
    if (out.empty()) out = "asset";
    return out;
}

bool import_glb(const std::string& glb_path, const Options& opt,
                Result& res, std::string& err)
{
    res = Result{};
    const std::string stem =
        std::filesystem::path(glb_path).stem().string();
    const std::string asset =
        sanitize_name(opt.asset_name.empty() ? stem : opt.asset_name);
    std::string folder = normalize_folder(
        opt.dest_folder.empty() ? ("art\\imported\\" + asset) : opt.dest_folder);

    progress_update(0, 100, "Reading " + std::filesystem::path(glb_path)
                                              .filename().string());

    GlbImport::Scene scene;
    if (!GlbImport::load_glb(glb_path, scene, err)) return false;

    if (!opt.material_textures.empty()) {
        if (opt.material_textures.size() != scene.materials.size()) {
            err = "GLB material assignments no longer match the source file";
            return false;
        }
        auto valid_image = [&](int idx) {
            return idx == -1 ||
                   (idx >= 0 && size_t(idx) < scene.images.size());
        };
        for (size_t mi = 0; mi < scene.materials.size(); ++mi) {
            const auto& assignment = opt.material_textures[mi];
            for (const auto& slot : {
                     std::pair<const char*, int>{"diffuse", assignment.diffuse},
                     {"normal", assignment.normal},
                     {"specular", assignment.specular},
                     {"metallic", assignment.metallic},
                     {"extra", assignment.extra}}) {
                if (!valid_image(slot.second)) {
                    err = "material '" + scene.materials[mi].name +
                          "' has an invalid " + slot.first +
                          " texture selection";
                    return false;
                }
            }
            auto& material = scene.materials[mi];
            material.base_color = assignment.diffuse;
            material.normal = assignment.normal;
            material.occlusion = assignment.specular;
            material.metallic_rough = assignment.metallic;
            material.emissive = assignment.extra;
        }
    }

    for (const auto& material : scene.materials) {
        for (const auto& slot : {
                 std::pair<const char*, int>{"diffuse", material.base_color},
                 {"normal", material.normal},
                 {"specular", material.occlusion},
                 {"metallic", material.metallic_rough},
                 {"extra", material.emissive}}) {
            if (slot.second < -1 ||
                (slot.second >= 0 &&
                 size_t(slot.second) >= scene.images.size())) {
                err = "material '" + material.name + "' references an invalid " +
                      slot.first + " texture";
                return false;
            }
        }
    }

    std::set<int> generic_images;
    std::set<int> normal_images;
    for (const auto& material : scene.materials) {
        if (material.normal >= 0) normal_images.insert(material.normal);
        for (int idx : {material.occlusion, material.metallic_rough,
                        material.emissive}) {
            if (idx >= 0) generic_images.insert(idx);
        }
        if (material.base_color >= 0) {
            generic_images.insert(material.base_color);
        }
    }

    std::vector<PendingEntry> pending;
    std::set<std::string> used_names;
    std::vector<std::string> image_vpath(scene.images.size());
    std::vector<std::string> normal_vpath(scene.images.size());
    std::vector<ImageLoad::Image> decoded_images(scene.images.size());
    std::vector<bool> image_decoded(scene.images.size(), false);

    int prog = 5;
    progress_update(prog, 100, "Encoding textures");
    auto decode_image = [&](int idx) -> ImageLoad::Image* {
        if (idx < 0 || size_t(idx) >= scene.images.size()) return nullptr;
        if (image_decoded[idx]) return &decoded_images[idx];
        const auto& img = scene.images[idx];
        if (!ImageLoad::load_memory(img.bytes.data(), img.bytes.size(),
                                    img.name, decoded_images[idx], err)) {
            err = "texture '" + img.name + "': " + err;
            return nullptr;
        }
        image_decoded[idx] = true;
        return &decoded_images[idx];
    };
    auto add_texture = [&](const std::string& raw_name,
                           const TexWriter::BuiltTex& built) {
        const std::string name = unique_name(
            used_names, sanitize_name(raw_name));
        const std::string vpath = folder + "\\" + name + ".tex";
        queue_tex(vpath, built, pending);
        res.tex_virtual_paths.push_back(vpath);
        res.notes.push_back("texture " + vpath + " (" +
                            std::to_string(built.width) + "x" +
                            std::to_string(built.height) + ", " +
                            std::to_string(built.mip_count) + " mips)");
        prog = std::min(prog + 4, 55);
        progress_update(prog, 100, "Encoded " + name);
        return vpath;
    };
    auto encode_image = [&](int idx, bool normal,
                            std::string& out_path) -> bool {
        ImageLoad::Image* decoded = decode_image(idx);
        if (!decoded) return false;
        TexWriter::Options topt;
        topt.max_dimension = opt.max_tex_dim;
        topt.generate_mips = opt.generate_mips;
        topt.format = normal ? TexWriter::Format::BC5Normal : opt.tex_format;
        TexWriter::BuiltTex built;
        if (!TexWriter::build_from_rgba(decoded->rgba.data(), decoded->width,
                                        decoded->height, topt, built, err)) {
            err = "texture '" + scene.images[idx].name + "': " + err;
            return false;
        }
        if (!verify_tex(built, err)) return false;
        std::string name = scene.images[idx].name;
        if (normal && generic_images.count(idx)) name += "_normal";
        out_path = add_texture(name, built);
        return true;
    };
    for (int idx : generic_images) {
        if (!encode_image(idx, false, image_vpath[idx])) return false;
    }
    for (int idx : normal_images) {
        if (!encode_image(idx, true, normal_vpath[idx])) return false;
    }

    std::vector<std::string> material_diffuse(scene.materials.size());
    for (size_t mi = 0; mi < scene.materials.size(); ++mi) {
        const auto& mt = scene.materials[mi];
        if (mt.base_color >= 0) {
            material_diffuse[mi] = image_vpath[mt.base_color];
            continue;
        }

        ImageLoad::Image color_image;
        color_image.width = 8;
        color_image.height = 8;
        color_image.rgba.resize(8 * 8 * 4);
        for (int pixel = 0; pixel < 64; ++pixel) {
            for (int channel = 0; channel < 4; ++channel) {
                color_image.rgba[pixel * 4 + channel] = uint8_t(
                    std::lround(mt.base_color_factor[channel] * 255.0f));
            }
        }
        TexWriter::Options topt;
        topt.max_dimension = 8;
        topt.generate_mips = opt.generate_mips;
        topt.format = opt.tex_format;
        TexWriter::BuiltTex built;
        if (!TexWriter::build_from_rgba(
                color_image.rgba.data(), color_image.width,
                color_image.height, topt, built, err)) {
            return false;
        }
        if (!verify_tex(built, err)) return false;
        std::string name = mt.name.empty() ? asset : mt.name;
        name += "_flat";
        material_diffuse[mi] = add_texture(name, built);
    }

    progress_update(60, 100, "Building model");
    std::vector<MdlWriter::MeshInput> meshes;
    for (const auto& prim : scene.prims) {
        MdlWriter::MeshInput m;
        m.name = sanitize_name(prim.name);
        m.positions = prim.positions;
        m.normals = prim.normals;
        m.uvs = prim.uvs;
        m.indices = prim.indices;
        if (prim.material >= 0 &&
            (size_t)prim.material < scene.materials.size()) {
            const auto& mt = scene.materials[prim.material];
            auto pathof = [&](int idx) -> std::string {
                return (idx >= 0 && (size_t)idx < image_vpath.size())
                           ? image_vpath[idx] : std::string();
            };
            m.tex_diffuse = material_diffuse[prim.material];
            m.tex_specular = pathof(mt.occlusion);
            if (mt.normal >= 0 && size_t(mt.normal) < normal_vpath.size())
                m.tex_normal = normal_vpath[mt.normal];
            m.tex_metallic = pathof(mt.metallic_rough);
            m.tex_extra    = pathof(mt.emissive);
        }
        meshes.push_back(std::move(m));
    }

    MdlWriter::BuiltMdl built_mdl;
    if (!MdlWriter::build(meshes, built_mdl, err)) return false;
    if (!verify_mdl(built_mdl, err)) return false;

    const std::string mdl_vpath = folder + "\\" + asset + ".mdl";
    pending.push_back({"globals_model_headers.bnk", mdl_vpath, built_mdl.header});
    pending.push_back({"globals_models.bnk", mdl_vpath, built_mdl.body});

    
    progress_update(75, 100, "Injecting into BNKs");
    std::vector<std::pair<std::string, std::string>> injected;
    if (!inject_entries(pending, injected, err)) return false;

    progress_update(95, 100, "Refreshing indices");
    register_injected(injected);

    res.mdl_virtual_path = mdl_vpath;
    res.meshes = built_mdl.mesh_count;
    res.vertices = built_mdl.vertex_count;
    res.triangles = built_mdl.triangle_count;
    res.notes.push_back("model " + mdl_vpath + " (" +
                        std::to_string(built_mdl.mesh_count) + " meshes, " +
                        std::to_string(built_mdl.vertex_count) + " verts, " +
                        std::to_string(built_mdl.triangle_count) + " tris)");

    if (opt.create_gdb_template) {
        progress_update(98, 100, "Creating spawnable entity");
        create_spawn_template(mdl_vpath, opt, asset, res);
    }
    return true;
}

bool import_image(const std::string& img_path, const Options& opt,
                  Result& res, std::string& err)
{
    res = Result{};
    const std::string stem =
        std::filesystem::path(img_path).stem().string();
    const std::string name =
        sanitize_name(opt.asset_name.empty() ? stem : opt.asset_name);
    std::string folder = normalize_folder(
        opt.dest_folder.empty() ? "art\\imported" : opt.dest_folder);

    progress_update(10, 100, "Loading " + stem);
    ImageLoad::Image decoded;
    if (!ImageLoad::load_file(img_path, decoded, err)) return false;

    TexWriter::Options topt;
    topt.max_dimension = opt.max_tex_dim;
    topt.generate_mips = opt.generate_mips;
    topt.format = opt.tex_format;
    TexWriter::BuiltTex built;
    progress_update(40, 100, "Encoding " + name);
    if (!TexWriter::build_from_rgba(decoded.rgba.data(), decoded.width,
                                    decoded.height, topt, built, err))
        return false;
    if (!verify_tex(built, err)) return false;

    const std::string vpath = folder + "\\" + name + ".tex";
    std::vector<PendingEntry> pending;
    queue_tex(vpath, built, pending);

    progress_update(70, 100, "Injecting into BNKs");
    std::vector<std::pair<std::string, std::string>> injected;
    if (!inject_entries(pending, injected, err)) return false;
    register_injected(injected);

    res.tex_virtual_paths.push_back(vpath);
    res.notes.push_back("texture " + vpath + " (" +
                        std::to_string(built.width) + "x" +
                        std::to_string(built.height) + ", " +
                        std::to_string(built.mip_count) + " mips)");
    return true;
}

bool replace_texture(const std::string& img_path,
                     const std::string& target_bnk_path,
                     int target_file_index, const Options& opt,
                     Result& res, std::string& err) {
    res = Result{};
    DebugTrace::log(
        "=== texture-replace start image='%s' bank='%s' index=%d max_dim=%d mips=%d format=%d ===",
        img_path.c_str(), target_bnk_path.c_str(), target_file_index,
        opt.max_tex_dim, opt.generate_mips ? 1 : 0, (int)opt.tex_format);
    TextureTargets target;
    if (!resolve_texture_targets(target_bnk_path, target_file_index,
                                 target, err)) {
        DebugTrace::log("texture-replace: resolve failed error='%s'",
                        err.c_str());
        return false;
    }

    progress_update(10, 100, "Loading " +
        std::filesystem::path(img_path).filename().string());
    ImageLoad::Image decoded;
    if (!ImageLoad::load_file(img_path, decoded, err)) {
        DebugTrace::log("texture-replace: image load failed error='%s'",
                        err.c_str());
        return false;
    }
    DebugTrace::log("texture-replace: image loaded width=%d height=%d rgba=%zu",
                    decoded.width, decoded.height, decoded.rgba.size());

    TexWriter::Options texture_options;
    texture_options.max_dimension = opt.max_tex_dim;
    texture_options.generate_mips = opt.generate_mips;
    texture_options.format = opt.tex_format;
    if (texture_options.format == TexWriter::Format::Auto) {
        try {
            const std::vector<uint8_t> header = BnkCache::extract_bytes(
                target.header.path, target.header.index);
            if (header.size() >= 28) {
                const uint32_t format =
                    (uint32_t(header[24]) << 24) |
                    (uint32_t(header[25]) << 16) |
                    (uint32_t(header[26]) << 8) |
                    uint32_t(header[27]);
                if (format == 35) {
                    texture_options.format = TexWriter::Format::BC1;
                } else if (format == 39) {
                    texture_options.format = TexWriter::Format::BC3;
                } else if (format == 40) {
                    texture_options.format = TexWriter::Format::BC5Normal;
                } else if (format == 2 || format == 4) {
                    texture_options.format = TexWriter::Format::RawARGB;
                }
            }
        } catch (...) {
        }
    }

    progress_update(40, 100, "Encoding replacement texture");
    TexWriter::BuiltTex built;
    if (!TexWriter::build_from_rgba(
            decoded.rgba.data(), decoded.width, decoded.height,
            texture_options, built, err)) {
        DebugTrace::log("texture-replace: encode failed error='%s'",
                        err.c_str());
        return false;
    }
    DebugTrace::log(
        "texture-replace: encoded width=%u height=%u mips=%u header=%zu mip=%zu body=%zu",
        built.width, built.height, built.mip_count, built.header.size(),
        built.mip0.size(), built.body.size());
    if (!verify_tex(built, err)) {
        DebugTrace::log("texture-replace: verify failed error='%s'",
                        err.c_str());
        return false;
    }

    progress_update(65, 100, "Checking backup coverage");
    if (!apply_texture_replacement(target, built, err)) {
        DebugTrace::log("texture-replace: apply failed error='%s'",
                        err.c_str());
        return false;
    }

    progress_update(95, 100, "Refreshing texture caches");
    res.tex_virtual_paths.push_back(target.virtual_path);
    res.notes.push_back("replaced " + target.virtual_path + " (" +
                        std::to_string(built.width) + "x" +
                        std::to_string(built.height) + ", " +
                        std::to_string(built.mip_count) + " mips)");
    DebugTrace::log("=== texture-replace success entry='%s' ===",
                    target.virtual_path.c_str());
    return true;
}

bool import_folder(const std::string& folder_path, const Options& opt,
                   Result& res, std::string& err)
{
    res = Result{};
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(folder_path, ec)) {
        err = "not a folder: " + folder_path;
        return false;
    }

    std::vector<std::string> glbs, images;
    for (const auto& de : fs::directory_iterator(folder_path, ec)) {
        if (!de.is_regular_file()) continue;
        const std::string p = de.path().string();
        std::string ext = to_lower(de.path().extension().string());
        if (ext == ".glb") glbs.push_back(p);
        else if (ImageLoad::extension_supported(p)) images.push_back(p);
    }
    if (glbs.empty() && images.empty()) {
        err = "no .glb or image files found in " + folder_path;
        return false;
    }

    int item = 0;
    const int total = (int)(glbs.size() + images.size());
    size_t failures = 0;
    for (const auto& g : glbs) {
        progress_update(item * 100 / total, 100,
                        fs::path(g).filename().string());
        Options one = opt;
        one.asset_name.clear();
        one.entity_id.clear();
        one.dest_folder = opt.dest_folder;
        Result r;
        std::string e;
        if (import_glb(g, one, r, e)) {
            res.notes.push_back("imported " + fs::path(g).filename().string());
            for (auto& n : r.notes) res.notes.push_back("  " + n);
            res.meshes += r.meshes;
            res.vertices += r.vertices;
            res.triangles += r.triangles;
            if (res.mdl_virtual_path.empty())
                res.mdl_virtual_path = r.mdl_virtual_path;
            if (r.gdb_template_created) {
                res.gdb_template_created = true;
                if (res.entity_id.empty()) res.entity_id = r.entity_id;
            }
            res.tex_virtual_paths.insert(res.tex_virtual_paths.end(),
                                         r.tex_virtual_paths.begin(),
                                         r.tex_virtual_paths.end());
        } else {
            ++failures;
            res.notes.push_back("FAILED " + fs::path(g).filename().string() +
                                ": " + e);
        }
        ++item;
    }
    for (const auto& im : images) {
        progress_update(item * 100 / total, 100,
                        fs::path(im).filename().string());
        Options one = opt;
        one.asset_name.clear();
        Result r;
        std::string e;
        if (import_image(im, one, r, e)) {
            res.notes.push_back("imported " + fs::path(im).filename().string());
            res.tex_virtual_paths.insert(res.tex_virtual_paths.end(),
                                         r.tex_virtual_paths.begin(),
                                         r.tex_virtual_paths.end());
        } else {
            ++failures;
            res.notes.push_back("FAILED " + fs::path(im).filename().string() +
                                ": " + e);
        }
        ++item;
    }

    if (failures == glbs.size() + images.size()) {
        err = "every file in the folder failed to import";
        return false;
    }
    return true;
}

}
