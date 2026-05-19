#include "TexParser.h"
#include "Utilities/Files.h"
#include "Utilities/Utils.h"
#include "BNKCore.cpp"
#include "Level/LevelLoader.h"
#include <filesystem>
#include <fstream>
#include <optional>
#include <unordered_map>
#include <algorithm>

static bool parse_bare_chain(const std::vector<unsigned char>& d, TexInfo& out) {
    size_t off = 0;
    while (off + 48 <= d.size()) {
        TexInfo::MipDef md{};
        md.DefOffset = off;
        size_t k = off;
        auto rd = [&](uint32_t& v) {
            if (!rd32be(d, k, v)) return false;
            k += 4; return true;
        };
        if (!(rd(md.CompFlag) && rd(md.DataOffset) && rd(md.DataSize) &&
              rd(md.Unknown_3) && rd(md.Unknown_4) && rd(md.Unknown_5) &&
              rd(md.Unknown_6) && rd(md.Unknown_7) && rd(md.Unknown_8) &&
              rd(md.Unknown_9) && rd(md.Unknown_10) && rd(md.Unknown_11))) break;

        if (md.DataOffset != 48 || md.CompFlag > 24) break;
        if (off + 48 + md.DataSize > d.size() + 32) break;

        if (md.CompFlag == 7) {
            md.HasWH = false;
            md.MipDataOffset = off + 48;
            md.MipDataSizeParsed = std::min<size_t>(md.DataSize, d.size() - (off + 48));
        } else {

            if (off + 48 + 4 > d.size()) break;
            uint16_t w16 = 0, h16 = 0;
            size_t whp = off + 48;
            if (!rd16be(d, whp, w16) || !rd16be(d, whp, h16)) break;
            md.HasWH = true;
            md.MipWidth = w16;
            md.MipHeight = h16;
            const size_t header_bytes = 4 + 440;
            if (md.DataSize < header_bytes) break;
            md.MipDataOffset = off + 48 + header_bytes;
            md.MipDataSizeParsed = md.DataSize - header_bytes;
        }

        size_t advance = 48 + md.DataSize;
        bool dual = false;
        if (md.Unknown_3 <= 24 &&
            md.Unknown_4 == 48 + md.DataSize &&
            md.Unknown_5 > 0 &&
            (off + (size_t)md.Unknown_4 + md.Unknown_5) <= d.size() + 32) {
            advance = (size_t)md.Unknown_4 + md.Unknown_5;
            dual = true;
        }

        out.Mips.push_back(md);

        if (out.TextureWidth == 0 && md.HasWH) {
            out.TextureWidth = md.MipWidth;
            out.TextureHeight = md.MipHeight;
        }

        if (dual && out.PixelFormat == 0) {
            const uint32_t cf = md.CompFlag;
            if (cf == 1 || cf == 11) {
                out.PixelFormat = 39;
            } else if (cf == 2 || cf == 3 || cf == 4) {
                out.PixelFormat = 40;
            } else {

                out.PixelFormat = 40;
            }
        }
        off += advance;
    }

    if (out.PixelFormat == 0) out.PixelFormat = 35;
    return !out.Mips.empty();
}

bool parse_tex_info(const std::vector<unsigned char> &d, TexInfo &out) {
    out = TexInfo{};
    if (d.size() < 32) return false;

    {
        uint32_t a = 0, b = 0;
        size_t p = 0;
        rd32be(d, p, a); p = 4;
        rd32be(d, p, b);
        if (a <= 24 && b == 48) {
            return parse_bare_chain(d, out);
        }
    }
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

    {
        bool single_offset = (!out.MipMapOffset.empty()) && (out.MipMapOffset[0] != 0);
        for (size_t j = 1; j < out.MipMapOffset.size() && single_offset; ++j) {
            if (out.MipMapOffset[j] != 0) single_offset = false;
        }
        if (single_offset && out.TextureWidth > 0 && out.TextureHeight > 0) {
            size_t mo = (size_t) out.MipMapOffset[0];
            uint32_t size_prefix = 0;
            size_t tmp_off = mo;
            const bool prefix_ok = (mo + 4 <= d.size()) &&
                                   rd32be(d, tmp_off, size_prefix) &&
                                   (size_t)mo + 4 + size_prefix <= d.size();

            const size_t W = (size_t)out.TextureWidth;
            const size_t H = (size_t)out.TextureHeight;
            const size_t sz_argb8  = W * H * 4;
            const size_t sz_bc1    = W * H / 2;
            const size_t sz_bc3    = W * H * 1;

            const size_t pW_argb = (W + 31) & ~size_t(31);
            const size_t pH_argb = (H + 31) & ~size_t(31);
            const size_t sz_argb8_padded = pW_argb * pH_argb * 4;

            const size_t Wb = (W + 3) / 4;
            const size_t Hb = (H + 3) / 4;
            const size_t pWb = (Wb + 31) & ~size_t(31);
            const size_t pHb = (Hb + 31) & ~size_t(31);
            const size_t sz_bc1_padded = pWb * pHb * 8;
            const size_t sz_bc3_padded = pWb * pHb * 16;

            bool matched = false;
            uint32_t comp_tag    = 0;
            size_t   body_size   = 0;

            if (prefix_ok && (size_t)size_prefix == sz_argb8) {
                comp_tag  = (out.PixelFormat == 2) ? 99u : 100u;
                body_size = sz_argb8;
                matched   = true;
            } else if (prefix_ok && (size_t)size_prefix == sz_argb8_padded &&
                       sz_argb8_padded != sz_argb8 &&
                       (out.PixelFormat == 2 || out.PixelFormat == 4)) {
                comp_tag  = (out.PixelFormat == 2) ? 99u : 100u;
                body_size = sz_argb8_padded;
                matched   = true;
            } else if (prefix_ok && (size_t)size_prefix == sz_bc1 &&
                       (out.PixelFormat == 35)) {
                comp_tag  = 7u;
                body_size = sz_bc1;
                matched   = true;
            } else if (prefix_ok && (size_t)size_prefix == sz_bc1_padded &&
                       sz_bc1_padded != sz_bc1 &&
                       (out.PixelFormat == 35)) {
                comp_tag  = 7u;
                body_size = sz_bc1_padded;
                matched   = true;
            } else if (prefix_ok && (size_t)size_prefix == sz_bc3 &&
                       (out.PixelFormat == 39)) {
                comp_tag  = 7u;
                body_size = sz_bc3;
                matched   = true;
            } else if (prefix_ok && (size_t)size_prefix == sz_bc3_padded &&
                       sz_bc3_padded != sz_bc3 &&
                       (out.PixelFormat == 39)) {
                comp_tag  = 7u;
                body_size = sz_bc3_padded;
                matched   = true;
            } else if (prefix_ok && (size_t)size_prefix == sz_bc3 &&
                       (out.PixelFormat == 40)) {
                comp_tag  = 7u;
                body_size = sz_bc3;
                matched   = true;
            } else if (prefix_ok && (size_t)size_prefix == sz_bc3_padded &&
                       sz_bc3_padded != sz_bc3 &&
                       (out.PixelFormat == 40)) {
                comp_tag  = 7u;
                body_size = sz_bc3_padded;
                matched   = true;
            }

            if (matched) {
                TexInfo::MipDef md{};
                md.DefOffset         = mo;
                md.CompFlag          = comp_tag;
                md.DataOffset        = 4;
                md.DataSize          = (uint32_t)(body_size + 4);
                md.HasWH             = true;
                md.MipWidth          = (uint16_t)out.TextureWidth;
                md.MipHeight         = (uint16_t)out.TextureHeight;
                md.MipDataOffset     = mo + 4;
                md.MipDataSizeParsed = body_size;
                out.Mips.push_back(md);
                return true;
            }

            if (prefix_ok && mo + 10 <= d.size()) {
                const uint8_t* z = d.data() + mo + 8;
                const bool zlib_magic =
                    z[0] == 0x78 && (z[1] == 0xDA || z[1] == 0x9C ||
                                     z[1] == 0x01 || z[1] == 0x5E);
                size_t expected_raw = 0;
                uint32_t z_tag      = 0;
                if (zlib_magic && (size_t)size_prefix == sz_bc1 &&
                    out.PixelFormat == 35) {
                    expected_raw = sz_bc1;
                    z_tag        = 200u;
                } else if (zlib_magic && (size_t)size_prefix == sz_bc3 &&
                           out.PixelFormat == 39) {
                    expected_raw = sz_bc3;
                    z_tag        = 201u;
                } else if (zlib_magic && (size_t)size_prefix == sz_bc3 &&
                           out.PixelFormat == 40) {
                    expected_raw = sz_bc3;
                    z_tag        = 202u;
                } else if (zlib_magic && (size_t)size_prefix == sz_argb8 &&
                           (out.PixelFormat == 2 || out.PixelFormat == 4)) {
                    expected_raw = sz_argb8;
                    z_tag        = 203u;
                }
                if (z_tag) {
                    TexInfo::MipDef md{};
                    md.DefOffset         = mo;
                    md.CompFlag          = z_tag;
                    md.DataOffset        = 8;
                    md.HasWH             = true;
                    md.MipWidth          = (uint16_t)out.TextureWidth;
                    md.MipHeight         = (uint16_t)out.TextureHeight;
                    md.MipDataOffset     = mo + 8;
                    md.MipDataSizeParsed = d.size() - (mo + 8);
                    md.DataSize          = (uint32_t)md.MipDataSizeParsed;
                    md.Unknown_3         = (uint32_t)expected_raw;
                    out.Mips.push_back(md);
                    return true;
                }
            }
        }
    }

    auto parse_one_mip = [&](size_t mo, TexInfo::MipDef& md, size_t& body_end) -> bool {
        if (mo + 48 > d.size()) return false;
        size_t k = mo;
        md = TexInfo::MipDef{};
        md.DefOffset = mo;
        auto rd = [&](uint32_t& v) {
            if (!rd32be(d, k, v)) return false;
            k += 4; return true;
        };
        if (!(rd(md.CompFlag) && rd(md.DataOffset) && rd(md.DataSize) &&
              rd(md.Unknown_3) && rd(md.Unknown_4) && rd(md.Unknown_5) &&
              rd(md.Unknown_6) && rd(md.Unknown_7) && rd(md.Unknown_8) &&
              rd(md.Unknown_9) && rd(md.Unknown_10) && rd(md.Unknown_11))) return false;

        if (md.CompFlag > 24u || md.DataOffset != 48u) return false;

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
            if (use == 0) use = max_sz;
            if (use == 0) return false;
            md.MipDataOffset = start;
            md.MipDataSizeParsed = use;
        } else {
            if (k + 4 > d.size()) return false;
            uint16_t w16 = 0, h16 = 0;
            if (!rd16be(d, k, w16) || !rd16be(d, k, h16)) return false;
            md.HasWH = true;
            md.MipWidth = w16;
            md.MipHeight = h16;
            const size_t extra_hdr_bytes = 440;
            if (k + extra_hdr_bytes > d.size()) return false;
            size_t start = k + extra_hdr_bytes;
            size_t header_bytes_total = 4 + extra_hdr_bytes;
            size_t data_declared = (md.DataSize >= header_bytes_total)
                                       ? (md.DataSize - header_bytes_total) : 0;
            size_t max_sz = (start < d.size()) ? (d.size() - start) : 0;
            size_t use = std::min(data_declared, max_sz);
            if (use == 0) use = max_sz;
            if (use == 0) return false;
            md.MipDataOffset = start;
            md.MipDataSizeParsed = use;
        }
        body_end = mo + 48 + (size_t)md.DataSize;
        return true;
    };

    if (out.MipMapOffset.empty()) return true;

    size_t walk_off = (size_t) out.MipMapOffset[0];
    int safety = 64;
    while (safety-- > 0 && walk_off + 48 <= d.size()) {
        TexInfo::MipDef md{};
        size_t body_end = 0;
        if (!parse_one_mip(walk_off, md, body_end)) break;
        out.Mips.push_back(md);
        if (body_end <= walk_off || body_end > d.size()) break;
        walk_off = body_end;
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

bool build_gui_tex_buffer_for_name(const std::string &tex_name, std::vector<unsigned char> &out) {
    std::vector<std::string> all_headers, all_bodies;

    std::vector<std::string> all_paths;
    all_paths.reserve(S.bnk_paths.size() + S.nested_bnk_paths.size());
    all_paths.insert(all_paths.end(), S.bnk_paths.begin(), S.bnk_paths.end());
    all_paths.insert(all_paths.end(), S.nested_bnk_paths.begin(), S.nested_bnk_paths.end());
    for (const auto& path : all_paths) {
        std::string fname = std::filesystem::path(path).filename().string();
        std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
        if (fname == "gui_texture_headers.bnk") {
            all_headers.push_back(path);
        } else if (fname == "gui_textures.bnk") {
            all_bodies.push_back(path);
        }
    }

    if (all_headers.empty() || all_bodies.empty()) {
        return false;
    }

    std::string key = tex_name;
    std::transform(key.begin(), key.end(), key.begin(), ::tolower);

    int header_idx = -1;
    std::string found_header_bnk;
    for (const auto& header_path : all_headers) {
        BNKReader r_headers(header_path);
        for (size_t i = 0; i < r_headers.list_files().size(); ++i) {
            auto &e = r_headers.list_files()[i];
            std::string fname_lower = e.name;
            std::transform(fname_lower.begin(), fname_lower.end(), fname_lower.begin(), ::tolower);
            if (fname_lower == key) {
                header_idx = (int)i;
                found_header_bnk = header_path;
                break;
            }
        }
        if (header_idx != -1) break;
    }

    int body_idx = -1;
    std::string found_body_bnk;
    for (const auto& body_path : all_bodies) {
        BNKReader r_rest(body_path);
        for (size_t i = 0; i < r_rest.list_files().size(); ++i) {
            auto &e = r_rest.list_files()[i];
            std::string fname_lower = e.name;
            std::transform(fname_lower.begin(), fname_lower.end(), fname_lower.begin(), ::tolower);
            if (fname_lower == key) {
                body_idx = (int)i;
                found_body_bnk = body_path;
                break;
            }
        }
        if (body_idx != -1) break;
    }

    if (header_idx == -1 || body_idx == -1) {
        return false;
    }

    auto tmpdir = std::filesystem::temp_directory_path() / "f2_tex_hex";
    std::error_code ec;
    std::filesystem::create_directories(tmpdir, ec);

    auto tmp_h = tmpdir / ("gui_h_" + std::to_string(std::hash<std::string>{}(tex_name)) + ".bin");
    auto tmp_r = tmpdir / ("gui_r_" + std::to_string(std::hash<std::string>{}(tex_name)) + ".bin");

    try {
        extract_one(found_header_bnk, header_idx, tmp_h.string());
        extract_one(found_body_bnk, body_idx, tmp_r.string());

        auto vh = read_all_bytes(tmp_h);
        auto vr = read_all_bytes(tmp_r);

        if (vh.empty() || vr.empty()) {
            std::filesystem::remove(tmp_h, ec);
            std::filesystem::remove(tmp_r, ec);
            return false;
        }

        out.clear();
        out.reserve(vh.size() + vr.size());
        out.insert(out.end(), vh.begin(), vh.end());
        out.insert(out.end(), vr.begin(), vr.end());

        std::filesystem::remove(tmp_h, ec);
        std::filesystem::remove(tmp_r, ec);

        return true;
    } catch (...) {
        std::filesystem::remove(tmp_h, ec);
        std::filesystem::remove(tmp_r, ec);
        return false;
    }
}

bool build_any_tex_buffer_for_name(const std::string &tex_name, std::vector<unsigned char> &out,
                                   const std::string &preferred_bnk) {
    auto normalize_lookup_key = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        std::replace(s.begin(), s.end(), '\\', '/');
        return s;
    };

    auto basename_key = [](const std::string& s) {
        size_t pos = s.find_last_of("/\\");
        return pos == std::string::npos ? s : s.substr(pos + 1);
    };

    std::string full_key = normalize_lookup_key(tex_name);
    std::string base_key = normalize_lookup_key(basename_key(tex_name));

    std::vector<std::string> search_paths;
    search_paths.reserve(S.bnk_paths.size() + S.nested_bnk_paths.size());
    search_paths.insert(search_paths.end(), S.bnk_paths.begin(), S.bnk_paths.end());
    search_paths.insert(search_paths.end(), S.nested_bnk_paths.begin(), S.nested_bnk_paths.end());

    auto classify = [](const std::string& bnk_path) -> int {
        std::string n = std::filesystem::path(bnk_path).filename().string();
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        const bool is_header  = n.find("header")   != std::string::npos
                             && n.find("texture")  != std::string::npos;
        const bool is_mip0    = n.find("1024mip0") != std::string::npos
                             && n.find("texture")  != std::string::npos;
        const bool is_texbody = n.find("texture")  != std::string::npos
                             && !is_header && !is_mip0;
        if (is_header)  return 1;
        if (is_mip0)    return 2;
        if (is_texbody) return 3;
        if (n.find(".manifest") != std::string::npos) return 0;
        if (n.size() < 4 || n.compare(n.size() - 4, 4, ".bnk") != 0) return 0;
        return 4;
    };

    auto family_key = [](const std::string& bnk_path) {
        std::string n = std::filesystem::path(bnk_path).filename().string();
        std::transform(n.begin(), n.end(), n.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        if (n.size() >= 4 && n.compare(n.size() - 4, 4, ".bnk") == 0) {
            n.resize(n.size() - 4);
        }
        const std::string suffixes[] = {
            "_texture_headers",
            "_textures",
            "_texture",
            "_headers"
        };
        for (const auto& suffix : suffixes) {
            if (n.size() > suffix.size() &&
                n.compare(n.size() - suffix.size(), suffix.size(), suffix) == 0) {
                n.resize(n.size() - suffix.size());
                break;
            }
        }
        return n;
    };

    if (!preferred_bnk.empty()) {
        std::string preferred_parent;
        auto it_pp = S.nested_bnk_parents.find(preferred_bnk);
        if (it_pp != S.nested_bnk_parents.end()) {
            preferred_parent = it_pp->second;
        }
        const bool preferred_is_nested = !preferred_parent.empty();
        const std::string preferred_family = family_key(preferred_bnk);

        auto is_sibling = [&](const std::string& p) {
            if (p == preferred_bnk) return true;
            if (preferred_parent.empty()) return false;
            auto it = S.nested_bnk_parents.find(p);
            return it != S.nested_bnk_parents.end() && it->second == preferred_parent;
        };

        auto is_nested_path = [&](const std::string& p) {
            return S.nested_bnk_parents.find(p) != S.nested_bnk_parents.end();
        };

        auto rank = [&](const std::string& p) {
            if (preferred_is_nested) {
                if (is_sibling(p)) return 0;
                return is_nested_path(p) ? 2 : 1;
            }
            if (p == preferred_bnk || family_key(p) == preferred_family) return 0;
            if (classify(p) == 2) return 1;
            return is_nested_path(p) ? 3 : 2;
        };

        std::stable_sort(search_paths.begin(), search_paths.end(),
                         [&](const std::string& a, const std::string& b) {
                             return rank(a) < rank(b);
                         });
    }

    auto find_tex_index = [&](const std::string& bnk_path) {
        int idx = BnkCache::find_index(bnk_path, full_key);
        if (idx < 0 && base_key != full_key) {
            idx = BnkCache::find_index(bnk_path, base_key);
        }
        return idx;
    };

    std::string header_bnk_path, mip0_bnk_path, body_bnk_path;
    int header_idx = -1, mip0_idx = -1, body_idx = -1;

    for (const auto& bnk_path : search_paths) {
        int role = classify(bnk_path);
        if (role == 1 && header_idx < 0) {
            int idx = find_tex_index(bnk_path);
            if (idx >= 0) { header_idx = idx; header_bnk_path = bnk_path; }
        } else if (role == 2 && mip0_idx < 0) {
            int idx = find_tex_index(bnk_path);
            if (idx >= 0) { mip0_idx = idx; mip0_bnk_path = bnk_path; }
        } else if (role == 3 && body_idx < 0) {
            int idx = find_tex_index(bnk_path);
            if (idx >= 0) { body_idx = idx; body_bnk_path = bnk_path; }
        }
    }

    if (header_idx < 0) return false;

    if (body_idx < 0 && !g_level_vfs_texture_body_bnks.empty()) {
        std::vector<std::string> resolved_vfs_paths;
        resolved_vfs_paths.reserve(g_level_vfs_texture_body_bnks.size());
        for (const auto& p : g_level_vfs_texture_body_bnks) {
            if (auto resolved = find_bnk_by_virtual_path(p)) {
                if (std::find(resolved_vfs_paths.begin(),
                              resolved_vfs_paths.end(),
                              *resolved) == resolved_vfs_paths.end()) {
                    resolved_vfs_paths.push_back(*resolved);
                }
            }
        }
        for (const auto& bnk_path : resolved_vfs_paths) {
            int idx = find_tex_index(bnk_path);
            if (idx >= 0) { body_idx = idx; body_bnk_path = bnk_path; break; }
        }
    }

    if (body_idx < 0 && !g_level_vfs_texture_body_bnks.empty()) {
        auto leaf_lower = [](const std::string& p) {
            std::string s = std::filesystem::path(p).filename().string();
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            return s;
        };
        std::vector<std::string> wanted_leaves;
        wanted_leaves.reserve(g_level_vfs_texture_body_bnks.size());
        for (const auto& p : g_level_vfs_texture_body_bnks) {
            wanted_leaves.push_back(leaf_lower(p));
        }
        for (const auto& bnk_path : search_paths) {
            const std::string leaf = leaf_lower(bnk_path);
            bool match = false;
            for (const auto& w : wanted_leaves) {
                if (w == leaf) { match = true; break; }
            }
            if (!match) continue;
            int idx = find_tex_index(bnk_path);
            if (idx >= 0) { body_idx = idx; body_bnk_path = bnk_path; break; }
        }
    }

    if (body_idx < 0) {
        for (const auto& bnk_path : search_paths) {
            if (classify(bnk_path) != 4) continue;
            int idx = find_tex_index(bnk_path);
            if (idx >= 0) { body_idx = idx; body_bnk_path = bnk_path; break; }
        }
    }
    if (body_idx < 0) {
        for (const auto& bnk_path : search_paths) {
            if (bnk_path == header_bnk_path) continue;
            if (mip0_idx >= 0 && bnk_path == mip0_bnk_path) continue;
            int role = classify(bnk_path);
            if (role == 0) continue;
            int idx = find_tex_index(bnk_path);
            if (idx >= 0) { body_idx = idx; body_bnk_path = bnk_path; break; }
        }
    }
    if (body_idx < 0) {
        for (const auto& e : S.all_tex_files) {
            if (e.bnk_path == header_bnk_path) continue;
            if (mip0_idx >= 0 && e.bnk_path == mip0_bnk_path) continue;
            std::string e_full = normalize_lookup_key(e.full_path);
            std::string e_base = normalize_lookup_key(e.name);
            if (e_full != full_key && e_base != base_key) continue;
            if (classify(e.bnk_path) == 1) continue;
            body_idx = e.file_index;
            body_bnk_path = e.bnk_path;
            break;
        }
    }

    try {
        auto vh = BnkCache::extract_bytes(header_bnk_path, header_idx);
        if (vh.empty()) return false;

        std::vector<uint8_t> vm, vb;
        if (mip0_idx >= 0) vm = BnkCache::extract_bytes(mip0_bnk_path, mip0_idx);
        if (body_idx >= 0) vb = BnkCache::extract_bytes(body_bnk_path, body_idx);

        if (vm.empty() && vb.empty()) return false;

        out.clear();
        out.reserve(vh.size() + vm.size() + vb.size());
        out.insert(out.end(), vh.begin(), vh.end());
        if (!vm.empty()) out.insert(out.end(), vm.begin(), vm.end());
        if (!vb.empty()) out.insert(out.end(), vb.begin(), vb.end());
        return !out.empty();
    } catch (...) {
        return false;
    }
}
