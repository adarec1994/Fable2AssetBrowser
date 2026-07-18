#include "TerrainPaint.h"

#include "../../BNKCore.cpp"
#include "../../UI/ModelPreview.h"
#include "../../UI/OutputLog.h"
#include "../../Utilities/State.h"

#include <algorithm>
#include <cctype>
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
    bool stroke_active = false;
    float stroke_x = 0.0f;
    float stroke_z = 0.0f;
    int stroke_layer = -1;
    bool stroke_erase = false;
    bool render_layers_dirty = true;
    uint32_t render_weight_dirty_mask = 0xFFFFFFFFu;

    std::unordered_map<std::string, DecodedTexture> tex_cache;
};

State& st() {
    static State s;
    return s;
}

#ifdef _WIN32
struct GpuState {
    RenderResources resources;
    ID3D11Texture2D* weights_texture = nullptr;
};

GpuState& gpu() {
    static GpuState value;
    return value;
}

void release_gpu() {
    auto& value = gpu();
    for (int i = 0; i < kMaxLayers; ++i) {
        if (value.resources.diffuse[i]) {
            value.resources.diffuse[i]->Release();
        }
        if (value.resources.normal[i]) {
            value.resources.normal[i]->Release();
        }
    }
    if (value.resources.weights) value.resources.weights->Release();
    if (value.weights_texture) value.weights_texture->Release();
    value = GpuState{};
}
#endif

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
    const FlatAssetEntry* best = nullptr;
    for (const FlatAssetEntry& e : S.all_tex_files) {
        if (e.full_path != tex_path) continue;
        std::string bank = e.bnk_path;
        std::transform(bank.begin(), bank.end(), bank.begin(),
                       [](unsigned char c) {
                           return (char)std::tolower(c);
                       });
        if (bank.find("texture_header") != std::string::npos) continue;
        if (!best || e.size > best->size) best = &e;
    }
    if (best) {
        try {
            const std::vector<uint8_t> blob =
                BnkCache::extract_bytes(best->bnk_path, best->file_index);
            std::vector<unsigned char> copy(blob.begin(), blob.end());
            bool has_alpha = false;
            dt.ok = decode_tex_to_rgba(copy, dt.rgba, dt.w, dt.h,
                                       &has_alpha, -1);
        } catch (...) {
            dt.ok = false;
        }
    }
    if (!dt.ok) {
        OutputLog::warn("paint: could not decode texture " + tex_path);
    }
    return s.tex_cache.emplace(tex_path, std::move(dt)).first->second;
}

std::vector<uint8_t> resize_weights(const std::vector<uint8_t>& source,
                                    int source_w, int source_h,
                                    int target_w, int target_h) {
    std::vector<uint8_t> target((size_t)target_w * target_h, 0);
    for (int y = 0; y < target_h; ++y) {
        const float sy = target_h > 1
                             ? float(y) * float(source_h - 1) /
                                   float(target_h - 1)
                             : 0.0f;
        const int y0 = std::clamp((int)std::floor(sy), 0, source_h - 1);
        const int y1 = std::min(y0 + 1, source_h - 1);
        const float fy = sy - float(y0);
        for (int x = 0; x < target_w; ++x) {
            const float sx = target_w > 1
                                 ? float(x) * float(source_w - 1) /
                                       float(target_w - 1)
                                 : 0.0f;
            const int x0 = std::clamp((int)std::floor(sx), 0, source_w - 1);
            const int x1 = std::min(x0 + 1, source_w - 1);
            const float fx = sx - float(x0);
            const float a = float(source[(size_t)y0 * source_w + x0]);
            const float b = float(source[(size_t)y0 * source_w + x1]);
            const float c = float(source[(size_t)y1 * source_w + x0]);
            const float d = float(source[(size_t)y1 * source_w + x1]);
            const float top = a + (b - a) * fx;
            const float bottom = c + (d - c) * fx;
            target[(size_t)y * target_w + x] = (uint8_t)std::clamp(
                int(top + (bottom - top) * fy + 0.5f), 0, 255);
        }
    }
    return target;
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
        const size_t t3 = t2 == std::string::npos
                              ? std::string::npos
                              : line.find('\t', t2 + 1);
        Layer layer;
        layer.tex_path = line.substr(
            t1 + 1, t2 == std::string::npos ? std::string::npos
                                            : t2 - t1 - 1);
        if (t2 != std::string::npos) {
            layer.tiling = std::strtof(line.c_str() + t2 + 1, nullptr);
            if (layer.tiling <= 0.0f) layer.tiling = 8.0f;
        }
        if (t3 != std::string::npos) {
            layer.normal_path = line.substr(t3 + 1);
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
        const uint64_t source_area = uint64_t(pw) * uint64_t(ph);
        if (wf && count == (uint32_t)s.layers.size() &&
            pw > 0 && ph > 0 && pw <= 8192 && ph <= 8192 &&
            source_area * uint64_t(count) <=
                128ull * 1024ull * 1024ull) {
            std::vector<std::vector<uint8_t>> loaded(
                s.layers.size(),
                std::vector<uint8_t>((size_t)source_area, 0));
            for (auto& w : loaded) {
                wf.read(reinterpret_cast<char*>(w.data()),
                        (std::streamsize)w.size());
            }
            if (wf) {
                if (pw == (uint32_t)s.paint_w &&
                    ph == (uint32_t)s.paint_h) {
                    s.weights = std::move(loaded);
                } else {
                    s.weights.reserve(loaded.size());
                    for (const auto& w : loaded) {
                        s.weights.push_back(resize_weights(
                            w, (int)pw, (int)ph,
                            s.paint_w, s.paint_h));
                    }
                }
            }
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
#ifdef _WIN32
    release_gpu();
#endif
    s = State{};
    s.level_key = level_key;
    s.grid_w = std::max(2, grid_w);
    s.grid_h = std::max(2, grid_h);
    s.tile_size = tile_size > 0.0f ? tile_size : 1.0f;
    s.paint_w = s.grid_w;
    s.paint_h = s.grid_h;
    load_sidecar();
}

void Clear() {
#ifdef _WIN32
    release_gpu();
#endif
    st() = State{};
}

bool Active() {
    return !st().level_key.empty() && !st().layers.empty();
}

const std::vector<Layer>& Layers() { return st().layers; }

#ifdef _WIN32
const RenderResources& GetRenderResources() {
    return gpu().resources;
}

bool SyncRenderResources(ID3D11Device* device) {
    auto& s = st();
    if (!device || s.level_key.empty() || s.layers.empty() ||
        s.layers.size() != s.weights.size()) {
        return false;
    }

    if (s.render_layers_dirty) {
        release_gpu();
        auto& resources = gpu().resources;
        resources.layer_count =
            std::min((int)s.layers.size(), kMaxLayers);
        for (int i = 0; i < resources.layer_count; ++i) {
            const Layer& layer = s.layers[(size_t)i];
            const DecodedTexture& diffuse = decoded_texture(layer.tex_path);
            if (diffuse.ok) {
                resources.diffuse[i] = create_srv_from_rgba_mipped(
                    device, diffuse.w, diffuse.h, diffuse.rgba);
            }
            if (!layer.normal_path.empty()) {
                const DecodedTexture& normal =
                    decoded_texture(layer.normal_path);
                if (normal.ok) {
                    resources.normal[i] = create_srv_from_rgba_mipped(
                        device, normal.w, normal.h, normal.rgba);
                }
                if (resources.normal[i]) {
                    resources.normal_mask |= uint32_t(1u << i);
                }
            }
            resources.tile_scale[i] =
                1.0f / std::max(layer.tiling, 0.5f);
        }
        static uint32_t next_generation = 1;
        resources.generation = next_generation++;
        s.render_layers_dirty = false;
        s.render_weight_dirty_mask =
            (1u << resources.layer_count) - 1u;
    }

    auto& resources = gpu().resources;
    const bool recreate_weights =
        !gpu().weights_texture || !resources.weights ||
        resources.weight_w != s.paint_w ||
        resources.weight_h != s.paint_h ||
        resources.layer_count != (int)s.layers.size();
    if (s.render_weight_dirty_mask != 0 && recreate_weights) {
        if (resources.weights) {
            resources.weights->Release();
            resources.weights = nullptr;
        }
        if (gpu().weights_texture) {
            gpu().weights_texture->Release();
            gpu().weights_texture = nullptr;
        }

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = (UINT)s.paint_w;
        desc.Height = (UINT)s.paint_h;
        desc.MipLevels = 1;
        desc.ArraySize = (UINT)resources.layer_count;
        desc.Format = DXGI_FORMAT_R8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;

        std::vector<D3D11_SUBRESOURCE_DATA> data(
            (size_t)resources.layer_count);
        for (int i = 0; i < resources.layer_count; ++i) {
            data[(size_t)i].pSysMem = s.weights[(size_t)i].data();
            data[(size_t)i].SysMemPitch = (UINT)s.paint_w;
        }
        if (FAILED(device->CreateTexture2D(
                &desc, data.data(), &gpu().weights_texture))) {
            resources.ok = false;
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = desc.Format;
        view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        view.Texture2DArray.MostDetailedMip = 0;
        view.Texture2DArray.MipLevels = 1;
        view.Texture2DArray.FirstArraySlice = 0;
        view.Texture2DArray.ArraySize = desc.ArraySize;
        if (FAILED(device->CreateShaderResourceView(
                gpu().weights_texture, &view, &resources.weights))) {
            gpu().weights_texture->Release();
            gpu().weights_texture = nullptr;
            resources.ok = false;
            return false;
        }
        resources.weight_w = s.paint_w;
        resources.weight_h = s.paint_h;
        s.render_weight_dirty_mask = 0;
    } else if (s.render_weight_dirty_mask != 0) {
        ID3D11DeviceContext* context = nullptr;
        device->GetImmediateContext(&context);
        if (context) {
            for (int i = 0; i < resources.layer_count; ++i) {
                const uint32_t layer_bit = 1u << i;
                if ((s.render_weight_dirty_mask & layer_bit) == 0) continue;
                const UINT subresource = D3D11CalcSubresource(0, (UINT)i, 1);
                context->UpdateSubresource(
                    gpu().weights_texture, subresource, nullptr,
                    s.weights[(size_t)i].data(), (UINT)s.paint_w, 0);
            }
            context->Release();
            s.render_weight_dirty_mask = 0;
        }
    }

    resources.ok = resources.weights && resources.layer_count > 0;
    return resources.ok;
}
#endif

int AddLayer(const std::string& tex_path,
             const std::string& normal_path) {
    auto& s = st();
    if (s.level_key.empty()) return -1;
    if ((int)s.layers.size() >= kMaxLayers) return -1;
    for (const Layer& layer : s.layers) {
        if (layer.tex_path == tex_path) return -1;
    }
    const uint8_t initial_weight = s.layers.empty() ? 255 : 0;
    Layer layer;
    layer.tex_path = tex_path;
    layer.normal_path = normal_path;
    s.layers.push_back(std::move(layer));
    s.weights.emplace_back((size_t)s.paint_w * s.paint_h,
                           initial_weight);
    s.active_layer = (int)s.layers.size() - 1;
    s.dirty = true;
    s.render_layers_dirty = true;
    s.render_weight_dirty_mask = 0xFFFFFFFFu;
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
    s.render_layers_dirty = true;
    s.render_weight_dirty_mask = 0xFFFFFFFFu;
#ifdef _WIN32
    if (s.layers.empty()) release_gpu();
#endif
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

    const float pr_x = radius_m / span_x * float(s.paint_w - 1);
    const float pr_z = radius_m / span_z * float(s.paint_h - 1);
    const float falloff = std::clamp(falloff01, 0.0f, 1.0f);
    const float add = std::clamp(strength01, 0.0f, 1.0f) * 48.0f;
    std::vector<uint8_t>& target = s.weights[(size_t)s.active_layer];

    auto apply_dab = [&](float dab_x, float dab_z) {
        const float px_c = dab_x / span_x * float(s.paint_w - 1);
        const float pz_c = dab_z / span_z * float(s.paint_h - 1);
        const int xmin = std::max(0, int(std::floor(px_c - pr_x)));
        const int xmax =
            std::min(s.paint_w - 1, int(std::ceil(px_c + pr_x)));
        const int zmin = std::max(0, int(std::floor(pz_c - pr_z)));
        const int zmax =
            std::min(s.paint_h - 1, int(std::ceil(pz_c + pr_z)));
        if (xmin > xmax || zmin > zmax) return;
        for (int z = zmin; z <= zmax; ++z) {
            for (int x = xmin; x <= xmax; ++x) {
                const float dx =
                    (float(x) - px_c) / std::max(pr_x, 0.001f);
                const float dz =
                    (float(z) - pz_c) / std::max(pr_z, 0.001f);
                const float dist = std::sqrt(dx * dx + dz * dz);
                if (dist >= 1.0f) continue;
                float w = 1.0f;
                const float fade_start = 1.0f - falloff;
                if (dist > fade_start && falloff > 0.0f) {
                    const float t =
                        1.0f - (dist - fade_start) / falloff;
                    w = t * t * (3.0f - 2.0f * t);
                }
                const size_t idx = (size_t)z * s.paint_w + x;
                const int delta =
                    int(add * w + 0.5f) * (erase ? -1 : 1);
                const int next =
                    std::clamp(int(target[idx]) + delta, 0, 255);
                target[idx] = (uint8_t)next;
            }
        }
    };

    const bool continue_stroke =
        s.stroke_active && s.stroke_layer == s.active_layer &&
        s.stroke_erase == erase;
    const float from_x = continue_stroke ? s.stroke_x : wx;
    const float from_z = continue_stroke ? s.stroke_z : wz;
    const float dx = wx - from_x;
    const float dz = wz - from_z;
    const float distance = std::sqrt(dx * dx + dz * dz);
    const float pixel_size = std::max(
        span_x / float(s.paint_w - 1),
        span_z / float(s.paint_h - 1));
    const float spacing =
        std::max(pixel_size * 0.75f, radius_m * 0.12f);
    const int steps = continue_stroke
                          ? std::clamp((int)std::ceil(distance / spacing),
                                       1, 256)
                          : 1;
    for (int i = 1; i <= steps; ++i) {
        const float t = float(i) / float(steps);
        apply_dab(from_x + dx * t, from_z + dz * t);
    }
    s.stroke_active = true;
    s.stroke_x = wx;
    s.stroke_z = wz;
    s.stroke_layer = s.active_layer;
    s.stroke_erase = erase;
    s.dirty = true;
    s.render_weight_dirty_mask |= 1u << s.active_layer;
}

void EndStroke() { st().stroke_active = false; }

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
                const float txf =
                    (tu - std::floor(tu)) * float(dt.w);
                const float tzf =
                    (tv - std::floor(tv)) * float(dt.h);
                const int tx0 = int(std::floor(txf)) % dt.w;
                const int tz0 = int(std::floor(tzf)) % dt.h;
                const int tx1 = (tx0 + 1) % dt.w;
                const int tz1 = (tz0 + 1) % dt.h;
                const float fx = txf - std::floor(txf);
                const float fz = tzf - std::floor(tzf);
                const uint8_t* p00 = dt.rgba.data() +
                    ((size_t)tz0 * dt.w + tx0) * 4;
                const uint8_t* p10 = dt.rgba.data() +
                    ((size_t)tz0 * dt.w + tx1) * 4;
                const uint8_t* p01 = dt.rgba.data() +
                    ((size_t)tz1 * dt.w + tx0) * 4;
                const uint8_t* p11 = dt.rgba.data() +
                    ((size_t)tz1 * dt.w + tx1) * 4;
                auto sample = [&](int channel) {
                    const float top = float(p00[channel]) +
                        (float(p10[channel]) - float(p00[channel])) * fx;
                    const float bottom = float(p01[channel]) +
                        (float(p11[channel]) - float(p01[channel])) * fx;
                    return top + (bottom - top) * fz;
                };
                r = r * (1.0f - w) + sample(0) * w;
                g = g * (1.0f - w) + sample(1) * w;
                b = b * (1.0f - w) + sample(2) * w;
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

bool BuildNormalComposite(std::vector<uint8_t>& rgba,
                          int& out_w, int& out_h) {
    auto& s = st();
    if (s.layers.empty()) return false;

    const int W = s.paint_w;
    const int H = s.paint_h;
    const float span_x = float(s.grid_w - 1) * s.tile_size;
    const float span_z = float(s.grid_h - 1) * s.tile_size;

    struct LayerSample {
        const DecodedTexture* tex = nullptr;
        float repeat_x = 1.0f;
        float repeat_z = 1.0f;
    };
    std::vector<LayerSample> samples(s.layers.size());
    bool have_normal = false;
    for (size_t li = 0; li < s.layers.size(); ++li) {
        if (s.layers[li].normal_path.empty()) continue;
        const DecodedTexture& dt =
            decoded_texture(s.layers[li].normal_path);
        if (!dt.ok) continue;
        samples[li].tex = &dt;
        samples[li].repeat_x = span_x / s.layers[li].tiling;
        samples[li].repeat_z = span_z / s.layers[li].tiling;
        have_normal = true;
    }
    if (!have_normal) return false;

    out_w = W;
    out_h = H;
    rgba.assign((size_t)W * H * 4, 0);
    for (int z = 0; z < H; ++z) {
        const float v = float(z) / float(H - 1);
        for (int x = 0; x < W; ++x) {
            const float u = float(x) / float(W - 1);
            const size_t idx = (size_t)z * W + x;
            float nx = 0.0f;
            float ny = 0.0f;
            float nz = 1.0f;
            for (size_t li = 0; li < s.layers.size(); ++li) {
                const float weight = float(s.weights[li][idx]) / 255.0f;
                if (weight <= 0.003f || !samples[li].tex) continue;
                const DecodedTexture& dt = *samples[li].tex;
                const float tu = u * samples[li].repeat_x;
                const float tv = v * samples[li].repeat_z;
                const float txf =
                    (tu - std::floor(tu)) * float(dt.w);
                const float tzf =
                    (tv - std::floor(tv)) * float(dt.h);
                const int tx0 = int(std::floor(txf)) % dt.w;
                const int tz0 = int(std::floor(tzf)) % dt.h;
                const int tx1 = (tx0 + 1) % dt.w;
                const int tz1 = (tz0 + 1) % dt.h;
                const float fx = txf - std::floor(txf);
                const float fz = tzf - std::floor(tzf);
                const uint8_t* p00 = dt.rgba.data() +
                    ((size_t)tz0 * dt.w + tx0) * 4;
                const uint8_t* p10 = dt.rgba.data() +
                    ((size_t)tz0 * dt.w + tx1) * 4;
                const uint8_t* p01 = dt.rgba.data() +
                    ((size_t)tz1 * dt.w + tx0) * 4;
                const uint8_t* p11 = dt.rgba.data() +
                    ((size_t)tz1 * dt.w + tx1) * 4;
                auto sample = [&](int channel) {
                    const float top = float(p00[channel]) +
                        (float(p10[channel]) - float(p00[channel])) * fx;
                    const float bottom = float(p01[channel]) +
                        (float(p11[channel]) - float(p01[channel])) * fx;
                    return (top + (bottom - top) * fz) / 127.5f - 1.0f;
                };
                float sx = sample(0);
                float sy = sample(1);
                float sz = sample(2);
                const float sample_length =
                    std::sqrt(sx * sx + sy * sy + sz * sz);
                if (sample_length > 0.0001f) {
                    sx /= sample_length;
                    sy /= sample_length;
                    sz /= sample_length;
                }
                nx = nx * (1.0f - weight) + sx * weight;
                ny = ny * (1.0f - weight) + sy * weight;
                nz = nz * (1.0f - weight) + sz * weight;
            }
            const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (length > 0.0001f) {
                nx /= length;
                ny /= length;
                nz /= length;
            }
            uint8_t* out = rgba.data() + idx * 4;
            out[0] = uint8_t(std::clamp(
                int((nx * 0.5f + 0.5f) * 255.0f + 0.5f), 0, 255));
            out[1] = uint8_t(std::clamp(
                int((ny * 0.5f + 0.5f) * 255.0f + 0.5f), 0, 255));
            out[2] = uint8_t(std::clamp(
                int((nz * 0.5f + 0.5f) * 255.0f + 0.5f), 0, 255));
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
                 << '\t' << layer.normal_path << '\n';
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
