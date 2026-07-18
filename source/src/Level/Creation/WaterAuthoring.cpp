#include "WaterAuthoring.h"

#include "LandscapeAuthoring.h"
#include "NewLevel.h"

#include "BNKCore.cpp"
#include "Level/Core/LevelLoader.h"
#include "Level/Environment/WaterParser.h"
#include "Level/Terrain/TerrainEdit.h"
#include "UI/OutputLog.h"
#include "Utilities/GameBackup.h"
#include "Utilities/State.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>

namespace Level {
namespace Creation {

namespace {

constexpr uint32_t kMarker = 0x00000FECu;

void be_u32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(uint8_t(v >> 24));
    out.push_back(uint8_t(v >> 16));
    out.push_back(uint8_t(v >> 8));
    out.push_back(uint8_t(v));
}

void be_f32(std::vector<uint8_t>& out, float f) {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    be_u32(out, v);
}

void put_strz(std::vector<uint8_t>& out, const std::string& s) {
    out.insert(out.end(), s.begin(), s.end());
    out.push_back(0);
}


std::filesystem::path level_water_path(const FlatAssetEntry& entry) {
    if (entry.bnk_path.empty()) return {};
    std::error_code ec;
    if (!std::filesystem::is_directory(entry.bnk_path, ec)) return {};
    const std::filesystem::path scenario_dir =
        (std::filesystem::path(entry.bnk_path) / entry.full_path)
            .parent_path();
    for (std::filesystem::directory_iterator it(scenario_dir, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        std::string ext = it->path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) {
                           return (char)std::tolower(c);
                       });
        if (ext == ".water") return it->path();
    }
    return {};
}



bool template_water_body(WaterBody& out, std::string& error) {
    static bool s_tried = false;
    static bool s_ok = false;
    static WaterBody s_body;
    if (s_tried) {
        if (s_ok) out = s_body;
        else error = "no vanilla .water template found in streaming.bnk";
        return s_ok;
    }
    s_tried = true;

    const std::string data_dir = ResolveGameDataDir();
    if (data_dir.empty()) {
        error = "open a game folder first";
        return false;
    }
    const std::string streaming =
        (std::filesystem::path(data_dir) / "streaming.bnk").string();
    try {
        const BnkCache::Entry bnk = BnkCache::get(streaming);
        const auto& files = bnk.reader->list_files();
        for (size_t i = 0; i < files.size(); ++i) {
            const std::string& name = files[i].name;
            if (name.size() < 6 ||
                name.compare(name.size() - 6, 6, ".water") != 0) {
                continue;
            }
            if (files[i].uncompressed_size < 200) continue;
            std::vector<uint8_t> bytes;
            try {
                bytes = BnkCache::extract_bytes(streaming, (int)i);
            } catch (...) {
                continue;
            }
            WaterScene scene;
            if (ParseWaterFile(bytes, scene) && !scene.bodies.empty()) {
                s_body = scene.bodies.front();
                s_body.tiles.clear();
                s_ok = true;
                out = s_body;
                OutputLog::info("water: cloned parameters from " + name);
                return true;
            }
        }
    } catch (const std::exception& ex) {
        error = std::string("streaming.bnk: ") + ex.what();
        return false;
    }
    error = "no vanilla .water template found in streaming.bnk";
    return false;
}

std::vector<uint8_t> serialize_body(const WaterBody& body) {
    std::vector<uint8_t> blob;
    be_u32(blob, kMarker);
    be_f32(blob, body.param_a);
    be_f32(blob, body.base_height);
    for (float value : body.params) be_f32(blob, value);
    put_strz(blob, body.normal_map_path);
    put_strz(blob, body.secondary_map_path);
    be_u32(blob, (uint32_t)body.tiles.size());
    for (const WaterTile& tile : body.tiles) {
        be_u32(blob, kMarker);
        be_f32(blob, tile.cx);
        be_f32(blob, tile.cz);
        be_f32(blob, tile.ex);
        be_f32(blob, tile.ez);
        be_f32(blob, float(tile.cells_x));
        be_f32(blob, float(tile.cells_z));
        be_u32(blob, (uint32_t)tile.aux.size());
        for (uint32_t value : tile.aux) be_u32(blob, value);
        be_u32(blob, (uint32_t)tile.mask.size());
        blob.insert(blob.end(), tile.mask.begin(), tile.mask.end());
        be_u32(blob, kMarker);
    }
    be_u32(blob, 0);
    return blob;
}

std::vector<uint8_t> serialize_scene(const WaterScene& scene) {
    std::vector<std::vector<uint8_t>> bodies;
    bodies.reserve(scene.bodies.size());
    for (const WaterBody& body : scene.bodies) {
        bodies.push_back(serialize_body(body));
    }
    std::vector<uint8_t> out;
    be_u32(out, 2);
    be_u32(out, (uint32_t)bodies.size());
    uint32_t offset = 8 + uint32_t(bodies.size()) * 4;
    for (const auto& body : bodies) {
        be_u32(out, offset);
        offset += (uint32_t)body.size();
    }
    for (const auto& body : bodies) {
        out.insert(out.end(), body.begin(), body.end());
    }
    return out;
}

bool read_water_scene(const FlatAssetEntry& entry, WaterScene& scene,
                      std::filesystem::path& path, std::string& error) {
    path = level_water_path(entry);
    if (path.empty()) {
        error = "no .water file found next to the .engine_level";
        return false;
    }
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        error = "could not read " + path.string();
        return false;
    }
    const std::streamoff size = file.tellg();
    if (size <= 0) {
        error = "the level has no water plane";
        return false;
    }
    std::vector<uint8_t> bytes((size_t)size);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!file || !ParseWaterFile(bytes, scene) || scene.bodies.empty()) {
        error = "the level has no editable water plane";
        return false;
    }
    return true;
}

bool write_water_scene(const std::filesystem::path& path,
                       const WaterScene& scene, std::string& error) {
    const std::vector<uint8_t> bytes = serialize_scene(scene);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file ||
        !file.write(reinterpret_cast<const char*>(bytes.data()),
                    (std::streamsize)bytes.size())) {
        error = "could not write " + path.string();
        return false;
    }
    return true;
}

bool water_bounds(const WaterScene& scene, float& min_x, float& max_x,
                  float& min_z, float& max_z) {
    min_x = std::numeric_limits<float>::max();
    min_z = std::numeric_limits<float>::max();
    max_x = -std::numeric_limits<float>::max();
    max_z = -std::numeric_limits<float>::max();
    bool found = false;
    for (const WaterBody& body : scene.bodies) {
        for (const WaterTile& tile : body.tiles) {
            const float half_x = std::abs(tile.ex) * 0.5f;
            const float half_z = std::abs(tile.ez) * 0.5f;
            min_x = std::min(min_x, tile.cx - half_x);
            max_x = std::max(max_x, tile.cx + half_x);
            min_z = std::min(min_z, tile.cz - half_z);
            max_z = std::max(max_z, tile.cz + half_z);
            found = true;
        }
    }
    return found;
}

}

bool CreateWaterPlane(const FlatAssetEntry& entry, float height,
                      std::string& error) {
    if (!GameBackup::RequireBackup(error)) return false;
    if (!IsCustomLooseLevel(entry)) {
        error = "Water authoring only works on loose custom levels.";
        return false;
    }
    const std::filesystem::path water_path = level_water_path(entry);
    if (water_path.empty()) {
        error = "no .water file found next to the .engine_level";
        return false;
    }
    const TerrainEdit::State& ts = TerrainEdit::Get();
    if (!ts.loaded || ts.width < 2 || ts.height < 2) {
        error = "load the level's terrain first";
        return false;
    }

    WaterBody body;
    if (!template_water_body(body, error)) return false;
    body.base_height = height;

    
    const float span_x = float(ts.width - 1) * ts.tile_size;
    const float span_z = float(ts.height - 1) * ts.tile_size;
    WaterTile tile;
    tile.cx = span_x * 0.5f;
    tile.cz = span_z * 0.5f;
    tile.ex = span_x;
    tile.ez = span_z;
    tile.cells_x = std::clamp(int(span_x / 8.0f), 4, 128);
    tile.cells_z = std::clamp(int(span_z / 8.0f), 4, 128);
    tile.mask.assign((size_t)tile.cells_x * (size_t)tile.cells_z, 1);
    body.tiles.clear();
    body.tiles.push_back(std::move(tile));

    WaterScene scene;
    scene.version = 2;
    scene.bodies.push_back(std::move(body));
    if (!write_water_scene(water_path, scene, error)) return false;

    BnkCache::invalidate(entry.bnk_path);
    OutputLog::success("water: plane created at height " +
                       std::to_string(height) + "; reloading level");
    Level::OpenAsync(entry);
    return true;
}

bool GetWaterPlaneSettings(const FlatAssetEntry& entry,
                           WaterPlaneSettings& settings,
                           std::string& error) {
    WaterScene scene;
    std::filesystem::path path;
    if (!read_water_scene(entry, scene, path, error)) return false;
    float min_x = 0.0f;
    float max_x = 0.0f;
    float min_z = 0.0f;
    float max_z = 0.0f;
    if (!water_bounds(scene, min_x, max_x, min_z, max_z)) {
        error = "the level has no editable water tiles";
        return false;
    }
    settings.width = max_x - min_x;
    settings.length = max_z - min_z;
    settings.height = scene.bodies.front().base_height;
    settings.center_x = (min_x + max_x) * 0.5f;
    settings.center_z = (min_z + max_z) * 0.5f;
    return true;
}

bool UpdateWaterPlane(const FlatAssetEntry& entry,
                      const WaterPlaneSettings& settings,
                      std::string& error) {
    if (!GameBackup::RequireBackup(error)) return false;
    if (!IsCustomLooseLevel(entry)) {
        error = "Water authoring only works on loose custom levels.";
        return false;
    }
    if (!std::isfinite(settings.width) ||
        !std::isfinite(settings.length) ||
        !std::isfinite(settings.height) ||
        !std::isfinite(settings.center_x) ||
        !std::isfinite(settings.center_z) || settings.width <= 0.0f ||
        settings.length <= 0.0f || settings.width > 1000000.0f ||
        settings.length > 1000000.0f) {
        error = "water width and length must be greater than zero";
        return false;
    }
    WaterScene scene;
    std::filesystem::path path;
    if (!read_water_scene(entry, scene, path, error)) return false;
    float min_x = 0.0f;
    float max_x = 0.0f;
    float min_z = 0.0f;
    float max_z = 0.0f;
    if (!water_bounds(scene, min_x, max_x, min_z, max_z)) {
        error = "the level has no editable water tiles";
        return false;
    }
    const float old_width = max_x - min_x;
    const float old_length = max_z - min_z;
    const float old_center_x = (min_x + max_x) * 0.5f;
    const float old_center_z = (min_z + max_z) * 0.5f;
    const float scale_x = old_width > 0.0001f
                              ? settings.width / old_width
                              : 1.0f;
    const float scale_z = old_length > 0.0001f
                              ? settings.length / old_length
                              : 1.0f;
    for (WaterBody& body : scene.bodies) {
        body.base_height = settings.height;
        for (WaterTile& tile : body.tiles) {
            tile.cx = settings.center_x +
                      (tile.cx - old_center_x) * scale_x;
            tile.cz = settings.center_z +
                      (tile.cz - old_center_z) * scale_z;
            tile.ex *= scale_x;
            tile.ez *= scale_z;
        }
    }
    if (!write_water_scene(path, scene, error)) return false;
    BnkCache::invalidate(entry.bnk_path);
    OutputLog::success("water: position, size, and height updated; reloading level");
    Level::OpenAsync(entry);
    return true;
}

bool RemoveWater(const FlatAssetEntry& entry, std::string& error) {
    if (!GameBackup::RequireBackup(error)) return false;
    if (!IsCustomLooseLevel(entry)) {
        error = "Water authoring only works on loose custom levels.";
        return false;
    }
    const std::filesystem::path water_path = level_water_path(entry);
    if (water_path.empty()) {
        error = "no .water file found next to the .engine_level";
        return false;
    }
    
    const uint8_t kEmpty[8] = {0, 0, 0, 2, 0, 0, 0, 0};
    std::ofstream f(water_path, std::ios::binary | std::ios::trunc);
    if (!f || !f.write(reinterpret_cast<const char*>(kEmpty), 8)) {
        error = "could not write " + water_path.string();
        return false;
    }
    f.close();
    BnkCache::invalidate(entry.bnk_path);
    OutputLog::success("water: removed; reloading level");
    Level::OpenAsync(entry);
    return true;
}

}
}
