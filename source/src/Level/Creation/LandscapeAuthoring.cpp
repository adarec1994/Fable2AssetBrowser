#include "LandscapeAuthoring.h"

#include "TiffHeightmap.h"

#include "BNKCore.cpp"
#include "Level/Core/LevelLoader.h"
#include "Level/Terrain/TerrainEdit.h"
#include "UI/OutputLog.h"
#include "Utilities/GameBackup.h"
#include "Utilities/State.h"

#include "stb_image.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>
#include <zlib.h>

namespace Level {
namespace Creation {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });
    return s;
}

std::string backslash(std::string s) {
    std::replace(s.begin(), s.end(), '/', '\\');
    return s;
}

void be_u32(std::string& out, uint32_t v) {
    out.push_back(char((v >> 24) & 0xFF));
    out.push_back(char((v >> 16) & 0xFF));
    out.push_back(char((v >> 8) & 0xFF));
    out.push_back(char(v & 0xFF));
}

void be_f32(std::string& out, float f) {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    be_u32(out, v);
}

bool gzip_compress(const std::string& raw, const std::string& embedded_name,
                   std::vector<uint8_t>& out, std::string& error) {
    z_stream z;
    std::memset(&z, 0, sizeof(z));
    if (deflateInit2(&z, Z_BEST_COMPRESSION, Z_DEFLATED, 15 + 16, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK) {
        error = "deflateInit2 failed";
        return false;
    }
    gz_header hdr;
    std::memset(&hdr, 0, sizeof(hdr));
    std::string name_copy = embedded_name;
    hdr.name = reinterpret_cast<Bytef*>(name_copy.data());
    hdr.os = 0;
    deflateSetHeader(&z, &hdr);

    out.resize(deflateBound(&z, (uLong)raw.size()) + embedded_name.size() +
               64);
    z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(raw.data()));
    z.avail_in = (uInt)raw.size();
    z.next_out = out.data();
    z.avail_out = (uInt)out.size();
    const int ret = deflate(&z, Z_FINISH);
    if (ret != Z_STREAM_END) {
        deflateEnd(&z);
        error = "deflate failed";
        return false;
    }
    out.resize(out.size() - z.avail_out);
    deflateEnd(&z);
    return true;
}


std::filesystem::path region_dir(const FlatAssetEntry& entry,
                                 std::string* region_out) {
    std::error_code ec;
    if (entry.bnk_path.empty() ||
        !std::filesystem::is_directory(entry.bnk_path, ec)) {
        return {};
    }
    const std::string vpath = lower(backslash(entry.full_path));
    const std::string prefix = "worlds\\albion\\";
    if (vpath.rfind(prefix, 0) != 0) return {};
    const size_t region_end = vpath.find('\\', prefix.size());
    if (region_end == std::string::npos) return {};
    const std::string region =
        vpath.substr(prefix.size(), region_end - prefix.size());
    if (region.empty()) return {};
    const std::filesystem::path dir = std::filesystem::path(entry.bnk_path) /
                                      "worlds" / "albion" / region;
    if (!std::filesystem::is_directory(dir, ec)) return {};
    if (region_out) *region_out = region;
    return dir;
}

std::string find_hfid(const std::filesystem::path& dir) {
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::filesystem::path p = it->path();
        if (lower(p.extension().string()) == ".ghf") {
            return p.stem().string();
        }
    }
    return {};
}

bool clamp_params(LandscapeParams p, LandscapeParams& out,
                  std::string& error) {
    if (p.grid_w < 2 || p.grid_h < 2 || p.grid_w > 1025 || p.grid_h > 1025) {
        error = "Grid size must be between 2x2 and 1025x1025 samples.";
        return false;
    }
    if (p.tile_size < 0.0f || p.tile_size > 256.0f) {
        error = "Tile size must be between 0 and 256 metres.";
        return false;
    }
    out = p;
    return true;
}



bool write_ghf(const FlatAssetEntry& entry, const std::string& region,
               int grid_w, int grid_h, float tile_size,
               const std::vector<float>& heights, std::string& error) {
    const std::filesystem::path dir = region_dir(entry, nullptr);
    if (dir.empty()) {
        error = "Not a loose custom level.";
        return false;
    }
    const std::string hfid = find_hfid(dir);
    if (hfid.empty()) {
        error = "No .ghf heightfield found in " + dir.string();
        return false;
    }

    static const uint8_t kCellTail[10] = {
        0x00, 0x00, 0x00, 0x00, 0xCA, 0xD8, 0x17, 0x57, 0x00, 0x00};
    std::string raw;
    raw.reserve(0x14 + heights.size() * 14);
    be_f32(raw, tile_size);
    be_u32(raw, 0);
    be_u32(raw, 0);
    be_u32(raw, (uint32_t)grid_w);
    be_u32(raw, (uint32_t)grid_h);
    for (const float h : heights) {
        be_f32(raw, h);
        raw.append(reinterpret_cast<const char*>(kCellTail),
                   sizeof(kCellTail));
    }

    const std::string embedded = "export_xbox360\\worlds\\albion\\" +
                                 region + "\\" + hfid + ".ghf";
    std::vector<uint8_t> gz;
    if (!gzip_compress(raw, embedded, gz, error)) return false;

    const std::filesystem::path out_path = dir / (hfid + ".ghf");
    std::ofstream f(out_path, std::ios::binary | std::ios::trunc);
    for (int attempt = 0; attempt < 4 && !f; ++attempt) {
        
        
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        f.clear();
        f.open(out_path, std::ios::binary | std::ios::trunc);
    }
    if (!f) {
        error = "cannot write " + out_path.string() +
                " (locked or still syncing?)";
        return false;
    }
    f.write(reinterpret_cast<const char*>(gz.data()),
            (std::streamsize)gz.size());
    if (!f) {
        error = "write failed for " + out_path.string();
        return false;
    }

    BnkCache::invalidate(entry.bnk_path);
    return true;
}

void reload_level(const FlatAssetEntry& entry) {
    Level::OpenAsync(entry);
}



bool load_heightmap(const std::string& path, std::vector<float>& values,
                    int& iw, int& ih, std::string& error) {
    const std::string ext =
        lower(std::filesystem::path(path).extension().string());
    if (ext == ".tif" || ext == ".tiff") {
        if (!LoadTiffHeightmap(path, values, iw, ih, error)) return false;
        if (iw < 2 || ih < 2) {
            error = "heightmap is too small";
            return false;
        }
        return true;
    }
    if (ext == ".raw" || ext == ".r16") {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f) {
            error = "cannot open " + path;
            return false;
        }
        const std::streamoff bytes = f.tellg();
        const size_t samples = (size_t)bytes / 2;
        const size_t side = (size_t)std::llround(std::sqrt((double)samples));
        if (bytes <= 0 || (bytes & 1) || side < 2 ||
            side * side != samples) {
            error = "raw heightmaps must be square 16-bit files";
            return false;
        }
        std::vector<uint16_t> raw(samples);
        f.seekg(0);
        f.read(reinterpret_cast<char*>(raw.data()), bytes);
        if (!f) {
            error = "short read from " + path;
            return false;
        }
        iw = ih = (int)side;
        values.resize(samples);
        for (size_t i = 0; i < samples; ++i) {
            values[i] = float(raw[i]) / 65535.0f;
        }
        return true;
    }

    int comp = 0;
    stbi_us* pixels = stbi_load_16(path.c_str(), &iw, &ih, &comp, 1);
    if (!pixels) {
        error = std::string("could not decode image: ") +
                stbi_failure_reason();
        return false;
    }
    const size_t samples = (size_t)iw * (size_t)ih;
    values.resize(samples);
    for (size_t i = 0; i < samples; ++i) {
        values[i] = float(pixels[i]) / 65535.0f;
    }
    stbi_image_free(pixels);
    if (iw < 2 || ih < 2) {
        error = "heightmap is too small";
        return false;
    }
    return true;
}

float sample_bilinear(const std::vector<float>& v, int iw, int ih,
                      float u, float t) {
    const float fx = u * float(iw - 1);
    const float fy = t * float(ih - 1);
    const int x0 = (int)fx;
    const int y0 = (int)fy;
    const int x1 = std::min(x0 + 1, iw - 1);
    const int y1 = std::min(y0 + 1, ih - 1);
    const float ax = fx - float(x0);
    const float ay = fy - float(y0);
    const float top = v[(size_t)y0 * iw + x0] * (1.0f - ax) +
                      v[(size_t)y0 * iw + x1] * ax;
    const float bot = v[(size_t)y1 * iw + x0] * (1.0f - ax) +
                      v[(size_t)y1 * iw + x1] * ax;
    return top * (1.0f - ay) + bot * ay;
}

}

bool IsCustomLooseLevel(const FlatAssetEntry& entry, std::string* region) {
    
    
    const std::filesystem::path dir = region_dir(entry, region);
    return !dir.empty() && !find_hfid(dir).empty();
}

bool CreateFlatLandscape(const FlatAssetEntry& entry,
                         const LandscapeParams& params,
                         std::string& error) {
    if (!GameBackup::RequireBackup(error)) return false;
    std::string region;
    if (region_dir(entry, &region).empty()) {
        error = "Terrain authoring only works on loose custom levels.";
        return false;
    }
    LandscapeParams p;
    if (!clamp_params(params, p, error)) return false;

    std::vector<float> heights((size_t)p.grid_w * (size_t)p.grid_h,
                               p.base_height);
    if (!write_ghf(entry, region, p.grid_w, p.grid_h, p.tile_size, heights,
                   error)) {
        return false;
    }

    OutputLog::success("landscape: flat " + std::to_string(p.grid_w) + "x" +
                       std::to_string(p.grid_h) + " terrain written for '" +
                       region + "'; reloading level");
    reload_level(entry);
    return true;
}

bool SaveSculptedHeights(const FlatAssetEntry& entry, std::string& error) {
    if (!GameBackup::RequireBackup(error)) return false;
    std::string region;
    if (region_dir(entry, &region).empty()) {
        error = "Terrain authoring only works on loose custom levels.";
        return false;
    }
    const TerrainEdit::State& ts = TerrainEdit::Get();
    if (!ts.loaded || ts.width < 2 || ts.height < 2 ||
        ts.heights_current.size() !=
            (size_t)ts.width * (size_t)ts.height) {
        error = "no sculpted terrain loaded";
        return false;
    }
    
    
    float tile = ts.tile_size;
    if (ts.ghf_payload_original.size() >= 4) {
        uint32_t bits = (uint32_t(ts.ghf_payload_original[0]) << 24) |
                        (uint32_t(ts.ghf_payload_original[1]) << 16) |
                        (uint32_t(ts.ghf_payload_original[2]) << 8) |
                        uint32_t(ts.ghf_payload_original[3]);
        std::memcpy(&tile, &bits, 4);
    }
    if (!write_ghf(entry, region, ts.width, ts.height, tile,
                   ts.heights_current, error)) {
        return false;
    }
    TerrainEdit::MarkSaved();
    return true;
}

bool ImportHeightmapLandscape(const FlatAssetEntry& entry,
                              const LandscapeParams& params,
                              const std::string& image_path,
                              std::string& error) {
    if (!GameBackup::RequireBackup(error)) return false;
    std::string region;
    if (region_dir(entry, &region).empty()) {
        error = "Terrain authoring only works on loose custom levels.";
        return false;
    }
    LandscapeParams p;
    if (!clamp_params(params, p, error)) return false;
    if (p.max_height < p.min_height) std::swap(p.max_height, p.min_height);

    std::vector<float> image;
    int iw = 0;
    int ih = 0;
    if (!load_heightmap(image_path, image, iw, ih, error)) return false;

    std::vector<float> heights((size_t)p.grid_w * (size_t)p.grid_h);
    const float span = p.max_height - p.min_height;
    for (int y = 0; y < p.grid_h; ++y) {
        const float t = p.grid_h > 1 ? float(y) / float(p.grid_h - 1) : 0.0f;
        for (int x = 0; x < p.grid_w; ++x) {
            const float u =
                p.grid_w > 1 ? float(x) / float(p.grid_w - 1) : 0.0f;
            heights[(size_t)y * p.grid_w + x] =
                p.min_height + sample_bilinear(image, iw, ih, u, t) * span;
        }
    }
    if (!write_ghf(entry, region, p.grid_w, p.grid_h, p.tile_size, heights,
                   error)) {
        return false;
    }

    OutputLog::success(
        "landscape: heightmap " +
        std::filesystem::path(image_path).filename().string() + " (" +
        std::to_string(iw) + "x" + std::to_string(ih) + ") imported as " +
        std::to_string(p.grid_w) + "x" + std::to_string(p.grid_h) +
        " terrain for '" + region + "'; reloading level");
    reload_level(entry);
    return true;
}

}
}
