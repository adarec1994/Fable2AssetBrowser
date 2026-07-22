#include "LandscapeAuthoring.h"

#include "TiffHeightmap.h"

#include "BNKCore.cpp"
#include "Level/Core/LevelLoader.h"
#include "Level/IO/BnkWriter.h"
#include "Level/Terrain/TerrainEdit.h"
#include "UI/OutputLog.h"
#include "Utilities/GameBackup.h"
#include "Utilities/State.h"

#include "stb_image.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
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

void be_u64(std::string& out, uint64_t v) {
    be_u32(out, static_cast<uint32_t>(v >> 32));
    be_u32(out, static_cast<uint32_t>(v));
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

bool gzip_decompress(const std::vector<uint8_t>& comp,
                     std::vector<uint8_t>& out) {
    z_stream z;
    std::memset(&z, 0, sizeof(z));
    if (inflateInit2(&z, 15 + 32) != Z_OK) return false;
    z.next_in = const_cast<Bytef*>(comp.data());
    z.avail_in = (uInt)comp.size();
    out.clear();
    std::vector<uint8_t> buf(1 << 16);
    for (;;) {
        z.next_out = buf.data();
        z.avail_out = (uInt)buf.size();
        const int ret = inflate(&z, Z_NO_FLUSH);
        out.insert(out.end(), buf.data(),
                   buf.data() + (buf.size() - z.avail_out));
        if (ret == Z_STREAM_END) break;
        if (ret != Z_OK) {
            inflateEnd(&z);
            return false;
        }
    }
    inflateEnd(&z);
    return true;
}

bool read_file_bytes(const std::filesystem::path& p,
                     std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign((std::istreambuf_iterator<char>(f)),
               std::istreambuf_iterator<char>());
    return !out.empty();
}

uint32_t read_be_u32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

float read_be_f32(const uint8_t* p) {
    const uint32_t bits = read_be_u32(p);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void write_be_f32(uint8_t* p, float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    p[0] = uint8_t(bits >> 24);
    p[1] = uint8_t(bits >> 16);
    p[2] = uint8_t(bits >> 8);
    p[3] = uint8_t(bits);
}

struct NativeLandscapeLayout {
    uint32_t width = 0;
    uint32_t height = 0;
    float sample_spacing = 0.5f;
    std::array<uint8_t, 20> ghf_header{};
};

bool read_native_layout(const std::filesystem::path& root,
                        const std::string& hfid,
                        NativeLandscapeLayout& out,
                        std::string& error) {
    out = {};
    const std::filesystem::path ehf_path =
        root / "defaultscenario" / (hfid + ".ehf");
    std::vector<uint8_t> ehf;
    static constexpr char kMagic[] = "HeightFieldGraphicsFile";
    if (!read_file_bytes(ehf_path, ehf) || ehf.size() < 63 ||
        std::memcmp(ehf.data(), kMagic, sizeof(kMagic) - 1) != 0) {
        error = "cannot read native terrain layout from " +
                ehf_path.string();
        return false;
    }
    out.width = read_be_u32(ehf.data() + 35);
    out.height = read_be_u32(ehf.data() + 39);
    out.sample_spacing = read_be_f32(ehf.data() + 43);
    if (out.width < 2 || out.height < 2 || out.width > 8192 ||
        out.height > 8192 || !std::isfinite(out.sample_spacing) ||
        out.sample_spacing <= 0.0f) {
        error = "native .ehf terrain layout is invalid";
        return false;
    }




    std::memcpy(out.ghf_header.data() + 0, ehf.data() + 27, 8);
    out.ghf_header[12] = uint8_t(out.width >> 24);
    out.ghf_header[13] = uint8_t(out.width >> 16);
    out.ghf_header[14] = uint8_t(out.width >> 8);
    out.ghf_header[15] = uint8_t(out.width);
    out.ghf_header[16] = uint8_t(out.height >> 24);
    out.ghf_header[17] = uint8_t(out.height >> 16);
    out.ghf_header[18] = uint8_t(out.height >> 8);
    out.ghf_header[19] = uint8_t(out.height);

    std::vector<uint8_t> hdb;
    if (read_file_bytes(root / (hfid + ".hdb"), hdb) && hdb.size() >= 32 &&
        read_be_u32(hdb.data() + 12) == out.width &&
        read_be_u32(hdb.data() + 16) == out.height) {
        std::copy_n(hdb.data(), out.ghf_header.size(),
                    out.ghf_header.data());
    }
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
               int grid_w, int grid_h,
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

    NativeLandscapeLayout layout;
    if (!read_native_layout(dir, hfid, layout, error)) return false;
    if (grid_w != int(layout.width) || grid_h != int(layout.height)) {
        error = "This level's graphics terrain is fixed at " +
                std::to_string(layout.width) + "x" +
                std::to_string(layout.height) +
                " samples by its .ehf file (requested " +
                std::to_string(grid_w) + "x" + std::to_string(grid_h) +
                "). Importing another size would be invisible in game.";
        return false;
    }
    const size_t cells = size_t(layout.width) * size_t(layout.height);
    if (heights.size() != cells) {
        error = "height sample count does not match the native terrain grid";
        return false;
    }

    const std::filesystem::path out_path = dir / (hfid + ".ghf");
    std::vector<uint8_t> compressed;
    std::vector<uint8_t> old_raw;
    if (read_file_bytes(out_path, compressed)) {
        if (compressed.size() >= 2 && compressed[0] == 0x1F &&
            compressed[1] == 0x8B) {
            gzip_decompress(compressed, old_raw);
        } else {
            old_raw = compressed;
        }
    }

    static constexpr uint8_t kFallbackCellTail[10] = {
        0x00, 0x00, 0x00, 0x00, 0xCA, 0xD8, 0x17, 0x57, 0x00, 0x00};
    std::array<uint8_t, 10> fallback_tail{};
    std::copy_n(kFallbackCellTail, fallback_tail.size(),
                fallback_tail.data());
    if (old_raw.size() >= 34) {
        std::copy_n(old_raw.data() + 24, fallback_tail.size(),
                    fallback_tail.data());
    }

    const size_t required = 20 + cells * 14;
    const bool can_preserve_cells =
        old_raw.size() >= required &&
        read_be_u32(old_raw.data() + 12) == layout.width &&
        read_be_u32(old_raw.data() + 16) == layout.height;
    std::vector<uint8_t> raw;
    if (can_preserve_cells) {
        raw.assign(old_raw.begin(), old_raw.begin() + required);
    } else {
        raw.resize(required);
        for (size_t i = 0; i < cells; ++i) {
            std::copy(fallback_tail.begin(), fallback_tail.end(),
                      raw.begin() + 24 + i * 14);
        }
    }
    std::copy(layout.ghf_header.begin(), layout.ghf_header.end(), raw.begin());
    for (size_t i = 0; i < cells; ++i) {
        write_be_f32(raw.data() + 20 + i * 14, heights[i]);
    }

    const std::string embedded = "export_xbox360\\worlds\\albion\\" +
                                 region + "\\" + hfid + ".ghf";
    std::vector<uint8_t> gz;
    const std::string raw_string(reinterpret_cast<const char*>(raw.data()),
                                 raw.size());
    if (!gzip_compress(raw_string, embedded, gz, error)) return false;

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

bool decode_ghf(const std::vector<uint8_t>& file_bytes,
                std::vector<uint8_t>& raw, uint32_t& width,
                uint32_t& height, std::vector<float>& heights,
                const std::string& label, std::string& error) {
    if (file_bytes.size() >= 2 && file_bytes[0] == 0x1F &&
        file_bytes[1] == 0x8B) {
        if (!gzip_decompress(file_bytes, raw)) {
            error = "cannot decompress " + label;
            return false;
        }
    } else {
        raw = file_bytes;
    }
    if (raw.size() < 20) {
        error = label + " is too small";
        return false;
    }
    width = read_be_u32(raw.data() + 12);
    height = read_be_u32(raw.data() + 16);
    const uint64_t cells = uint64_t(width) * uint64_t(height);
    if (width < 2 || height < 2 || cells > (1ull << 28) ||
        20 + cells * 14 > raw.size()) {
        error = label + " dimensions or payload are invalid";
        return false;
    }
    heights.resize(static_cast<size_t>(cells));
    for (size_t i = 0; i < heights.size(); ++i) {
        heights[i] = read_be_f32(raw.data() + 20 + i * 14);
        if (!std::isfinite(heights[i])) heights[i] = 0.0f;
    }
    return true;
}

bool extract_exact_bank_file(const std::filesystem::path& bank,
                             const std::string& entry_name,
                             std::vector<uint8_t>& bytes,
                             std::string& error) {
    try {
        const BnkCache::Entry b = BnkCache::get(bank.string());
        const auto& files = b.reader->list_files();
        const std::string wanted = lower(backslash(entry_name));
        for (size_t i = 0; i < files.size(); ++i) {
            if (lower(backslash(files[i].name)) != wanted) continue;
            bytes = BnkCache::extract_bytes(bank.string(), static_cast<int>(i));
            return !bytes.empty();
        }
        error = "missing " + entry_name + " in " + bank.string();
    } catch (const std::exception& ex) {
        error = bank.filename().string() + ": " + ex.what();
    }
    return false;
}

bool write_file_bytes(const std::filesystem::path& path,
                      const std::vector<uint8_t>& bytes,
                      std::string& error) {
    for (int attempt = 0; attempt < 4; ++attempt) {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (f) {
            f.write(reinterpret_cast<const char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
            if (f) return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
    error = "cannot write " + path.string() + " (locked or still syncing?)";
    return false;
}

bool rewrite_engine_terrain_reference(
        const std::vector<uint8_t>& existing,
        const std::string& virtual_ehf,
        uint64_t key,
        std::vector<uint8_t>& rewritten,
        std::string& error) {
    Level::EngineLevelInfo info;
    if (!Level::ParseEngineLevel(existing, info) || !info.ok ||
        info.entries.size() != info.entry_count) {
        error = "cannot safely rewrite engine_level: " + info.error;
        return false;
    }

    const std::string wanted = lower(backslash(virtual_ehf));
    const Level::EngineLevelEntry* matching = nullptr;
    for (const Level::EngineLevelEntry& item : info.entries) {
        if (item.type != 4 || lower(backslash(item.str_a)) != wanted) continue;
        if (matching) {
            error = "engine_level has duplicate references for " + virtual_ehf;
            return false;
        }
        matching = &item;
    }

    std::string record;
    be_u32(record, 4u);
    record += virtual_ehf;
    record.push_back('\0');
    be_u64(record, key);

    static constexpr size_t kCountOffset = sizeof("LevelGraphicsFile") - 1 + 4;
    static constexpr size_t kHeaderSize = kCountOffset + 4;
    if (existing.size() < kHeaderSize) {
        error = "engine_level header is truncated";
        return false;
    }
    size_t replace_begin = kHeaderSize;
    size_t replace_end = kHeaderSize;
    uint32_t new_count = info.entry_count;
    if (matching) {
        replace_begin = matching->offset;
        replace_end = matching->offset + matching->size;
        if (replace_end > existing.size()) {
            error = "engine_level type-4 record extends beyond the file";
            return false;
        }
    } else {
        if (new_count == std::numeric_limits<uint32_t>::max()) {
            error = "engine_level entry count cannot be incremented";
            return false;
        }
        ++new_count;
    }

    rewritten.clear();
    rewritten.reserve(existing.size() - (replace_end - replace_begin) +
                      record.size());
    rewritten.insert(rewritten.end(), existing.begin(),
                     existing.begin() + static_cast<std::ptrdiff_t>(
                         replace_begin));
    rewritten.insert(rewritten.end(), record.begin(), record.end());
    rewritten.insert(rewritten.end(),
                     existing.begin() + static_cast<std::ptrdiff_t>(
                         replace_end),
                     existing.end());
    rewritten[kCountOffset + 0] = static_cast<uint8_t>(new_count >> 24);
    rewritten[kCountOffset + 1] = static_cast<uint8_t>(new_count >> 16);
    rewritten[kCountOffset + 2] = static_cast<uint8_t>(new_count >> 8);
    rewritten[kCountOffset + 3] = static_cast<uint8_t>(new_count);

    Level::EngineLevelInfo check;
    if (!Level::ParseEngineLevel(rewritten, check) || !check.ok ||
        check.entries.size() != check.entry_count) {
        error = "rewritten engine_level failed validation: " + check.error;
        rewritten.clear();
        return false;
    }
    size_t found = 0;
    for (const Level::EngineLevelEntry& item : check.entries) {
        if (item.type == 4 && item.has_resource_key &&
            lower(backslash(item.str_a)) == wanted &&
            item.resource_key == key) {
            ++found;
        }
    }
    if (found != 1) {
        error = "rewritten engine_level did not retain the terrain key";
        rewritten.clear();
        return false;
    }
    return true;
}

}

bool IsCustomLooseLevel(const FlatAssetEntry& entry, std::string* region) {
    
    
    const std::filesystem::path dir = region_dir(entry, region);
    return !dir.empty() && !find_hfid(dir).empty();
}

bool GetNativeLandscapeLayout(const FlatAssetEntry& entry, int& grid_w,
                              int& grid_h, float& sample_spacing,
                              std::string& error) {
    grid_w = 0;
    grid_h = 0;
    sample_spacing = 0.0f;
    const std::filesystem::path dir = region_dir(entry, nullptr);
    if (dir.empty()) {
        error = "Terrain layout is only available for loose custom levels.";
        return false;
    }
    const std::string hfid = find_hfid(dir);
    if (hfid.empty()) {
        error = "No .ghf heightfield found in " + dir.string();
        return false;
    }
    NativeLandscapeLayout layout;
    if (!read_native_layout(dir, hfid, layout, error)) return false;
    grid_w = static_cast<int>(layout.width);
    grid_h = static_cast<int>(layout.height);
    sample_spacing = layout.sample_spacing;
    return true;
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
    if (!write_ghf(entry, region, p.grid_w, p.grid_h, heights, error)) {
        return false;
    }
    if (!EnsureEhfInStreamingBank(entry, error)) return false;

    OutputLog::success("landscape: flat " + std::to_string(p.grid_w) + "x" +
                       std::to_string(p.grid_h) + " terrain written for '" +
                       region + "'; reloading level");
    reload_level(entry);
    return true;
}

struct BankFilePlan {
    std::string entry_name;
    std::vector<uint8_t> bytes;
};

bool ensure_entries_in_bank(const std::filesystem::path& bank,
                            const std::vector<BankFilePlan>& plans,
                            const char* label,
                            bool* changed,
                            std::string& error) {
    if (changed) *changed = false;
    if (plans.empty()) return true;
    std::vector<BnkWriter::EntryReplacement> repls;
    std::vector<BnkWriter::EntryAddition> adds;
    try {
        {
            const BnkCache::Entry b = BnkCache::get(bank.string());
            const auto& files = b.reader->list_files();
            for (const BankFilePlan& plan : plans) {
                int found = -1;
                for (size_t i = 0; i < files.size(); ++i) {
                    if (lower(backslash(files[i].name)) ==
                        plan.entry_name) {
                        found = (int)i;
                        break;
                    }
                }
                if (found >= 0) {
                    const std::vector<uint8_t> cur =
                        BnkCache::extract_bytes(bank.string(), found);
                    if (cur == plan.bytes) continue;
                    BnkWriter::EntryReplacement r;
                    r.file_index = found;
                    r.payload = plan.bytes;
                    repls.push_back(std::move(r));
                } else {
                    BnkWriter::EntryAddition a;
                    a.name = plan.entry_name;
                    a.payload = plan.bytes;
                    adds.push_back(std::move(a));
                }
            }
        }
        if (repls.empty() && adds.empty()) return true;
        BnkCache::invalidate(bank.string());
    } catch (const std::exception& ex) {
        error = std::string(label) + ": " + ex.what();
        return false;
    }
    bool ok = false;
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (attempt > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            BnkCache::invalidate(bank.string());
        }
        error.clear();
        ok = BnkWriter::RebuildWithChanges(bank.string(), repls, adds,
                                           error);
        if (ok) break;
    }
    if (ok) {
        BnkCache::invalidate(bank.string());
        if (changed) *changed = true;
        OutputLog::success(
            "terrain: " + std::to_string(repls.size()) +
            " updated / " + std::to_string(adds.size()) +
            " added entr" + (repls.size() + adds.size() == 1 ? "y" : "ies") +
            " in " + label);
    }
    return ok;
}

bool ensure_entry_in_bank(const std::filesystem::path& bank,
                          const std::string& entry_name,
                          const std::vector<uint8_t>& bytes,
                          const char* label,
                          bool* changed,
                          std::string& error) {
    std::vector<BankFilePlan> plans(1);
    plans[0].entry_name = entry_name;
    plans[0].bytes = bytes;
    return ensure_entries_in_bank(bank, plans, label, changed, error);
}

void put_be_u32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(uint8_t(x >> 24));
    v.push_back(uint8_t(x >> 16));
    v.push_back(uint8_t(x >> 8));
    v.push_back(uint8_t(x));
}

void put_be_f32(std::vector<uint8_t>& v, float f) {
    uint32_t x;
    std::memcpy(&x, &f, 4);
    put_be_u32(v, x);
}

bool hdb_matches_layout(const std::vector<uint8_t>& hdb,
                        const NativeLandscapeLayout& layout) {
    if (hdb.size() < 32 ||
        std::memcmp(hdb.data(), layout.ghf_header.data(), 12) != 0 ||
        read_be_u32(hdb.data() + 12) != layout.width ||
        read_be_u32(hdb.data() + 16) != layout.height) {
        return false;
    }
    const uint32_t cx = (layout.width - 1) / 8 + 1;
    const uint32_t cy = (layout.height - 1) / 8 + 1;
    return read_be_u32(hdb.data() + 20) == cx &&
           read_be_u32(hdb.data() + 24) == cy &&
           hdb.size() >= 32 + size_t(cx) * size_t(cy) * 4;
}

std::vector<uint8_t> build_hdb_bytes(
    const NativeLandscapeLayout& layout) {
    const uint32_t cx = (layout.width - 1) / 8 + 1;
    const uint32_t cy = (layout.height - 1) / 8 + 1;
    std::vector<uint8_t> out;
    out.reserve(32 + (size_t)cx * cy * 4);
    out.insert(out.end(), layout.ghf_header.begin(),
               layout.ghf_header.begin() + 12);
    put_be_u32(out, layout.width);
    put_be_u32(out, layout.height);
    put_be_u32(out, cx);
    put_be_u32(out, cy);
    put_be_f32(out, 4.0f);



    for (uint32_t i = 0; i < cx * cy; ++i) put_be_u32(out, 0);
    return out;
}

void genv_layout(const NativeLandscapeLayout& layout, uint32_t& span_x,
                 uint32_t& span_y, uint32_t& grid_x, uint32_t& grid_y) {
    span_x = static_cast<uint32_t>(
        std::lround(float(layout.width - 1) * layout.sample_spacing));
    span_y = static_cast<uint32_t>(
        std::lround(float(layout.height - 1) * layout.sample_spacing));
    grid_x = std::max(1u, (span_x + 3) / 4);
    grid_y = std::max(1u, (span_y + 3) / 4);
}

bool genv_matches_layout(const std::vector<uint8_t>& genv,
                         const NativeLandscapeLayout& layout) {
    if (genv.size() < 32 ||
        std::memcmp(genv.data(), layout.ghf_header.data(), 12) != 0) {
        return false;
    }
    uint32_t span_x = 0, span_y = 0, grid_x = 0, grid_y = 0;
    genv_layout(layout, span_x, span_y, grid_x, grid_y);
    return read_be_u32(genv.data() + 12) == span_x &&
           read_be_u32(genv.data() + 16) == span_y &&
           read_be_u32(genv.data() + 20) == grid_x &&
           read_be_u32(genv.data() + 24) == grid_y &&
           genv.size() >= 32 + size_t(grid_x) * size_t(grid_y) * 4;
}

std::vector<uint8_t> build_genv_bytes(
    const NativeLandscapeLayout& layout) {
    uint32_t span_x = 0, span_y = 0, grid_x = 0, grid_y = 0;
    genv_layout(layout, span_x, span_y, grid_x, grid_y);
    const uint32_t egw =
        grid_x;
    const uint32_t egh = grid_y;
    std::vector<uint8_t> out;
    out.reserve(32 + (size_t)egw * egh * 4);
    out.insert(out.end(), layout.ghf_header.begin(),
               layout.ghf_header.begin() + 12);
    put_be_u32(out, span_x);
    put_be_u32(out, span_y);
    put_be_u32(out, egw);
    put_be_u32(out, egh);
    put_be_f32(out, 4.0f);
    while (out.size() < 32) out.push_back(0);
    for (uint32_t i = 0; i < egw * egh; ++i) put_be_u32(out, 0xFFFFFFFFu);
    return out;
}

bool ensure_heightfield_family_in_levels_bnk(const std::string& data_dir,
                                             const std::string& region,
                                             std::string& error) {
    const std::filesystem::path root =
        std::filesystem::path(data_dir) / "worlds" / "albion" /
        lower(region);
    const std::string hfid = find_hfid(root);
    if (hfid.empty()) {
        error = "No .ghf heightfield found in " + root.string();
        return false;
    }
    NativeLandscapeLayout layout;
    if (!read_native_layout(root, hfid, layout, error)) return false;

    std::vector<uint8_t> hdb;
    read_file_bytes(root / (hfid + ".hdb"), hdb);
    if (!hdb_matches_layout(hdb, layout)) {
        hdb = build_hdb_bytes(layout);
        OutputLog::warn(
            "terrain: repaired an HDB whose grid did not match the EHF (" +
            std::to_string(layout.width) + "x" +
            std::to_string(layout.height) + ")");
    }
    std::vector<uint8_t> old_genv;
    read_file_bytes(root / (hfid + ".genv"), old_genv);
    std::vector<uint8_t> genv = old_genv;
    if (!genv_matches_layout(genv, layout)) {
        genv = build_genv_bytes(layout);
        OutputLog::warn(
            "terrain: repaired a GENV whose grid did not match the EHF");
    }

    auto write_loose = [&](const char* ext,
                           const std::vector<uint8_t>& bytes) -> bool {
        const std::filesystem::path p = root / (hfid + ext);
        std::vector<uint8_t> cur;
        if (read_file_bytes(p, cur) && cur == bytes) return true;
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        if (!f) {
            error = "cannot write " + p.string();
            return false;
        }
        f.write(reinterpret_cast<const char*>(bytes.data()),
                (std::streamsize)bytes.size());
        return (bool)f;
    };
    if (!write_loose(".hdb", hdb)) return false;
    if (!write_loose(".genv", genv)) return false;

    const std::string prefix =
        "worlds\\albion\\" + lower(region) + "\\" + hfid;
    std::vector<BankFilePlan> plans;
    plans.push_back({prefix + ".hdb", hdb});
    plans.push_back({prefix + ".genv", genv});
    for (const char* ext : {".ama", ".amm", ".amr"}) {
        std::vector<uint8_t> bytes;
        if (read_file_bytes(root / (hfid + ext), bytes)) {
            plans.push_back({prefix + ext, std::move(bytes)});
        }
    }

    const std::filesystem::path bank =
        std::filesystem::path(data_dir) / "levels.bnk";
    std::error_code ec;
    if (!std::filesystem::is_regular_file(bank, ec)) return true;

    bool in_sync = true;
    try {
        const BnkCache::Entry b = BnkCache::get(bank.string());
        const auto& files = b.reader->list_files();
        for (const BankFilePlan& plan : plans) {
            int found = -1;
            for (size_t i = 0; i < files.size(); ++i) {
                if (lower(backslash(files[i].name)) == plan.entry_name) {
                    found = (int)i;
                    break;
                }
            }
            if (found < 0 ||
                BnkCache::extract_bytes(bank.string(), found) !=
                    plan.bytes) {
                in_sync = false;
                break;
            }
        }
    } catch (const std::exception& ex) {
        error = std::string("levels.bnk: ") + ex.what();
        return false;
    }
    if (in_sync) return true;

    if (!GameBackup::RequireBackup(error)) return false;
    if (!GameBackup::EnsureFilesCovered({bank.string()}, error)) {
        return false;
    }
    OutputLog::info(
        "terrain: registering the heightfield family in levels.bnk "
        "(full bank rebuild - this can take a few minutes, one-time "
        "per grid change)");
    return ensure_entries_in_bank(bank, plans, "levels.bnk", nullptr,
                                  error);
}

bool EnsureEhfInStreamingBank(const std::string& data_dir,
                              const std::string& region,
                              std::string& error) {
    const std::filesystem::path root =
        std::filesystem::path(data_dir) / "worlds" / "albion" /
        lower(region);
    const std::string hfid = find_hfid(root);
    if (hfid.empty()) {
        error = "No .ghf heightfield found in " + root.string();
        return false;
    }
    const std::filesystem::path scenario = root / "defaultscenario";
    const std::filesystem::path ehf = scenario / (hfid + ".ehf");
    const std::filesystem::path level_bank =
        scenario / "defaultscenario_streaming.bnk";
    const std::filesystem::path global_bank =
        std::filesystem::path(data_dir) / "streaming.bnk";
    std::error_code ec;
    if (!std::filesystem::is_regular_file(ehf, ec)) {
        error = "missing " + ehf.string();
        return false;
    }
    std::ifstream f(ehf, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                               std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        error = "empty " + ehf.string();
        return false;
    }
    const std::string entry_name = "worlds\\albion\\" + lower(region) +
                                   "\\defaultscenario\\" + hfid + ".ehf";

    if (std::filesystem::is_regular_file(level_bank, ec)) {
        if (!ensure_entry_in_bank(level_bank, entry_name, bytes,
                                  "the level streaming bank", nullptr,
                                  error)) {
            return false;
        }
    }

    if (!std::filesystem::is_regular_file(global_bank, ec)) return true;
    bool needs_global = true;
    try {
        const BnkCache::Entry b = BnkCache::get(global_bank.string());
        const auto& files = b.reader->list_files();
        for (size_t i = 0; i < files.size(); ++i) {
            if (lower(backslash(files[i].name)) != entry_name) continue;
            const std::vector<uint8_t> cur =
                BnkCache::extract_bytes(global_bank.string(), (int)i);
            if (cur == bytes) needs_global = false;
            break;
        }
    } catch (const std::exception& ex) {
        error = std::string("data\\streaming.bnk: ") + ex.what();
        return false;
    }
    if (needs_global) {
        if (!GameBackup::RequireBackup(error)) return false;
        if (!GameBackup::EnsureFilesCovered({global_bank.string()},
                                            error)) {
            return false;
        }
        OutputLog::info(
            "terrain: injecting " + hfid +
            ".ehf into data\\streaming.bnk (full bank rebuild - this "
            "can take a few minutes, one-time per change)");
        if (!ensure_entry_in_bank(global_bank, entry_name, bytes,
                                  "data\\streaming.bnk", nullptr,
                                  error)) {
            return false;
        }
    }

    return ensure_heightfield_family_in_levels_bnk(data_dir, region,
                                                   error);
}

bool EnsureEhfInStreamingBank(const FlatAssetEntry& entry,
                              std::string& error) {
    std::string region;
    if (region_dir(entry, &region).empty()) {
        error = "Not a loose custom level.";
        return false;
    }
    return EnsureEhfInStreamingBank(entry.bnk_path, region, error);
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
    if (!write_ghf(entry, region, ts.width, ts.height,
                   ts.heights_current, error)) {
        return false;
    }
    TerrainEdit::MarkSaved();
    return true;
}

bool ProbeHeightmapSize(const std::string& image_path, int& out_w,
                        int& out_h) {
    out_w = 0;
    out_h = 0;
    std::vector<float> values;
    std::string ignored;
    if (!load_heightmap(image_path, values, out_w, out_h, ignored)) {
        return false;
    }
    return out_w > 0 && out_h > 0;
}

bool RepairLandscapeForGame(const FlatAssetEntry& entry,
                            std::string& error) {
    if (!GameBackup::RequireBackup(error)) return false;
    std::string region;
    const std::filesystem::path dir = region_dir(entry, &region);
    if (dir.empty()) {
        error = "Terrain repair only works on loose custom levels.";
        return false;
    }
    const std::string hfid = find_hfid(dir);
    NativeLandscapeLayout layout;
    if (hfid.empty() || !read_native_layout(dir, hfid, layout, error)) {
        return false;
    }

    std::vector<uint8_t> file_bytes;
    std::vector<uint8_t> raw;
    const std::filesystem::path ghf_path = dir / (hfid + ".ghf");
    if (!read_file_bytes(ghf_path, file_bytes)) {
        error = "cannot read " + ghf_path.string();
        return false;
    }
    if (file_bytes.size() >= 2 && file_bytes[0] == 0x1F &&
        file_bytes[1] == 0x8B) {
        if (!gzip_decompress(file_bytes, raw)) {
            error = "cannot decompress " + ghf_path.string();
            return false;
        }
    } else {
        raw = std::move(file_bytes);
    }
    if (raw.size() < 20) {
        error = "current .ghf is too small";
        return false;
    }
    const uint32_t src_w = read_be_u32(raw.data() + 12);
    const uint32_t src_h = read_be_u32(raw.data() + 16);
    const uint64_t src_cells = uint64_t(src_w) * uint64_t(src_h);
    if (src_w < 2 || src_h < 2 || src_cells > (1ull << 28) ||
        20 + src_cells * 14 > raw.size()) {
        error = "current .ghf dimensions or payload are invalid";
        return false;
    }

    std::vector<float> source(static_cast<size_t>(src_cells));
    for (size_t i = 0; i < source.size(); ++i) {
        source[i] = read_be_f32(raw.data() + 20 + i * 14);
        if (!std::isfinite(source[i])) source[i] = 0.0f;
    }
    std::vector<float> repaired(size_t(layout.width) * layout.height);
    for (uint32_t y = 0; y < layout.height; ++y) {
        const float v = layout.height > 1
                            ? float(y) / float(layout.height - 1)
                            : 0.0f;
        for (uint32_t x = 0; x < layout.width; ++x) {
            const float u = layout.width > 1
                                ? float(x) / float(layout.width - 1)
                                : 0.0f;
            repaired[size_t(y) * layout.width + x] =
                sample_bilinear(source, int(src_w), int(src_h), u, v);
        }
    }

    if (!write_ghf(entry, region, int(layout.width), int(layout.height),
                   repaired, error)) {
        return false;
    }
    if (!EnsureEhfInStreamingBank(entry, error)) return false;

    OutputLog::success(
        "terrain repair: resampled " + std::to_string(src_w) + "x" +
        std::to_string(src_h) + " GHF to the donor's " +
        std::to_string(layout.width) + "x" +
        std::to_string(layout.height) +
        " EHF grid and synchronized the game banks");
    reload_level(entry);
    return true;
}

bool UpgradeLandscapeToLarge(const FlatAssetEntry& entry,
                             std::string& error) {
    if (!GameBackup::RequireBackup(error)) return false;
    std::string region;
    const std::filesystem::path dir = region_dir(entry, &region);
    if (dir.empty()) {
        error = "Terrain upgrade only works on loose custom levels.";
        return false;
    }
    const std::string hfid = find_hfid(dir);
    if (hfid.empty()) {
        error = "No .ghf heightfield found in " + dir.string();
        return false;
    }

    std::vector<uint8_t> current_file;
    const std::filesystem::path current_ghf = dir / (hfid + ".ghf");
    if (!read_file_bytes(current_ghf, current_file)) {
        error = "cannot read " + current_ghf.string();
        return false;
    }
    std::vector<uint8_t> current_raw;
    std::vector<float> current_heights;
    uint32_t current_w = 0, current_h = 0;
    if (!decode_ghf(current_file, current_raw, current_w, current_h,
                    current_heights, current_ghf.string(), error)) {
        return false;
    }

    static constexpr char kDonorRegion[] = "crucible";
    static constexpr char kDonorHfid[] = "new_heightfield_id_ceee6364";
    const std::filesystem::path levels_bank =
        std::filesystem::path(entry.bnk_path) / "levels.bnk";
    const std::filesystem::path streaming_bank =
        std::filesystem::path(entry.bnk_path) / "streaming.bnk";
    const std::string donor_prefix =
        std::string("worlds\\albion\\") + kDonorRegion + "\\" +
        kDonorHfid;

    std::vector<uint8_t> donor_ghf;
    if (!extract_exact_bank_file(levels_bank, donor_prefix + ".ghf",
                                 donor_ghf, error)) {
        return false;
    }
    std::vector<uint8_t> large_raw;
    std::vector<float> ignored_heights;
    uint32_t large_w = 0, large_h = 0;
    if (!decode_ghf(donor_ghf, large_raw, large_w, large_h,
                    ignored_heights, "Crucible donor GHF", error)) {
        return false;
    }
    if (large_w != 769 || large_h != 769) {
        error = "Crucible donor is not the expected 769x769 terrain.";
        return false;
    }

    struct DonorPart {
        const char* extension;
        std::vector<uint8_t> bytes;
    };
    std::array<DonorPart, 5> parts{{
        {".ama", {}}, {".amm", {}}, {".amr", {}},
        {".hdb", {}}, {".genv", {}}
    }};
    for (DonorPart& part : parts) {
        if (!extract_exact_bank_file(levels_bank,
                                     donor_prefix + part.extension,
                                     part.bytes, error)) {
            return false;
        }
    }
    std::vector<uint8_t> large_ehf;
    const std::string donor_ehf =
        std::string("worlds\\albion\\") + kDonorRegion +
        "\\defaultscenario\\" + kDonorHfid + ".ehf";
    if (!extract_exact_bank_file(streaming_bank, donor_ehf, large_ehf,
                                 error)) {
        return false;
    }
    static constexpr char kEhfMagic[] = "HeightFieldGraphicsFile";
    if (large_ehf.size() < 63 ||
        std::memcmp(large_ehf.data(), kEhfMagic,
                    sizeof(kEhfMagic) - 1) != 0 ||
        read_be_u32(large_ehf.data() + 35) != large_w ||
        read_be_u32(large_ehf.data() + 39) != large_h) {
        error = "Crucible EHF does not match its 769x769 GHF.";
        return false;
    }

    const std::string donor_scenario_prefix =
        std::string("worlds\\albion\\") + kDonorRegion +
        "\\defaultscenario\\";
    std::vector<uint8_t> donor_engine;
    if (!extract_exact_bank_file(
            levels_bank,
            donor_scenario_prefix + "defaultscenario.engine_level",
            donor_engine, error)) {
        return false;
    }
    Level::EngineLevelInfo donor_engine_info;
    if (!Level::ParseEngineLevel(donor_engine, donor_engine_info) ||
        !donor_engine_info.ok ||
        donor_engine_info.entries.size() != donor_engine_info.entry_count) {
        error = "Crucible donor engine_level is invalid: " +
                donor_engine_info.error;
        return false;
    }
    const std::string wanted_donor_ehf = lower(backslash(donor_ehf));
    uint64_t donor_lightmap_key = 0;
    size_t donor_reference_count = 0;
    for (const Level::EngineLevelEntry& item : donor_engine_info.entries) {
        if (item.type != 4 || !item.has_resource_key ||
            lower(backslash(item.str_a)) != wanted_donor_ehf) {
            continue;
        }
        donor_lightmap_key = item.resource_key;
        ++donor_reference_count;
    }
    if (donor_reference_count != 1) {
        error = "Crucible donor does not have exactly one keyed terrain "
                "reference.";
        return false;
    }

    std::vector<uint8_t> donor_lmp;
    if (!extract_exact_bank_file(
            levels_bank,
            donor_scenario_prefix + "defaultscenario.lmp",
            donor_lmp, error)) {
        return false;
    }
    Level::TerrainLightmap donor_lightmap;
    if (!Level::DecodeTerrainLightmap(
            donor_lmp, donor_lightmap_key, donor_lightmap)) {
        error = "Crucible donor lightmap is invalid: " +
                donor_lightmap.error;
        return false;
    }
    if (donor_lightmap.sample_width != large_w ||
        donor_lightmap.sample_height != large_h) {
        error = "Crucible donor lightmap dimensions do not match its EHF.";
        return false;
    }

    const std::filesystem::path scenario_dir = dir / "defaultscenario";
    const std::filesystem::path current_engine =
        scenario_dir / "defaultscenario.engine_level";
    const std::filesystem::path current_lmp =
        scenario_dir / "defaultscenario.lmp";
    std::vector<uint8_t> current_engine_bytes;
    std::vector<uint8_t> current_lmp_bytes;
    if (!read_file_bytes(current_engine, current_engine_bytes) ||
        !read_file_bytes(current_lmp, current_lmp_bytes)) {
        error = "cannot read the custom level's engine_level and LMP";
        return false;
    }
    const std::string current_virtual_ehf =
        "worlds\\albion\\" + region + "\\defaultscenario\\" +
        hfid + ".ehf";
    std::vector<uint8_t> upgraded_engine;
    if (!rewrite_engine_terrain_reference(
            current_engine_bytes, current_virtual_ehf,
            donor_lightmap_key, upgraded_engine, error)) {
        return false;
    }
    std::vector<uint8_t> upgraded_lmp;
    if (!Level::AttachTerrainLightmapRecord(
            current_lmp_bytes, donor_lmp, donor_lightmap_key,
            upgraded_lmp, error)) {
        error = "cannot attach the Crucible terrain lightmap: " + error;
        return false;
    }
    Level::TerrainLightmap merged_lightmap;
    if (!Level::DecodeTerrainLightmap(
            upgraded_lmp, donor_lightmap_key, merged_lightmap) ||
        merged_lightmap.sample_width != large_w ||
        merged_lightmap.sample_height != large_h) {
        error = "upgraded terrain lightmap failed final validation: " +
                merged_lightmap.error;
        return false;
    }

    for (uint32_t y = 0; y < large_h; ++y) {
        const float v = float(y) / float(large_h - 1);
        for (uint32_t x = 0; x < large_w; ++x) {
            const float u = float(x) / float(large_w - 1);
            const float height = sample_bilinear(
                current_heights, static_cast<int>(current_w),
                static_cast<int>(current_h), u, v);
            write_be_f32(large_raw.data() +
                             20 + (size_t(y) * large_w + x) * 14,
                         height);
        }
    }
    const std::string embedded = "export_xbox360\\worlds\\albion\\" +
                                 region + "\\" + hfid + ".ghf";
    const std::string large_raw_string(
        reinterpret_cast<const char*>(large_raw.data()), large_raw.size());
    std::vector<uint8_t> upgraded_ghf;
    if (!gzip_compress(large_raw_string, embedded, upgraded_ghf, error)) {
        return false;
    }

    std::vector<std::string> backup_paths;
    backup_paths.push_back(current_ghf.string());
    const std::filesystem::path current_ehf =
        dir / "defaultscenario" / (hfid + ".ehf");
    backup_paths.push_back(current_ehf.string());
    backup_paths.push_back(current_engine.string());
    backup_paths.push_back(current_lmp.string());
    for (const DonorPart& part : parts) {
        backup_paths.push_back((dir / (hfid + part.extension)).string());
    }
    if (!GameBackup::EnsureFilesCovered(backup_paths, error)) return false;

    if (!write_file_bytes(current_ghf, upgraded_ghf, error) ||
        !write_file_bytes(current_ehf, large_ehf, error) ||
        !write_file_bytes(current_engine, upgraded_engine, error) ||
        !write_file_bytes(current_lmp, upgraded_lmp, error)) {
        return false;
    }
    for (const DonorPart& part : parts) {
        if (!write_file_bytes(dir / (hfid + part.extension), part.bytes,
                              error)) {
            return false;
        }
    }
    BnkCache::invalidate(entry.bnk_path);
    if (!EnsureEhfInStreamingBank(entry, error)) return false;

    OutputLog::success(
        "terrain upgrade: resampled " + std::to_string(current_w) + "x" +
        std::to_string(current_h) + " terrain to the native 769x769 "
        "Crucible graphics family; the keyed terrain lightmap was attached "
        "without replacing the level's other lighting records, and scenario "
        "entities were preserved");
    reload_level(entry);
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
    if (!write_ghf(entry, region, p.grid_w, p.grid_h, heights, error)) {
        return false;
    }
    if (!EnsureEhfInStreamingBank(entry, error)) return false;

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
