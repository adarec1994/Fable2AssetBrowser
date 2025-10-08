#include "TexParser.h"
#include "Files.h"
#include "Utils.h"
#include "BNKCore.cpp"
#include <filesystem>
#include <fstream>
#include <optional>
#include <unordered_map>
#include <algorithm>

bool parse_tex_info(const std::vector<unsigned char> &d, TexInfo &out) {
    out = TexInfo{};
    size_t off = 0;
    if (!rd32be(d, off, out.Sign)) return false;
    off += 4;
    if (!rd32be(d, off, out.RawDataSize)) return false;
    off += 4;
    if (!rd32be(d, off, out.Unknown_0)) return false;
    off += 4;
    if (!rd32be(d, off, out.Unknown_1)) return false;
    off += 4;
    if (!rd32be(d, off, out.TextureWidth)) return false;
    off += 4;
    if (!rd32be(d, off, out.TextureHeight)) return false;
    off += 4;
    if (!rd32be(d, off, out.PixelFormat)) return false;
    off += 4;
    uint32_t mipCountField = 0;
    if (!rd32be(d, off, mipCountField)) return false;
    off += 4;
    out.MipMap = mipCountField;
    out.MipMapOffset.clear();
    out.Mips.clear();
    const uint32_t kMaxReasonableMips = 4096;
    size_t bytes_left_for_offsets = (off <= d.size()) ? (d.size() - off) : 0;
    uint32_t max_entries_that_fit = (uint32_t) std::min<size_t>(bytes_left_for_offsets / 4, kMaxReasonableMips);
    uint32_t to_read;
    if (mipCountField == 0 || mipCountField > kMaxReasonableMips || mipCountField > max_entries_that_fit) {
        to_read = max_entries_that_fit;
    } else {
        to_read = mipCountField;
    }
    out.MipMapOffset.reserve(to_read);
    for (uint32_t i = 0; i < to_read; ++i) {
        uint32_t v = 0;
        if (!rd32be(d, off, v)) break;
        out.MipMapOffset.push_back(v);
        off += 4;
    }
    if (out.MipMapOffset.empty()) {
        return true;
    }
    for (uint32_t i = 0; i < out.MipMapOffset.size(); ++i) {
        size_t mo = (size_t) out.MipMapOffset[i];
        if (mo >= d.size()) {
            continue;
        }
        if (mo + 12 * 4 > d.size()) {
            continue;
        }
        TexInfo::MipDef md{};
        md.DefOffset = mo;
        size_t k = mo;
        auto safe_rd32 = [&](uint32_t &outv)-> bool {
            if (!rd32be(d, k, outv)) return false;
            k += 4;
            return true;
        };
        bool ok =
                safe_rd32(md.CompFlag) &&
                safe_rd32(md.DataOffset) &&
                safe_rd32(md.DataSize) &&
                safe_rd32(md.Unknown_3) &&
                safe_rd32(md.Unknown_4) &&
                safe_rd32(md.Unknown_5) &&
                safe_rd32(md.Unknown_6) &&
                safe_rd32(md.Unknown_7) &&
                safe_rd32(md.Unknown_8) &&
                safe_rd32(md.Unknown_9) &&
                safe_rd32(md.Unknown_10) &&
                safe_rd32(md.Unknown_11);
        if (!ok) {
            continue;
        }
        md.HasWH = false;
        md.MipWidth = 0;
        md.MipHeight = 0;
        md.MipDataOffset = 0;
        md.MipDataSizeParsed = 0;
        if (md.CompFlag == 7) {
            size_t start = k;
            size_t max_sz = (start < d.size()) ? (d.size() - start) : 0;
            size_t want = (size_t) md.DataSize;
            size_t use = std::min(want, max_sz);

            if (use == 0) {
                use = max_sz;
            }
            if (use > 0) {
                md.MipDataOffset = start;
                md.MipDataSizeParsed = use;
                out.Mips.push_back(md);
            }
        } else {
            if (k + 4 > d.size()) {
                continue;
            }
            uint16_t w16 = 0, h16 = 0;
            size_t whp = k;
            if (!rd16be(d, k, w16) || !rd16be(d, k, h16)) {
                continue;
            }
            md.HasWH = true;
            md.MipWidth = w16;
            md.MipHeight = h16;
            const size_t extra_hdr_bytes = 440;
            if (k + extra_hdr_bytes > d.size()) {
                continue;
            }
            size_t start = k + extra_hdr_bytes;
            size_t header_bytes_total = 4 /*W/H*/ + extra_hdr_bytes;
            size_t data_declared = (md.DataSize >= header_bytes_total) ? (md.DataSize - header_bytes_total) : 0;
            size_t max_sz = (start < d.size()) ? (d.size() - start) : 0;
            size_t use = std::min(data_declared, max_sz);
            if (use == 0) {
                use = max_sz;
            }

            if (use > 0) {
                md.MipDataOffset = start;
                md.MipDataSizeParsed = use;
                out.Mips.push_back(md);
            }
        }
    }
    return true;
}

bool build_tex_buffer_for_name(const std::string &tex_name, std::vector<unsigned char> &out) {
    auto p_headers = find_bnk_by_filename("globals_texture_headers.bnk");
    auto p_rest = find_bnk_by_filename("globals_textures.bnk");
    if (!p_headers || !p_rest) return false;

    BNKReader r_headers(*p_headers);
    BNKReader r_rest(*p_rest);

    auto p_mip0 = find_bnk_by_filename("1024mip0_textures.bnk");
    std::optional<BNKReader> r_mip0;
    if (p_mip0) r_mip0.emplace(*p_mip0);

    std::unordered_map<std::string, int> mapH, mapR, mapM;
    for (size_t i = 0; i < r_headers.list_files().size(); ++i) {
        auto &e = r_headers.list_files()[i];
        std::string fname = std::filesystem::path(e.name).filename().string();
        std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
        mapH.emplace(fname, (int) i);
    }
    for (size_t i = 0; i < r_rest.list_files().size(); ++i) {
        auto &e = r_rest.list_files()[i];
        std::string fname = std::filesystem::path(e.name).filename().string();
        std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
        mapR.emplace(fname, (int) i);
    }
    if (r_mip0) {
        for (size_t i = 0; i < r_mip0->list_files().size(); ++i) {
            auto &e = r_mip0->list_files()[i];
            std::string fname = std::filesystem::path(e.name).filename().string();
            std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
            mapM.emplace(fname, (int) i);
        }
    }

    std::string key = std::filesystem::path(tex_name).filename().string();
    std::transform(key.begin(), key.end(), key.begin(), ::tolower);

    if (!mapH.count(key)) return false;

    auto tmpdir = std::filesystem::temp_directory_path() / "f2_tex_hex";
    std::error_code ec;
    std::filesystem::create_directories(tmpdir, ec);

    auto tmp_h = tmpdir / ("h_" + std::to_string(std::hash<std::string>{}(tex_name)) + ".bin");
    auto tmp_m = tmpdir / ("m_" + std::to_string(std::hash<std::string>{}(tex_name)) + ".bin");
    auto tmp_r = tmpdir / ("r_" + std::to_string(std::hash<std::string>{}(tex_name)) + ".bin");

    try {
        extract_one(*p_headers, mapH.at(key), tmp_h.string());

        bool has_mip0 = false;
        if (mapM.count(key) && p_mip0) {
            extract_one(*p_mip0, mapM.at(key), tmp_m.string());
            has_mip0 = std::filesystem::exists(tmp_m);
        }

        bool has_rest = false;
        if (mapR.count(key)) {
            extract_one(*p_rest, mapR.at(key), tmp_r.string());
            has_rest = std::filesystem::exists(tmp_r);
        }

        auto vh = read_all_bytes(tmp_h);
        if (vh.empty()) {
            std::filesystem::remove(tmp_h, ec);
            return false;
        }

        std::vector<unsigned char> vm;
        if (has_mip0) {
            vm = read_all_bytes(tmp_m);
        }

        std::vector<unsigned char> vr;
        if (has_rest) {
            vr = read_all_bytes(tmp_r);
        }

        out.clear();
        out.reserve(vh.size() + vm.size() + vr.size());
        out.insert(out.end(), vh.begin(), vh.end());
        out.insert(out.end(), vm.begin(), vm.end());
        out.insert(out.end(), vr.begin(), vr.end());

        std::filesystem::remove(tmp_h, ec);
        if (has_mip0) std::filesystem::remove(tmp_m, ec);
        if (has_rest) std::filesystem::remove(tmp_r, ec);

        return !out.empty();
    } catch (...) {
        std::filesystem::remove(tmp_h, ec);
        std::filesystem::remove(tmp_m, ec);
        std::filesystem::remove(tmp_r, ec);
        return false;
    }
}