#include "TerrainPaint.h"

#include "../../BNKCore.cpp"
#include "../../UI/ModelPreview.h"
#include "../../UI/OutputLog.h"
#include "../../Utilities/State.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace TerrainPaint {

namespace {

struct DecodedTexture {
    int w = 0;
    int h = 0;
    std::vector<uint8_t> rgba;
    bool ok = false;
};

struct State {
    std::string level_key;
    int   grid_w = 0;
    int   grid_h = 0;
    float tile_size = 1.0f;

    
    int paint_w = 0;
    int paint_h = 0;
    std::vector<Layer> layers;
    std::vector<std::vector<uint8_t>> weights;

    int  active_layer = 0;
    bool dirty = false;

    std::unordered_map<std::string, DecodedTexture> tex_cache;
};

State& st() {
    static State s;
    return s;
}

std::string sanitize_key(const std::string& key) {
    std::string out = key;
    for (char& c : out) {
        if (c == '\\' || c == '/' || c == ':') c = '_';
        else c = (char)std::tolower((unsigned char)c);
    }
    return out;
}

std::filesystem::path sidecar_dir() {
    std::filesystem::path root(S.root_dir);
    std::error_code ec;
    if (!S.root_dir.empty() &&
        std::filesystem::is_regular_file(root, ec)) {
        root = root.parent_path();
    }
    if (root.empty()) root = std::filesystem::current_path();
    return root / "edited_levels";
}

std::filesystem::path sidecar_path(const char* ext) {
    return sidecar_dir() / (sanitize_key(st().level_key) + ext);
}

const DecodedTexture& decoded_texture(const std::string& tex_path) {
    auto& s = st();
    auto it = s.tex_cache.find(tex_path);
    if (it != s.tex_cache.end()) return it->second;

    DecodedTexture dt;
    for (const FlatAssetEntry& e : S.all_tex_files) {
        if (e.full_path != tex_path) continue;
        try {
            const std::vector<uint8_t> blob =
                BnkCache::extract_bytes(e.bnk_path, e.file_index);
            std::vector<unsigned char> copy(blob.begin(), blob.end());
            bool has_alpha = false;
            dt.ok = decode_tex_to_rgba(copy, dt.rgba, dt.w, dt.h,
                                       &has_alpha);
        } catch (...) {
            dt.ok = false;
        }
        break;
    }
    if (!dt.ok) {
        OutputLog::warn("paint: could not decode texture " + tex_path);
    }
    return s.tex_cache.emplace(tex_path, std::move(dt)).first->second;
}

void load_sidecar() {
    auto& s = st();
    std::ifstream meta(sidecar_path(".paint.txt"));
    if (!meta) return;
    std::string line;
    while (std::getline(meta, line)) {
        while (!line.empty() &&
               (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        const size_t t1 = line.find('\t');
        if (line.rfind("LAYER\t", 0) != 0 || t1 == std::string::npos) {
            continue;
        }
        const size_t t2 = line.find('\t', t1 + 1);
        Layer layer;
        layer.tex_path = line.substr(
            t1 + 1, t2 == std::string::npos ? std::string::npos
                                            : t2 - t1 - 1);
        if (t2 != std::string::npos) {
            layer.tiling = std::strtof(line.c_str() + t2 + 1, nullptr);
            if (layer.tiling <= 0.0f) layer.tiling = 8.0f;
        }
        if ((int)s.layers.size() < kMaxLayers) {
            s.layers.push_back(std::move(layer));
        }
    }
    if (s.layers.empty()) return;

    std::ifstream wf(sidecar_path(".paint.bin"), std::ios::binary);
    if (wf) {
        uint32_t pw = 0, ph = 0, count = 0;
        wf.read(reinterpret_cast<char*>(&pw), 4);
        wf.read(reinterpret_cast<char*>(&ph), 4);
        wf.read(reinterpret_cast<char*>(&count), 4);
        if (wf && pw == (uint32_t)s.paint_w &&
            ph == (uint32_t)s.paint_h &&
            count == (uint32_t)s.layers.size()) {
            s.weights.assign(s.layers.size(),
                             std::vector<uint8_t>(
                                 (size_t)s.paint_w * s.paint_h, 0));
            for (auto& w : s.weights) {
                wf.read(reinterpret_cast<char*>(w.data()),
                        (std::streamsize)w.size());
            }
            if (!wf) s.weights.clear();
        }
    }
    if (s.weights.size() != s.layers.size()) {
        s.weights.assign(
            s.layers.size(),
            std::vector<uint8_t>((size_t)s.paint_w * s.paint_h, 0));
    }
    OutputLog::info("paint: restored " +
                    std::to_string(s.layers.size()) +
                    " terrain layer(s)");
}

}

void InitForLevel(const std::string& level_key, int grid_w, int grid_h,
                  float tile_size) {
    auto& s = st();
    s = State{};
    s.level_key = level_key;
    s.grid_w = std::max(2, grid_w);
    s.grid_h = std::max(2, grid_h);
    s.tile_size = tile_size > 0.0f ? tile_size : 1.0f;
    s.paint_w = std::clamp(s.grid_w * 2, 64, 1024);
    s.paint_h = std::clamp(s.grid_h * 2, 64, 1024);
    load_sidecar();
}

void Clear() { st() = State{}; }

bool Active() {
    return !st().level_key.empty() && !st().layers.empty();
}

const std::vector<Layer>& Layers() { return st().layers; }

int AddLayer(const std::string& tex_path) {
    auto& s = st();
    if (s.level_key.empty()) return -1;
    if ((int)s.layers.size() >= kMaxLayers) return -1;
    for (const Layer& layer : s.layers) {
        if (layer.tex_path == tex_path) return -1;
    }
    Layer layer;
    layer.tex_path = tex_path;
    s.layers.push_back(std::move(layer));
    s.weights.emplace_back((size_t)s.paint_w * s.paint_h, 0);
    s.active_layer = (int)s.layers.size() - 1;
    s.dirty = true;
    return s.active_layer;
}

void RemoveLayer(int index) {
    auto& s = st();
    if (index < 0 || index >= (int)s.layers.size()) return;
    s.layers.erase(s.layers.begin() + index);
    s.weights.erase(s.weights.begin() + index);
    if (s.active_layer >= (int)s.layers.size()) {
        s.active_layer = (int)s.layers.size() - 1;
    }
    s.dirty = true;
}

void SetLayerTiling(int index, float metres) {
    auto& s = st();
    if (index < 0 || index >= (int)s.layers.size()) return;
    s.layers[(size_t)index].tiling = std::clamp(metres, 0.5f, 256.0f);
    s.dirty = true;
}

int  ActiveLayer() { return st().active_layer; }
void SetActiveLayer(int index) {
    auto& s = st();
    if (index >= 0 && index < (int)s.layers.size()) {
        s.active_layer = index;
    }
}

bool Dirty() { return st().dirty; }
void MarkSaved() { st().dirty = false; }

void ApplyBrush(float wx, float wz, float radius_m, float strength01,
                float falloff01, bool erase) {
    auto& s = st();
    if (s.active_layer < 0 || s.active_layer >= (int)s.weights.size()) {
        return;
    }
    const float span_x = float(s.grid_w - 1) * s.tile_size;
    const float span_z = float(s.grid_h - 1) * s.tile_size;
    if (span_x <= 0.0f || span_z <= 0.0f || radius_m <= 0.0f) return;

    const float px_c = wx / span_x * float(s.paint_w - 1);
    const float pz_c = wz / span_z * float(s.paint_h - 1);
    const float pr_x = radius_m / span_x * float(s.paint_w - 1);
    const float pr_z = radius_m / span_z * float(s.paint_h - 1);
    const int xmin = std::max(0, int(std::floor(px_c - pr_x)));
    const int xmax = std::min(s.paint_w - 1, int(std::ceil(px_c + pr_x)));
    const int zmin = std::max(0, int(std::floor(pz_c - pr_z)));
    const int zmax = std::min(s.paint_h - 1, int(std::ceil(pz_c + pr_z)));
    if (xmin > xmax || zmin > zmax) return;

    const float falloff = std::clamp(falloff01, 0.0f, 1.0f);
    const float add = std::clamp(strength01, 0.0f, 1.0f) * 48.0f;
    std::vector<uint8_t>& target = s.weights[(size_t)s.active_layer];

    for (int z = zmin; z <= zmax; ++z) {
        for (int x = xmin; x <= xmax; ++x) {
            const float dx = (float(x) - px_c) / std::max(pr_x, 0.001f);
            const float dz = (float(z) - pz_c) / std::max(pr_z, 0.001f);
            const float dist = std::sqrt(dx * dx + dz * dz);
            if (dist >= 1.0f) continue;
            float w = 1.0f;
            const float fade_start = 1.0f - falloff;
            if (dist > fade_start && falloff > 0.0f) {
                const float t = 1.0f - (dist - fade_start) / falloff;
                w = t * t * (3.0f - 2.0f * t);
            }
            const size_t idx = (size_t)z * s.paint_w + x;
            const int delta = int(add * w + 0.5f) * (erase ? -1 : 1);
            const int next =
                std::clamp(int(target[idx]) + delta, 0, 255);
            target[idx] = (uint8_t)next;
        }
    }
    s.dirty = true;
}

bool BuildComposite(std::vector<uint8_t>& rgba, int& out_w, int& out_h) {
    auto& s = st();
    if (s.layers.empty()) return false;

    const int W = s.paint_w;
    const int H = s.paint_h;
    out_w = W;
    out_h = H;
    rgba.assign((size_t)W * H * 4, 0);

    const float span_x = float(s.grid_w - 1) * s.tile_size;
    const float span_z = float(s.grid_h - 1) * s.tile_size;

    struct LayerSample {
        const DecodedTexture* tex = nullptr;
        float repeat_x = 1.0f;
        float repeat_z = 1.0f;
    };
    std::vector<LayerSample> samples(s.layers.size());
    for (size_t li = 0; li < s.layers.size(); ++li) {
        const DecodedTexture& dt = decoded_texture(s.layers[li].tex_path);
        if (!dt.ok) continue;
        samples[li].tex = &dt;
        samples[li].repeat_x = span_x / s.layers[li].tiling;
        samples[li].repeat_z = span_z / s.layers[li].tiling;
    }

    for (int z = 0; z < H; ++z) {
        const float v = float(z) / float(H - 1);
        for (int x = 0; x < W; ++x) {
            const float u = float(x) / float(W - 1);
            const size_t idx = (size_t)z * W + x;
            
            float r = 96.0f, g = 96.0f, b = 92.0f;
            for (size_t li = 0; li < s.layers.size(); ++li) {
                const float w = float(s.weights[li][idx]) / 255.0f;
                if (w <= 0.003f || !samples[li].tex) continue;
                const DecodedTexture& dt = *samples[li].tex;
                const float tu = u * samples[li].repeat_x;
                const float tv = v * samples[li].repeat_z;
                const int tx =
                    int((tu - std::floor(tu)) * float(dt.w)) % dt.w;
                const int tz =
                    int((tv - std::floor(tv)) * float(dt.h)) % dt.h;
                const uint8_t* p =
                    dt.rgba.data() + ((size_t)tz * dt.w + tx) * 4;
                r = r * (1.0f - w) + float(p[0]) * w;
                g = g * (1.0f - w) + float(p[1]) * w;
                b = b * (1.0f - w) + float(p[2]) * w;
            }
            uint8_t* out = rgba.data() + idx * 4;
            out[0] = (uint8_t)std::clamp(int(r + 0.5f), 0, 255);
            out[1] = (uint8_t)std::clamp(int(g + 0.5f), 0, 255);
            out[2] = (uint8_t)std::clamp(int(b + 0.5f), 0, 255);
            out[3] = 255;
        }
    }
    return true;
}

bool SaveSidecar(std::string& error) {
    auto& s = st();
    if (s.level_key.empty()) {
        error = "no level loaded";
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(sidecar_dir(), ec);
    if (s.layers.empty()) {
        std::filesystem::remove(sidecar_path(".paint.txt"), ec);
        std::filesystem::remove(sidecar_path(".paint.bin"), ec);
        s.dirty = false;
        return true;
    }
    {
        std::ofstream meta(sidecar_path(".paint.txt"),
                           std::ios::binary | std::ios::trunc);
        if (!meta) {
            error = "cannot write " + sidecar_path(".paint.txt").string();
            return false;
        }
        for (const Layer& layer : s.layers) {
            meta << "LAYER\t" << layer.tex_path << '\t' << layer.tiling
                 << '\n';
        }
    }
    {
        std::ofstream wf(sidecar_path(".paint.bin"),
                         std::ios::binary | std::ios::trunc);
        if (!wf) {
            error = "cannot write " + sidecar_path(".paint.bin").string();
            return false;
        }
        const uint32_t pw = (uint32_t)s.paint_w;
        const uint32_t ph = (uint32_t)s.paint_h;
        const uint32_t count = (uint32_t)s.layers.size();
        wf.write(reinterpret_cast<const char*>(&pw), 4);
        wf.write(reinterpret_cast<const char*>(&ph), 4);
        wf.write(reinterpret_cast<const char*>(&count), 4);
        for (const auto& w : s.weights) {
            wf.write(reinterpret_cast<const char*>(w.data()),
                     (std::streamsize)w.size());
        }
        if (!wf) {
            error = "short write to " +
                    sidecar_path(".paint.bin").string();
            return false;
        }
    }
    s.dirty = false;
    return true;
}

}
