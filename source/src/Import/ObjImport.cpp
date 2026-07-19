#include "ObjImport.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

namespace ObjImport {

namespace {

namespace fs = std::filesystem;

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

bool read_logical_lines(const std::string& path,
                        std::vector<std::string>& out, std::string& err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { err = "could not open " + path; return false; }
    std::string line, pending;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        bool cont = false;
        std::string t = trim(line);
        if (!t.empty() && t.back() == '\\') {
            t.pop_back();
            cont = true;
        }
        pending += t;
        if (cont) { pending += ' '; continue; }
        if (!pending.empty()) out.push_back(pending);
        pending.clear();
    }
    if (!pending.empty()) out.push_back(pending);
    return true;
}

std::string mtl_map_filename(const std::string& args) {
    static const std::map<std::string, int> kOptionArgs = {
        {"-blendu", 1}, {"-blendv", 1}, {"-boost", 1}, {"-bm", 1},
        {"-cc", 1},     {"-clamp", 1},  {"-imfchan", 1}, {"-mm", 2},
        {"-o", 3},      {"-s", 3},      {"-t", 3},       {"-texres", 1},
        {"-type", 1},
    };
    std::istringstream in(args);
    std::vector<std::string> tokens;
    std::string tok;
    while (in >> tok) tokens.push_back(tok);
    size_t i = 0;
    while (i < tokens.size()) {
        auto it = kOptionArgs.find(to_lower(tokens[i]));
        if (it == kOptionArgs.end()) break;
        i += 1 + (size_t)it->second;
    }
    std::string name;
    for (; i < tokens.size(); ++i) {
        if (!name.empty()) name += ' ';
        name += tokens[i];
    }
    return name;
}

struct MtlMaterial {
    std::string name;
    std::string map_kd, map_normal, map_ks, map_ke;
    float kd[3] = {0.8f, 0.8f, 0.8f};
    float alpha = 1.0f;
    bool has_kd = false;
};

void parse_mtl(const std::string& path,
               std::vector<MtlMaterial>& materials) {
    std::vector<std::string> lines;
    std::string err;
    if (!read_logical_lines(path, lines, err)) return;
    MtlMaterial* cur = nullptr;
    for (const std::string& raw : lines) {
        std::istringstream in(raw);
        std::string key;
        if (!(in >> key)) continue;
        std::string rest;
        std::getline(in, rest);
        rest = trim(rest);
        const std::string lkey = to_lower(key);
        if (lkey == "newmtl") {
            materials.emplace_back();
            cur = &materials.back();
            cur->name = rest;
            continue;
        }
        if (!cur) continue;
        if (lkey == "kd") {
            std::istringstream v(rest);
            if (v >> cur->kd[0] >> cur->kd[1] >> cur->kd[2])
                cur->has_kd = true;
        } else if (lkey == "d") {
            std::istringstream v(rest);
            float d = 1.0f;
            if (v >> d) cur->alpha = d;
        } else if (lkey == "tr") {
            std::istringstream v(rest);
            float tr = 0.0f;
            if (v >> tr) cur->alpha = 1.0f - tr;
        } else if (lkey == "map_kd") {
            cur->map_kd = mtl_map_filename(rest);
        } else if (lkey == "map_bump" || lkey == "bump" ||
                   lkey == "norm" || lkey == "map_kn") {
            cur->map_normal = mtl_map_filename(rest);
        } else if (lkey == "map_ks") {
            cur->map_ks = mtl_map_filename(rest);
        } else if (lkey == "map_ke") {
            cur->map_ke = mtl_map_filename(rest);
        }
    }
}

std::string resolve_texture_path(const std::string& reference,
                                 const fs::path& mtl_dir,
                                 const fs::path& obj_dir) {
    if (reference.empty()) return {};
    std::string normalized = reference;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    fs::path ref(normalized);
    std::error_code ec;

    std::vector<fs::path> candidates;
    if (ref.is_absolute()) {
        candidates.push_back(ref);
    } else {
        candidates.push_back(mtl_dir / ref);
        candidates.push_back(obj_dir / ref);
        candidates.push_back(mtl_dir / ref.filename());
        candidates.push_back(obj_dir / ref.filename());
    }
    for (const fs::path& c : candidates) {
        if (fs::is_regular_file(c, ec)) return c.string();
    }

    const std::string want = to_lower(ref.filename().string());
    for (const fs::path& dir : {mtl_dir, obj_dir}) {
        if (dir.empty() || !fs::is_directory(dir, ec)) continue;
        for (const auto& de : fs::directory_iterator(dir, ec)) {
            if (!de.is_regular_file()) continue;
            if (to_lower(de.path().filename().string()) == want)
                return de.path().string();
        }
    }
    return {};
}

struct FaceCorner {
    int v = 0, vt = 0, vn = 0;
};

bool parse_corner(const std::string& token, FaceCorner& out) {
    int parts[3] = {0, 0, 0};
    int part = 0;
    size_t i = 0;
    while (i < token.size() && part < 3) {
        size_t slash = token.find('/', i);
        std::string piece = (slash == std::string::npos)
                                ? token.substr(i)
                                : token.substr(i, slash - i);
        if (!piece.empty()) {
            try {
                parts[part] = std::stoi(piece);
            } catch (...) {
                return false;
            }
        }
        if (slash == std::string::npos) break;
        i = slash + 1;
        ++part;
    }
    out.v = parts[0];
    out.vt = parts[1];
    out.vn = parts[2];
    return out.v != 0;
}

struct PrimBuilder {
    std::string name;
    int material = -1;
    std::vector<float> positions, normals, uvs;
    std::vector<uint32_t> indices;
    std::map<std::array<int, 3>, uint32_t> weld;
    bool any_missing_normal = false;
};

}

bool load_obj(const std::string& path, GlbImport::Scene& out,
              std::string& err)
{
    out = GlbImport::Scene{};

    std::vector<std::string> lines;
    if (!read_logical_lines(path, lines, err)) return false;

    const fs::path obj_dir = fs::path(path).parent_path();
    const std::string stem = fs::path(path).stem().string();

    std::vector<float> vx, vt, vn;
    std::vector<MtlMaterial> mtl_materials;
    std::map<std::string, fs::path> mtl_dirs;

    for (const std::string& raw : lines) {
        std::istringstream in(raw);
        std::string key;
        if (!(in >> key)) continue;
        if (to_lower(key) != "mtllib") continue;
        std::string rest;
        std::getline(in, rest);
        rest = trim(rest);
        auto parse_lib = [&](const std::string& lib) -> bool {
            const std::string resolved =
                resolve_texture_path(lib, obj_dir, obj_dir);
            if (resolved.empty()) return false;
            const size_t before = mtl_materials.size();
            parse_mtl(resolved, mtl_materials);
            for (size_t i = before; i < mtl_materials.size(); ++i) {
                mtl_dirs[mtl_materials[i].name] =
                    fs::path(resolved).parent_path();
            }
            return true;
        };
        if (!parse_lib(rest)) {
            std::istringstream split(rest);
            std::string one;
            while (split >> one) parse_lib(one);
        }
    }

    std::map<std::string, int> material_index;
    std::map<std::string, int> image_by_file;

    auto add_image = [&](const std::string& reference,
                         const fs::path& mtl_dir) -> int {
        if (reference.empty()) return -1;
        const std::string resolved =
            resolve_texture_path(reference, mtl_dir, obj_dir);
        if (resolved.empty()) return -1;
        auto it = image_by_file.find(resolved);
        if (it != image_by_file.end()) return it->second;
        std::ifstream f(resolved, std::ios::binary);
        if (!f) return -1;
        GlbImport::Image image;
        image.name = fs::path(resolved).stem().string();
        image.bytes.assign(std::istreambuf_iterator<char>(f),
                           std::istreambuf_iterator<char>());
        if (image.bytes.empty()) return -1;
        const int index = (int)out.images.size();
        out.images.push_back(std::move(image));
        image_by_file.emplace(resolved, index);
        return index;
    };

    for (const MtlMaterial& src : mtl_materials) {
        if (material_index.count(src.name)) continue;
        const fs::path mtl_dir = mtl_dirs.count(src.name)
                                     ? mtl_dirs[src.name] : obj_dir;
        GlbImport::Material material;
        material.name = src.name.empty()
                            ? "material_" + std::to_string(out.materials.size())
                            : src.name;
        material.base_color = add_image(src.map_kd, mtl_dir);
        material.normal = add_image(src.map_normal, mtl_dir);
        material.occlusion = add_image(src.map_ks, mtl_dir);
        material.emissive = add_image(src.map_ke, mtl_dir);
        material.base_color_factor[0] = src.kd[0];
        material.base_color_factor[1] = src.kd[1];
        material.base_color_factor[2] = src.kd[2];
        material.base_color_factor[3] = src.alpha;
        material.alpha_blend = src.alpha < 1.0f;
        material_index[src.name] = (int)out.materials.size();
        out.materials.push_back(std::move(material));
    }

    int default_material = -1;
    auto ensure_default_material = [&]() {
        if (default_material >= 0) return default_material;
        GlbImport::Material material;
        material.name = stem + "_default";
        default_material = (int)out.materials.size();
        out.materials.push_back(std::move(material));
        return default_material;
    };

    std::map<std::pair<std::string, int>, PrimBuilder> builders;
    std::string group = stem;
    int current_material = -1;
    bool missing_material_used = false;

    auto builder_for = [&]() -> PrimBuilder& {
        auto key = std::make_pair(group, current_material);
        auto it = builders.find(key);
        if (it != builders.end()) return it->second;
        PrimBuilder b;
        b.name = group;
        b.material = current_material;
        return builders.emplace(key, std::move(b)).first->second;
    };

    auto resolve_index = [](int idx, size_t count) -> int {
        if (idx > 0) return idx - 1;
        if (idx < 0) return (int)count + idx;
        return -1;
    };

    for (const std::string& raw : lines) {
        std::istringstream in(raw);
        std::string key;
        if (!(in >> key)) continue;
        const std::string lkey = to_lower(key);
        if (lkey == "v") {
            float x = 0, y = 0, z = 0;
            in >> x >> y >> z;
            vx.push_back(x); vx.push_back(y); vx.push_back(z);
        } else if (lkey == "vt") {
            float u = 0, v = 0;
            in >> u >> v;
            vt.push_back(u); vt.push_back(v);
        } else if (lkey == "vn") {
            float x = 0, y = 0, z = 0;
            in >> x >> y >> z;
            vn.push_back(x); vn.push_back(y); vn.push_back(z);
        } else if (lkey == "o" || lkey == "g") {
            std::string rest;
            std::getline(in, rest);
            rest = trim(rest);
            group = rest.empty() ? stem : rest;
        } else if (lkey == "usemtl") {
            std::string rest;
            std::getline(in, rest);
            rest = trim(rest);
            auto it = material_index.find(rest);
            current_material =
                (it != material_index.end()) ? it->second : -1;
            if (current_material < 0 && !rest.empty())
                missing_material_used = true;
        } else if (lkey == "f") {
            std::vector<FaceCorner> corners;
            std::string tok;
            while (in >> tok) {
                FaceCorner c;
                if (parse_corner(tok, c)) corners.push_back(c);
            }
            if (corners.size() < 3) continue;

            if (current_material < 0) {
                current_material = ensure_default_material();
            }
            PrimBuilder& b = builder_for();

            auto emit_vertex = [&](const FaceCorner& c) -> int {
                const int pi = resolve_index(c.v, vx.size() / 3);
                if (pi < 0 || (size_t)pi >= vx.size() / 3) return -1;
                const int ti = resolve_index(c.vt, vt.size() / 2);
                const int ni = resolve_index(c.vn, vn.size() / 3);

                const std::array<int, 3> key = {pi, ti, ni};
                auto it = b.weld.find(key);
                if (it != b.weld.end()) return (int)it->second;

                const uint32_t index = (uint32_t)(b.positions.size() / 3);
                b.positions.push_back(vx[pi * 3 + 0]);
                b.positions.push_back(vx[pi * 3 + 1]);
                b.positions.push_back(vx[pi * 3 + 2]);
                if (ti >= 0 && (size_t)ti < vt.size() / 2) {
                    b.uvs.push_back(vt[ti * 2 + 0]);
                    b.uvs.push_back(1.0f - vt[ti * 2 + 1]);
                } else {
                    b.uvs.push_back(0.0f);
                    b.uvs.push_back(0.0f);
                }
                if (ni >= 0 && (size_t)ni < vn.size() / 3) {
                    b.normals.push_back(vn[ni * 3 + 0]);
                    b.normals.push_back(vn[ni * 3 + 1]);
                    b.normals.push_back(vn[ni * 3 + 2]);
                } else {
                    b.normals.push_back(0.0f);
                    b.normals.push_back(0.0f);
                    b.normals.push_back(0.0f);
                    b.any_missing_normal = true;
                }
                b.weld.emplace(key, index);
                return (int)index;
            };

            const int first = emit_vertex(corners[0]);
            if (first < 0) continue;
            for (size_t c = 1; c + 1 < corners.size(); ++c) {
                const int second = emit_vertex(corners[c]);
                const int third = emit_vertex(corners[c + 1]);
                if (second < 0 || third < 0) continue;
                b.indices.push_back((uint32_t)first);
                b.indices.push_back((uint32_t)second);
                b.indices.push_back((uint32_t)third);
            }
        }
    }

    if (missing_material_used) {
        ensure_default_material();
    }

    for (auto& [key, b] : builders) {
        if (b.positions.empty() || b.indices.size() < 3) continue;
        const size_t vcount = b.positions.size() / 3;

        if (b.any_missing_normal) {
            std::vector<float> accum(vcount * 3, 0.0f);
            for (size_t t = 0; t + 2 < b.indices.size(); t += 3) {
                const uint32_t a = b.indices[t], c = b.indices[t + 1],
                               d = b.indices[t + 2];
                if (a >= vcount || c >= vcount || d >= vcount) continue;
                const float* A = &b.positions[a * 3];
                const float* B = &b.positions[c * 3];
                const float* C = &b.positions[d * 3];
                const float u[3] = {B[0]-A[0], B[1]-A[1], B[2]-A[2]};
                const float w[3] = {C[0]-A[0], C[1]-A[1], C[2]-A[2]};
                const float n[3] = {u[1]*w[2]-u[2]*w[1],
                                    u[2]*w[0]-u[0]*w[2],
                                    u[0]*w[1]-u[1]*w[0]};
                for (uint32_t vi : {a, c, d}) {
                    accum[vi*3+0] += n[0];
                    accum[vi*3+1] += n[1];
                    accum[vi*3+2] += n[2];
                }
            }
            for (size_t v = 0; v < vcount; ++v) {
                float* dst = &b.normals[v * 3];
                const float lsq = dst[0]*dst[0] + dst[1]*dst[1] +
                                  dst[2]*dst[2];
                if (lsq > 1e-12f) continue;
                dst[0] = accum[v*3+0];
                dst[1] = accum[v*3+1];
                dst[2] = accum[v*3+2];
            }
        }

        GlbImport::Prim prim;
        prim.name = b.name;
        prim.material = b.material;
        prim.positions.resize(vcount * 3);
        prim.normals.resize(vcount * 3);
        prim.uvs = b.uvs;
        for (size_t v = 0; v < vcount; ++v) {
            prim.positions[v*3+0] = b.positions[v*3+0];
            prim.positions[v*3+1] = -b.positions[v*3+2];
            prim.positions[v*3+2] = b.positions[v*3+1];
            float nx = b.normals[v*3+0];
            float ny = -b.normals[v*3+2];
            float nz = b.normals[v*3+1];
            const float l = std::sqrt(nx*nx + ny*ny + nz*nz);
            if (l < 1e-12f) {
                prim.normals[v*3+0] = 0.0f;
                prim.normals[v*3+1] = 0.0f;
                prim.normals[v*3+2] = 1.0f;
            } else {
                prim.normals[v*3+0] = nx / l;
                prim.normals[v*3+1] = ny / l;
                prim.normals[v*3+2] = nz / l;
            }
        }
        prim.indices.resize(b.indices.size());
        for (size_t t = 0; t + 2 < b.indices.size(); t += 3) {
            prim.indices[t + 0] = b.indices[t + 0];
            prim.indices[t + 1] = b.indices[t + 2];
            prim.indices[t + 2] = b.indices[t + 1];
        }
        out.prims.push_back(std::move(prim));
    }

    if (out.prims.empty()) {
        err = "no triangle geometry found in " +
              fs::path(path).filename().string();
        return false;
    }
    return true;
}

std::vector<std::string> referenced_texture_files(const std::string& path)
{
    std::vector<std::string> result;
    std::vector<std::string> lines;
    std::string err;
    if (!read_logical_lines(path, lines, err)) return result;

    const fs::path obj_dir = fs::path(path).parent_path();
    std::vector<MtlMaterial> mtl_materials;
    std::map<std::string, fs::path> mtl_dirs;
    for (const std::string& raw : lines) {
        std::istringstream in(raw);
        std::string key;
        if (!(in >> key)) continue;
        if (to_lower(key) != "mtllib") continue;
        std::string rest;
        std::getline(in, rest);
        rest = trim(rest);
        auto parse_lib = [&](const std::string& lib) -> bool {
            const std::string resolved =
                resolve_texture_path(lib, obj_dir, obj_dir);
            if (resolved.empty()) return false;
            const size_t before = mtl_materials.size();
            parse_mtl(resolved, mtl_materials);
            for (size_t i = before; i < mtl_materials.size(); ++i) {
                mtl_dirs[mtl_materials[i].name] =
                    fs::path(resolved).parent_path();
            }
            return true;
        };
        if (!parse_lib(rest)) {
            std::istringstream split(rest);
            std::string one;
            while (split >> one) parse_lib(one);
        }
    }
    for (const MtlMaterial& material : mtl_materials) {
        const fs::path mtl_dir = mtl_dirs.count(material.name)
                                     ? mtl_dirs[material.name] : obj_dir;
        for (const std::string& reference :
             {material.map_kd, material.map_normal, material.map_ks,
              material.map_ke}) {
            const std::string resolved =
                resolve_texture_path(reference, mtl_dir, obj_dir);
            if (!resolved.empty()) result.push_back(resolved);
        }
    }
    return result;
}

}
