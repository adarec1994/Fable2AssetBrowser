#include "HavokPackfileReader.h"

#include "../UI/OutputLog.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace Havok {

namespace {

constexpr size_t kHeaderSize        = 0x40;
constexpr size_t kSectionHeaderSize = 0x30;

uint32_t read_u32_be(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8)  |  uint32_t(p[3]);
}

bool read_section_header(const std::vector<uint8_t>& bytes,
                         size_t offset, SectionHeader& out)
{
    if (offset + kSectionHeaderSize > bytes.size()) return false;
    const uint8_t* p = bytes.data() + offset;
    char name[17] = {0};
    std::memcpy(name, p, 16);
    out.name = std::string(name);
    out.absolute_data_start = read_u32_be(p + 0x14);
    out.local_fixups        = read_u32_be(p + 0x18);
    out.global_fixups       = read_u32_be(p + 0x1C);
    out.virtual_fixups      = read_u32_be(p + 0x20);
    out.exports             = read_u32_be(p + 0x24);
    out.imports             = read_u32_be(p + 0x28);
    out.end_offset          = read_u32_be(p + 0x2C);
    return true;
}

}

const ClassEntry* PackFile::find_class(const std::string& name) const {
    for (const auto& c : classes) {
        if (c.name == name) return &c;
    }
    return nullptr;
}

const ClassEntry* PackFile::class_for(uint32_t classnames_offset) const {
    for (const auto& c : classes) {
        if (c.classnames_offset == classnames_offset) return &c;
    }
    return nullptr;
}

std::optional<PackFile> LoadPackFileFromBytes(std::vector<uint8_t> bytes,
                                              const std::string& source_label)
{
    if (bytes.size() < kHeaderSize) {
        OutputLog::error("havok: file too small " + source_label);
        return std::nullopt;
    }

    PackFile pf;
    pf.bytes = std::move(bytes);

    static const uint8_t kMagic[8] = {0x57,0xE0,0xE0,0x57,0x10,0xC0,0xC0,0x10};
    if (std::memcmp(pf.bytes.data(), kMagic, 8) != 0) {
        OutputLog::error("havok: bad magic in " + source_label);
        return std::nullopt;
    }

    {
        const char* vp = reinterpret_cast<const char*>(pf.bytes.data() + 0x20);
        const size_t maxlen = std::min<size_t>(16, pf.bytes.size() - 0x20);
        size_t n = 0;
        while (n < maxlen && vp[n] != '\0') ++n;
        pf.version_string.assign(vp, n);
    }

    if (!read_section_header(pf.bytes, 0x40, pf.classnames_section) ||
        !read_section_header(pf.bytes, 0x70, pf.data_section) ||
        !read_section_header(pf.bytes, 0xA0, pf.types_section))
    {
        OutputLog::error("havok: failed to read section headers ("
                          + source_label + ")");
        return std::nullopt;
    }

    const auto& cs = pf.classnames_section;
    const size_t cs_start = cs.absolute_data_start;
    const size_t cs_end   = cs_start + cs.local_fixups;
    if (cs_end > pf.bytes.size()) {
        OutputLog::error("havok: classnames section past EOF ("
                          + source_label + ")");
        return std::nullopt;
    }

    {
        size_t i = cs_start;
        while (i + 5 < cs_end) {
            const uint32_t hash = read_u32_be(pf.bytes.data() + i);
            const uint8_t  sep  = pf.bytes[i + 4];
            if (sep != 0x09) {
                break;
            }
            const size_t name_start = i + 5;
            size_t j = name_start;
            while (j < cs_end && pf.bytes[j] != 0) ++j;
            if (j >= cs_end) break;
            ClassEntry ce;
            ce.hash = hash;
            ce.name.assign(reinterpret_cast<const char*>(pf.bytes.data() + name_start),
                           j - name_start);
            ce.classnames_offset = uint32_t(name_start - cs_start);
            pf.classes.push_back(std::move(ce));
            i = j + 1;
        }
    }

    {
        const auto& ds = pf.data_section;
        const size_t vf_start = ds.absolute_data_start + ds.virtual_fixups;
        const size_t vf_end   = ds.absolute_data_start + ds.end_offset;
        if (vf_end > pf.bytes.size() || vf_start > vf_end) {
            OutputLog::error("havok: virtual_fixups range invalid ("
                              + source_label + ")");
            return std::nullopt;
        }
        if (vf_end - vf_start >= 8) {
            size_t i = vf_start + 8;
            while (i + 12 <= vf_end) {
                VirtualFixup vf;
                vf.classnames_offset = read_u32_be(pf.bytes.data() + i);
                vf.data_offset       = read_u32_be(pf.bytes.data() + i + 4);
                vf.section_idx       = read_u32_be(pf.bytes.data() + i + 8);
                if (vf.classnames_offset == 0 && vf.data_offset == 0 &&
                    vf.section_idx == 0) {
                    break;
                }
                pf.virtual_fixups.push_back(vf);
                i += 12;
            }
        }
    }

    return pf;
}

std::optional<PackFile> LoadPackFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        OutputLog::error("havok: cannot open " + path);
        return std::nullopt;
    }
    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    f.seekg(0, std::ios::beg);
    if (size <= 0) {
        OutputLog::error("havok: empty file " + path);
        return std::nullopt;
    }
    const size_t sz = static_cast<size_t>(size);
    std::vector<uint8_t> bytes;
    bytes.resize(sz);
    f.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!f) {
        OutputLog::error("havok: short read on " + path);
        return std::nullopt;
    }
    return LoadPackFileFromBytes(std::move(bytes), path);
}

void LogPackFileSummary(const PackFile& pf) {
    {
        std::ostringstream os;
        os << "havok packfile: " << pf.version_string
           << "  size=" << pf.bytes.size() << "B"
           << "  classes=" << pf.classes.size()
           << "  instances=" << pf.virtual_fixups.size();
        OutputLog::info(os.str());
    }
    {
        std::ostringstream os;
        os << "  __classnames__: start=0x" << std::hex
           << pf.classnames_section.absolute_data_start
           << "  size=0x" << pf.classnames_section.local_fixups
           << std::dec;
        OutputLog::info(os.str());
    }
    {
        std::ostringstream os;
        os << "  __data__:       start=0x" << std::hex
           << pf.data_section.absolute_data_start
           << "  localFix=0x" << pf.data_section.local_fixups
           << "  globalFix=0x" << pf.data_section.global_fixups
           << "  virtFix=0x" << pf.data_section.virtual_fixups
           << "  end=0x" << pf.data_section.end_offset
           << std::dec;
        OutputLog::info(os.str());
    }
    {
        std::ostringstream os;
        os << "  __types__:     start=0x" << std::hex
           << pf.types_section.absolute_data_start
           << "  end=0x" << pf.types_section.end_offset
           << std::dec;
        OutputLog::info(os.str());
    }

    std::vector<std::pair<const ClassEntry*, size_t>> counts;
    counts.reserve(pf.classes.size());
    for (const auto& ce : pf.classes) {
        counts.emplace_back(&ce, 0);
    }
    for (const auto& vf : pf.virtual_fixups) {
        for (auto& [ce, n] : counts) {
            if (ce->classnames_offset == vf.classnames_offset) {
                ++n;
                break;
            }
        }
    }
    std::sort(counts.begin(), counts.end(),
              [](const auto& a, const auto& b) {
                  return a.second > b.second;
              });
    size_t shown = 0;
    OutputLog::info("  class instance counts (top 20):");
    for (auto& [ce, n] : counts) {
        if (n == 0) break;
        if (shown++ >= 20) break;
        std::ostringstream os;
        os << "    " << n << "  " << ce->name;
        OutputLog::info(os.str());
    }
}

void LogClassInstanceBytes(const PackFile& pf,
                           const std::string& class_name,
                           size_t max_instances,
                           size_t dump_size,
                           float world_max_xy,
                           float world_max_z)
{
    const ClassEntry* tgt = pf.find_class(class_name);
    if (!tgt) {
        OutputLog::info("  (no " + class_name + " in this packfile)");
        return;
    }

    auto read_f32_be = [](const uint8_t* p) {
        uint32_t u = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
                     (uint32_t(p[2]) << 8)  |  uint32_t(p[3]);
        float f;
        std::memcpy(&f, &u, sizeof(f));
        return f;
    };

    const size_t data_start = pf.data_section.absolute_data_start;
    const size_t data_end   = data_start + pf.data_section.end_offset;

    size_t shown = 0;
    for (const auto& vf : pf.virtual_fixups) {
        if (vf.classnames_offset != tgt->classnames_offset) continue;
        if (shown >= max_instances) break;
        ++shown;

        const size_t obj_off = data_start + vf.data_offset;
        if (obj_off + dump_size > data_end ||
            obj_off + dump_size > pf.bytes.size()) {
            continue;
        }
        const uint8_t* p = pf.bytes.data() + obj_off;

        {
            std::ostringstream os;
            os << "  " << class_name << " @data+0x" << std::hex
               << vf.data_offset << std::dec;
            OutputLog::info(os.str());
        }
        for (size_t row = 0; row < dump_size; row += 16) {
            std::ostringstream os;
            os << "    +0x" << std::hex;
            os.width(3); os.fill('0');
            os << row << ":  ";
            for (size_t i = 0; i < 16; ++i) {
                char buf[4];
                std::snprintf(buf, sizeof(buf), "%02x ", p[row + i]);
                os << buf;
            }
            OutputLog::info(os.str());
        }
        OutputLog::info("    candidate vec3 (x,y,z in world bounds):");
        bool any_hit = false;
        for (size_t off = 0; off + 12 <= dump_size; off += 4) {
            float x = read_f32_be(p + off);
            float y = read_f32_be(p + off + 4);
            float z = read_f32_be(p + off + 8);
            if (!(std::isfinite(x) && std::isfinite(y) &&
                  std::isfinite(z))) continue;
            const bool xy_ok = std::abs(x) <= world_max_xy &&
                               std::abs(y) <= world_max_xy;
            const bool z_ok  = std::abs(z) <= world_max_z;
            if (!xy_ok || !z_ok) continue;
            if (x == 0.0f && y == 0.0f && z == 0.0f) continue;
            std::ostringstream os;
            os << "      +0x" << std::hex << off << std::dec
               << "  (" << x << ", " << y << ", " << z << ")";
            OutputLog::info(os.str());
            any_hit = true;
        }
        if (!any_hit) OutputLog::info("      (none)");
    }
}

void LogRigidBodyBytes(const PackFile& pf,
                       size_t max_bodies,
                       float world_max_xy,
                       float world_max_z)
{
    LogClassInstanceBytes(pf, "hkpRigidBody", max_bodies, 0x100,
                          world_max_xy, world_max_z);
}

void ApplyLocalFixups(PackFile& pf) {
    const auto& ds = pf.data_section;
    const size_t lf_start = ds.absolute_data_start + ds.local_fixups;
    const size_t lf_end   = ds.absolute_data_start + ds.global_fixups;
    if (lf_end > pf.bytes.size() || lf_start > lf_end) return;
    if (lf_end - lf_start < 8) return;

    size_t i = lf_start;
    bool all_zero_head = true;
    for (size_t k = 0; k < 8 && i + k < lf_end; ++k) {
        if (pf.bytes[i + k] != 0) { all_zero_head = false; break; }
    }
    if (all_zero_head) i += 8;

    auto read_u32_be = [&](size_t off) {
        return (uint32_t(pf.bytes[off]) << 24) |
               (uint32_t(pf.bytes[off + 1]) << 16) |
               (uint32_t(pf.bytes[off + 2]) << 8) |
                uint32_t(pf.bytes[off + 3]);
    };
    auto write_u32_be = [&](size_t off, uint32_t v) {
        pf.bytes[off]     = uint8_t((v >> 24) & 0xff);
        pf.bytes[off + 1] = uint8_t((v >> 16) & 0xff);
        pf.bytes[off + 2] = uint8_t((v >> 8)  & 0xff);
        pf.bytes[off + 3] = uint8_t( v        & 0xff);
    };

    size_t applied = 0;
    while (i + 8 <= lf_end) {
        const uint32_t src = read_u32_be(i);
        const uint32_t dst = read_u32_be(i + 4);
        i += 8;
        if (src == 0xFFFFFFFF || dst == 0xFFFFFFFF) break;
        const size_t pfield = ds.absolute_data_start + src;
        if (pfield + 4 > pf.bytes.size()) continue;
        write_u32_be(pfield, dst);
        ++applied;
    }
    {
        std::ostringstream os;
        os << "havok: applied " << applied << " local fixups";
        OutputLog::info(os.str());
    }
}

std::vector<CollisionMesh> ExtractCollisionMeshes(const PackFile& pf) {
    std::vector<CollisionMesh> out;
    const ClassEntry* storage_cls =
        pf.find_class("hkpStorageExtendedMeshShapeMeshSubpartStorage");
    if (!storage_cls) {
        OutputLog::info("havok: no mesh subpart storage class — "
                        "nothing to extract");
        return out;
    }

    const auto& ds = pf.data_section;
    const size_t data_base = ds.absolute_data_start;
    auto read_u32_be = [&](size_t off) {
        return (uint32_t(pf.bytes[off]) << 24) |
               (uint32_t(pf.bytes[off + 1]) << 16) |
               (uint32_t(pf.bytes[off + 2]) << 8) |
                uint32_t(pf.bytes[off + 3]);
    };
    auto read_u16_be = [&](size_t off) {
        return uint16_t((uint16_t(pf.bytes[off]) << 8) |
                        uint16_t(pf.bytes[off + 1]));
    };
    auto read_f32_be = [&](size_t off) {
        uint32_t u = read_u32_be(off);
        float f;
        std::memcpy(&f, &u, sizeof(f));
        return f;
    };

    auto read_hk_array = [&](size_t arr_off) -> std::pair<uint32_t, uint32_t> {
        const size_t abs = data_base + arr_off;
        if (abs + 12 > pf.bytes.size()) return {0, 0};
        uint32_t data_off = read_u32_be(abs);
        uint32_t size     = read_u32_be(abs + 4);
        return {data_off, size};
    };

    size_t bad_count = 0;
    for (const auto& vf : pf.virtual_fixups) {
        if (vf.classnames_offset != storage_cls->classnames_offset) continue;
        const size_t inst = vf.data_offset;
        auto [verts_off, num_verts] = read_hk_array(inst + 0x08);
        auto [i16_off,   num_i16]   = read_hk_array(inst + 0x14);
        auto [i32_off,   num_i32]   = read_hk_array(inst + 0x20);

        const size_t data_max = data_base + ds.end_offset;
        if (num_verts > 0) {
            const size_t verts_abs = data_base + verts_off;
            if (verts_abs + size_t(num_verts) * 16 > data_max) {
                ++bad_count;
                continue;
            }
        }
        if (num_i16 > 0) {
            const size_t i16_abs = data_base + i16_off;
            if (i16_abs + size_t(num_i16) * 2 > data_max) {
                ++bad_count;
                continue;
            }
        }

        CollisionMesh m;
        m.vertices.reserve(size_t(num_verts) * 3);
        for (uint32_t v = 0; v < num_verts; ++v) {
            const size_t va = data_base + verts_off + size_t(v) * 16;
            m.vertices.push_back(read_f32_be(va));
            m.vertices.push_back(read_f32_be(va + 4));
            m.vertices.push_back(read_f32_be(va + 8));
        }
        m.indices16.reserve(num_i16);
        for (uint32_t k = 0; k < num_i16; ++k) {
            m.indices16.push_back(read_u16_be(data_base + i16_off + size_t(k) * 2));
        }
        m.indices32.reserve(num_i32);
        for (uint32_t k = 0; k < num_i32; ++k) {
            m.indices32.push_back(read_u32_be(data_base + i32_off + size_t(k) * 4));
        }
        if (!m.vertices.empty()) out.push_back(std::move(m));
    }

    if (bad_count > 0) {
        OutputLog::warn("havok: " + std::to_string(bad_count) +
                        " subpart storages skipped (bounds check failed)");
    }
    return out;
}

std::vector<CandidatePlacement>
ExtractCandidatePlacements(const PackFile& pf,
                           float world_max_xy,
                           float world_max_z)
{
    std::vector<CandidatePlacement> out;
    const size_t data_start = pf.data_section.absolute_data_start;
    const size_t data_end   = std::min(
        data_start + pf.data_section.local_fixups,
        pf.bytes.size());
    if (data_end <= data_start) return out;
    const uint8_t* p = pf.bytes.data();

    auto read_f32_be = [&](size_t off) {
        uint32_t u = (uint32_t(p[off]) << 24) | (uint32_t(p[off + 1]) << 16) |
                     (uint32_t(p[off + 2]) << 8)  |  uint32_t(p[off + 3]);
        float f;
        std::memcpy(&f, &u, sizeof(f));
        return f;
    };

    for (size_t off = data_start; off + 16 <= data_end; off += 16) {
        float x = read_f32_be(off);
        float y = read_f32_be(off + 4);
        float z = read_f32_be(off + 8);
        if (!(std::isfinite(x) && std::isfinite(y) &&
              std::isfinite(z))) continue;
        if (std::abs(x) > world_max_xy) continue;
        if (std::abs(y) > world_max_xy) continue;
        if (std::abs(z) > world_max_z)  continue;
        if (std::abs(x) < 1.0f && std::abs(y) < 1.0f &&
            std::abs(z) < 1.0f) continue;
        auto is_tiny_subnormal = [](float v) {
            return std::abs(v) > 0.0f && std::abs(v) < 1e-30f;
        };
        if (is_tiny_subnormal(x) || is_tiny_subnormal(y) ||
            is_tiny_subnormal(z)) continue;
        out.push_back({x, y, z});
    }

    std::sort(out.begin(), out.end(),
              [](const CandidatePlacement& a, const CandidatePlacement& b) {
                  if (a.x != b.x) return a.x < b.x;
                  if (a.y != b.y) return a.y < b.y;
                  return a.z < b.z;
              });
    auto eq = [](const CandidatePlacement& a, const CandidatePlacement& b) {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    };
    out.erase(std::unique(out.begin(), out.end(), eq), out.end());
    return out;
}

void ScanDataSection(const PackFile& pf,
                     float world_max_xy,
                     float world_max_z,
                     size_t max_strings,
                     size_t max_vec3s)
{
    const size_t data_start = pf.data_section.absolute_data_start;
    const size_t data_end   = std::min(
        data_start + pf.data_section.local_fixups,
        pf.bytes.size());
    if (data_end <= data_start) return;
    const uint8_t* p = pf.bytes.data();

    auto is_printable = [](uint8_t c) {
        return c >= 0x20 && c < 0x7f;
    };

    {
        OutputLog::info("  data-section ASCII strings:");
        size_t shown = 0;
        size_t i = data_start;
        while (i < data_end && shown < max_strings) {
            if (!is_printable(p[i])) { ++i; continue; }
            const size_t start = i;
            while (i < data_end && is_printable(p[i])) ++i;
            const size_t len = i - start;
            if (len >= 4 && i < data_end && p[i] == 0) {
                std::string s(reinterpret_cast<const char*>(p + start), len);
                std::ostringstream os;
                os << "    @data+0x" << std::hex
                   << (start - data_start) << std::dec
                   << "  '" << s << "'";
                OutputLog::info(os.str());
                ++shown;
            }
            ++i;
        }
        if (shown >= max_strings) {
            OutputLog::info("    (truncated)");
        }
    }

    auto read_f32_be = [&](size_t off) {
        uint32_t u = (uint32_t(p[off]) << 24) | (uint32_t(p[off + 1]) << 16) |
                     (uint32_t(p[off + 2]) << 8)  |  uint32_t(p[off + 3]);
        float f;
        std::memcpy(&f, &u, sizeof(f));
        return f;
    };
    {
        OutputLog::info("  data-section candidate world-vec3s "
                        "(|x|,|y|<=" + std::to_string((int)world_max_xy) +
                        ", |z|<=" + std::to_string((int)world_max_z) +
                        ", all nonzero):");
        size_t shown = 0;
        for (size_t off = data_start; off + 12 <= data_end && shown < max_vec3s;
             off += 4)
        {
            float x = read_f32_be(off);
            float y = read_f32_be(off + 4);
            float z = read_f32_be(off + 8);
            if (!(std::isfinite(x) && std::isfinite(y) &&
                  std::isfinite(z))) continue;
            if (x == 0.0f || y == 0.0f || z == 0.0f) continue;
            if (std::abs(x) > world_max_xy) continue;
            if (std::abs(y) > world_max_xy) continue;
            if (std::abs(z) > world_max_z)  continue;
            if (std::abs(x) < 1.0f && std::abs(y) < 1.0f &&
                std::abs(z) < 1.0f) continue;
            std::ostringstream os;
            os << "    @data+0x" << std::hex << (off - data_start) << std::dec
               << "  (" << x << ", " << y << ", " << z << ")";
            OutputLog::info(os.str());
            ++shown;
        }
        if (shown >= max_vec3s) OutputLog::info("    (truncated)");
        else OutputLog::info("    " + std::to_string(shown) + " total");
    }
}

}
