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
#include <cstring>
#include <filesystem>
#include <fstream>

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
    tile.ex = span_x * 0.5f;
    tile.ez = span_z * 0.5f;
    tile.cells_x = std::clamp(int(span_x / 8.0f), 4, 128);
    tile.cells_z = std::clamp(int(span_z / 8.0f), 4, 128);
    tile.mask.assign((size_t)tile.cells_x * (size_t)tile.cells_z, 1);
    body.tiles.clear();
    body.tiles.push_back(std::move(tile));

    
    std::vector<uint8_t> blob;
    be_u32(blob, kMarker);
    be_f32(blob, body.param_a);
    be_f32(blob, body.base_height);
    for (float v : body.params) be_f32(blob, v);
    put_strz(blob, body.normal_map_path);
    put_strz(blob, body.secondary_map_path);
    be_u32(blob, (uint32_t)body.tiles.size());
    for (const WaterTile& t : body.tiles) {
        be_u32(blob, kMarker);
        be_f32(blob, t.cx);
        be_f32(blob, t.cz);
        be_f32(blob, t.ex);
        be_f32(blob, t.ez);
        be_f32(blob, float(t.cells_x));
        be_f32(blob, float(t.cells_z));
        be_u32(blob, (uint32_t)t.aux.size());
        for (uint32_t a : t.aux) be_u32(blob, a);
        be_u32(blob, (uint32_t)t.mask.size());
        blob.insert(blob.end(), t.mask.begin(), t.mask.end());
        be_u32(blob, kMarker);
    }
    be_u32(blob, 0);   

    std::vector<uint8_t> out;
    out.reserve(12 + blob.size());
    be_u32(out, 2);    
    be_u32(out, 1);    
    be_u32(out, 12);   
    out.insert(out.end(), blob.begin(), blob.end());

    std::ofstream f(water_path, std::ios::binary | std::ios::trunc);
    if (!f ||
        !f.write(reinterpret_cast<const char*>(out.data()),
                 (std::streamsize)out.size())) {
        error = "could not write " + water_path.string();
        return false;
    }
    f.close();

    BnkCache::invalidate(entry.bnk_path);
    OutputLog::success("water: plane created at height " +
                       std::to_string(height) + "; reloading level");
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
