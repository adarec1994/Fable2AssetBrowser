#include "AssetImport.h"

#include "GlbImport.h"
#include "ImageLoad.h"
#include "MdlWriter.h"

#include "MDL/ModelParser.h"
#include "UI/OutputLog.h"
#include "Utilities/Progress.h"
#include "Utilities/State.h"
#include "Utilities/Utils.h"

#include "BNKCore.cpp"
#include "Level/IO/BnkWriter.h"

#include <algorithm>
#include <cctype>
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

std::string unique_name(std::set<std::string>& used, std::string base)
{
    if (base.empty()) base = "texture";
    std::string name = base;
    int n = 2;
    while (!used.insert(name).second)
        name = base + "_" + std::to_string(n++);
    return name;
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

    
    
    
    std::vector<int> image_role(scene.images.size(), 0);   
    for (const auto& mt : scene.materials)
        if (mt.normal >= 0 && (size_t)mt.normal < scene.images.size())
            image_role[mt.normal] = 1;

    std::set<int> used_images;
    for (const auto& mt : scene.materials)
        for (int idx : {mt.base_color, mt.normal, mt.metallic_rough,
                        mt.occlusion, mt.emissive})
            if (idx >= 0 && (size_t)idx < scene.images.size())
                used_images.insert(idx);

    std::vector<PendingEntry> pending;
    std::set<std::string> used_names;
    std::vector<std::string> image_vpath(scene.images.size());

    int prog = 5;
    progress_update(prog, 100, "Encoding textures");
    for (int idx : used_images) {
        const auto& img = scene.images[idx];
        ImageLoad::Image decoded;
        if (!ImageLoad::load_memory(img.bytes.data(), img.bytes.size(),
                                    img.name, decoded, err)) {
            err = "texture '" + img.name + "': " + err;
            return false;
        }
        TexWriter::Options topt;
        topt.max_dimension = opt.max_tex_dim;
        topt.generate_mips = opt.generate_mips;
        topt.format = (image_role[idx] == 1) ? TexWriter::Format::BC5Normal
                                             : opt.tex_format;
        TexWriter::BuiltTex built;
        if (!TexWriter::build_from_rgba(decoded.rgba.data(), decoded.width,
                                        decoded.height, topt, built, err)) {
            err = "texture '" + img.name + "': " + err;
            return false;
        }
        if (!verify_tex(built, err)) return false;

        const std::string name = unique_name(used_names,
                                             sanitize_name(img.name));
        const std::string vpath = folder + "\\" + name + ".tex";
        image_vpath[idx] = vpath;
        queue_tex(vpath, built, pending);
        res.tex_virtual_paths.push_back(vpath);
        res.notes.push_back("texture " + vpath + " (" +
                            std::to_string(built.width) + "x" +
                            std::to_string(built.height) + ", " +
                            std::to_string(built.mip_count) + " mips)");
        prog = std::min(prog + 5, 55);
        progress_update(prog, 100, "Encoded " + name);
    }

    
    std::vector<std::string> mat_fallback(scene.materials.size());
    for (size_t mi = 0; mi < scene.materials.size(); ++mi) {
        const auto& mt = scene.materials[mi];
        if (mt.base_color >= 0) continue;
        uint8_t rgba[8 * 8 * 4];
        for (int p = 0; p < 64; ++p) {
            rgba[p * 4 + 0] = (uint8_t)std::min(255.0f, mt.base_color_factor[0] * 255.0f);
            rgba[p * 4 + 1] = (uint8_t)std::min(255.0f, mt.base_color_factor[1] * 255.0f);
            rgba[p * 4 + 2] = (uint8_t)std::min(255.0f, mt.base_color_factor[2] * 255.0f);
            rgba[p * 4 + 3] = (uint8_t)std::min(255.0f, mt.base_color_factor[3] * 255.0f);
        }
        TexWriter::Options topt;
        topt.max_dimension = 8;
        topt.generate_mips = opt.generate_mips;
        topt.format = TexWriter::Format::Auto;
        TexWriter::BuiltTex built;
        if (!TexWriter::build_from_rgba(rgba, 8, 8, topt, built, err))
            return false;
        if (!verify_tex(built, err)) return false;
        const std::string name = unique_name(
            used_names, sanitize_name(mt.name.empty()
                                          ? asset + "_flat"
                                          : mt.name + "_flat"));
        const std::string vpath = folder + "\\" + name + ".tex";
        mat_fallback[mi] = vpath;
        queue_tex(vpath, built, pending);
        res.tex_virtual_paths.push_back(vpath);
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
            m.tex_diffuse = pathof(mt.base_color);
            if (m.tex_diffuse.empty())
                m.tex_diffuse = mat_fallback[prim.material];
            m.tex_specular = pathof(mt.occlusion);
            m.tex_normal   = pathof(mt.normal);
            m.tex_metallic = pathof(mt.metallic_rough);
            m.tex_extra    = pathof(mt.emissive);
        }
        if (m.tex_diffuse.empty() && !res.tex_virtual_paths.empty())
            m.tex_diffuse = res.tex_virtual_paths.front();
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
