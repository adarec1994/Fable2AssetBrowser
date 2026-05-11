#include "TexParser.h"
#include "Utilities/Files.h"
#include "Utilities/Utils.h"
#include "BNKCore.cpp"
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
            size_t header_bytes_total = 4 + extra_hdr_bytes;
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
    std::string key = std::filesystem::path(tex_name).filename().string();
    std::transform(key.begin(), key.end(), key.begin(), ::tolower);

    std::vector<std::string> search_paths;
    search_paths.reserve(S.bnk_paths.size() + S.nested_bnk_paths.size());
    search_paths.insert(search_paths.end(), S.bnk_paths.begin(), S.bnk_paths.end());
    search_paths.insert(search_paths.end(), S.nested_bnk_paths.begin(), S.nested_bnk_paths.end());

    if (!preferred_bnk.empty()) {
        std::string preferred_parent;
        auto it_pp = S.nested_bnk_parents.find(preferred_bnk);
        if (it_pp != S.nested_bnk_parents.end()) {
            preferred_parent = it_pp->second;
        }

        auto is_sibling = [&](const std::string& p) -> bool {
            if (p == preferred_bnk) return true;
            if (preferred_parent.empty()) return false;
            auto it = S.nested_bnk_parents.find(p);
            return it != S.nested_bnk_parents.end() && it->second == preferred_parent;
        };

        std::stable_partition(search_paths.begin(), search_paths.end(), is_sibling);
    }

    std::string header_bnk_path;
    int header_idx = -1;

    for (const auto& bnk_path : search_paths) {
        std::string fname = std::filesystem::path(bnk_path).filename().string();
        std::string fname_lower = fname;
        std::transform(fname_lower.begin(), fname_lower.end(), fname_lower.begin(), ::tolower);

        if (fname_lower.find("header") != std::string::npos && fname_lower.find("texture") != std::string::npos) {
            try {
                BNKReader reader(bnk_path);
                for (size_t i = 0; i < reader.list_files().size(); ++i) {
                    std::string file_lower = reader.list_files()[i].name;
                    std::transform(file_lower.begin(), file_lower.end(), file_lower.begin(), ::tolower);
                    std::string file_base = std::filesystem::path(file_lower).filename().string();

                    if (file_base == key) {
                        header_bnk_path = bnk_path;
                        header_idx = (int)i;
                        break;
                    }
                }
            } catch (...) {}

            if (header_idx != -1) break;
        }
    }

    if (header_idx == -1) {
        return false;
    }

    std::string mip0_bnk_path;
    int mip0_idx = -1;

    for (const auto& bnk_path : search_paths) {
        std::string fname = std::filesystem::path(bnk_path).filename().string();
        std::string fname_lower = fname;
        std::transform(fname_lower.begin(), fname_lower.end(), fname_lower.begin(), ::tolower);

        if (fname_lower.find("1024mip0") != std::string::npos && fname_lower.find("texture") != std::string::npos) {
            try {
                BNKReader reader(bnk_path);
                for (size_t i = 0; i < reader.list_files().size(); ++i) {
                    std::string file_lower = reader.list_files()[i].name;
                    std::transform(file_lower.begin(), file_lower.end(), file_lower.begin(), ::tolower);
                    std::string file_base = std::filesystem::path(file_lower).filename().string();

                    if (file_base == key) {
                        mip0_bnk_path = bnk_path;
                        mip0_idx = (int)i;
                        break;
                    }
                }
            } catch (...) {}

            if (mip0_idx != -1) break;
        }
    }

    std::string body_bnk_path;
    int body_idx = -1;

    auto scan_bnk_for_body = [&](const std::string& bnk_path) -> bool {
        try {
            BNKReader reader(bnk_path);
            for (size_t i = 0; i < reader.list_files().size(); ++i) {
                std::string file_lower = reader.list_files()[i].name;
                std::transform(file_lower.begin(), file_lower.end(), file_lower.begin(), ::tolower);
                std::string file_base = std::filesystem::path(file_lower).filename().string();

                if (file_base == key) {
                    body_bnk_path = bnk_path;
                    body_idx = (int)i;
                    return true;
                }
            }
        } catch (...) {}
        return false;
    };

    for (const auto& bnk_path : search_paths) {
        std::string fname_lower = std::filesystem::path(bnk_path).filename().string();
        std::transform(fname_lower.begin(), fname_lower.end(), fname_lower.begin(), ::tolower);

        if (fname_lower.find("texture") != std::string::npos &&
            fname_lower.find("header") == std::string::npos &&
            fname_lower.find("1024mip0") == std::string::npos) {
            if (scan_bnk_for_body(bnk_path)) break;
        }
    }

    if (body_idx == -1) {
        for (const auto& bnk_path : search_paths) {
            std::string fname_lower = std::filesystem::path(bnk_path).filename().string();
            std::transform(fname_lower.begin(), fname_lower.end(), fname_lower.begin(), ::tolower);

            if (fname_lower.find("header")    != std::string::npos) continue;
            if (fname_lower.find("1024mip0")  != std::string::npos) continue;
            if (fname_lower.find("texture")   != std::string::npos) continue;
            if (fname_lower.find(".manifest") != std::string::npos) continue;
            if (fname_lower.size() < 4 ||
                fname_lower.compare(fname_lower.size() - 4, 4, ".bnk") != 0) continue;

            if (scan_bnk_for_body(bnk_path)) break;
        }
    }

    auto tmpdir = std::filesystem::temp_directory_path() / "f2_tex_rebuild";
    std::error_code ec;
    std::filesystem::create_directories(tmpdir, ec);

    auto tmp_h = tmpdir / ("h_" + std::to_string(std::hash<std::string>{}(tex_name)) + ".bin");
    auto tmp_m = tmpdir / ("m_" + std::to_string(std::hash<std::string>{}(tex_name)) + ".bin");
    auto tmp_b = tmpdir / ("b_" + std::to_string(std::hash<std::string>{}(tex_name)) + ".bin");

    try {
        extract_one(header_bnk_path, header_idx, tmp_h.string());
        auto vh = read_all_bytes(tmp_h);

        if (vh.empty()) {
            std::filesystem::remove(tmp_h, ec);
            return false;
        }

        std::vector<unsigned char> vm;
        bool has_mip0 = false;
        if (mip0_idx != -1) {
            extract_one(mip0_bnk_path, mip0_idx, tmp_m.string());
            vm = read_all_bytes(tmp_m);
            has_mip0 = !vm.empty();
        }

        std::vector<unsigned char> vb;
        bool has_body = false;
        if (body_idx != -1) {
            extract_one(body_bnk_path, body_idx, tmp_b.string());
            vb = read_all_bytes(tmp_b);
            has_body = !vb.empty();
        }

        out.clear();
        out.reserve(vh.size() + vm.size() + vb.size());
        out.insert(out.end(), vh.begin(), vh.end());

        if (has_mip0) {
            out.insert(out.end(), vm.begin(), vm.end());
        }

        if (has_body) {
            out.insert(out.end(), vb.begin(), vb.end());
        }

        std::filesystem::remove(tmp_h, ec);
        if (has_mip0) std::filesystem::remove(tmp_m, ec);
        if (has_body) std::filesystem::remove(tmp_b, ec);

        return !out.empty();

    } catch (...) {
        std::filesystem::remove(tmp_h, ec);
        std::filesystem::remove(tmp_m, ec);
        std::filesystem::remove(tmp_b, ec);
        return false;
    }
}
