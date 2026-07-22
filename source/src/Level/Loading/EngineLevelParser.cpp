#include "Level/Core/LevelLoader.h"
#include "Level/Loading/LevelBinaryReader.h"
#include "UI/OutputLog.h"

#include <cmath>
#include <cstring>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace Level {

namespace {
constexpr char kEngineLevelMagic[] = "LevelGraphicsFile";
constexpr size_t kEngineLevelMagicLen = sizeof(kEngineLevelMagic) - 1;
}


bool ParseEngineLevel(const std::vector<uint8_t>& bytes,
                      EngineLevelInfo&            out)
{
    out = {};
    if (bytes.size() < kEngineLevelMagicLen + 8) {
        out.error = "file too small for header";
        return false;
    }

    BeReader r{bytes.data(), bytes.size(), 0};

    if (std::memcmp(r.p, kEngineLevelMagic, kEngineLevelMagicLen) != 0) {
        out.error = "magic mismatch (expected \"LevelGraphicsFile\")";
        return false;
    }
    if (!r.skip(kEngineLevelMagicLen)) {
        out.error = "truncated reading magic";
        return false;
    }

    if (!r.u32(out.version)) {
        out.error = "truncated reading version";
        return false;
    }
    if (out.version < 11 || out.version > 12) {
        std::ostringstream os;
        os << "unsupported version " << out.version
           << " (engine accepts 11..12)";
        out.error = os.str();
        return false;
    }

    if (!r.u32(out.entry_count)) {
        out.error = "truncated reading entry_count";
        return false;
    }
    if (out.entry_count > (1u << 20)) {
        out.error = "entry_count looks corrupt";
        return false;
    }
    out.entries.reserve(out.entry_count);

    for (uint32_t mi = 0; mi < out.entry_count; ++mi) {
        EngineLevelEntry e;
        e.offset = r.i;

        if (!r.u32(e.type)) {
            std::ostringstream os;
            os << "truncated at entry " << mi << " of " << out.entry_count;
            out.error = os.str();
            out.ok = false;
            return false;
        }

        switch (e.type) {
            case 2: {
                PropBlock block;
                block.offset = e.offset;
                block.type = e.type;
                if (!r.cstr(block.model_path) ||
                    !r.cstr(block.shadow_model_path) ||
                    !r.cstr(block.lod_model_path) ||
                    !r.cstr(block.extra_model_path)) {
                    out.error = "truncated reading type-2 model paths";
                    return false;
                }

                e.str_a = block.model_path;
                e.str_b = block.lod_model_path;

                const uint32_t instance_count_off = (uint32_t)r.i;
                uint32_t instance_count = 0;
                if (!r.u32(instance_count)) {
                    out.error = "truncated reading type-2 instance count";
                    return false;
                }
                if (instance_count > 100000) {
                    out.error = "type-2 instance count looks corrupt";
                    return false;
                }

                block.instances.reserve(instance_count);
                for (uint32_t pi = 0; pi < instance_count; ++pi) {
                    PropInstance inst;
                    inst.record_file_offset = (uint32_t)r.i;
                    inst.count_file_offset = instance_count_off;
                    inst.record_size = 3 + 8 + 20 * 4;
                    if (!r.u8(inst.flags[0]) ||
                        !r.u8(inst.flags[1]) ||
                        !r.u8(inst.flags[2]) ||
                        !r.u64(inst.hash)) {
                        out.error = "truncated reading type-2 instance header";
                        return false;
                    }
                    inst.pos_file_offset = (uint32_t)r.i;
                    inst.lev_rec_kind = 1;
                    for (float& v : inst.values) {
                        if (!r.f32(v)) {
                            out.error = "truncated reading type-2 instance floats";
                            return false;
                        }
                    }
                    block.instances.push_back(inst);
                }

                out.prop_blocks.push_back(std::move(block));
                break;
            }
            case 4:
            case 5:
            case 32: {
                if (!r.cstr(e.str_a)) {
                    out.error = "truncated reading string for type "
                              + std::to_string(e.type);
                    return false;
                }
                if (e.type == 4) {
                    if (!r.u64(e.resource_key)) {
                        out.error = "truncated reading type-4 tail";
                        return false;
                    }
                    e.has_resource_key = true;
                }
                break;
            }
            case 21: {
                e.str_b.clear();
                if (!r.cstr(e.str_a)) {
                    out.error = "truncated reading string A for type 21";
                    return false;
                }
                if (!r.cstr(e.str_b)) {
                    out.error = "truncated reading string B for type 21";
                    return false;
                }
                if (!r.skip(8 + 1 + 1)) {
                    out.error = "truncated reading type-21 hash+flags";
                    return false;
                }

                const uint32_t loop1_count_off = (uint32_t)r.i;
                uint32_t loop1_count = 0;
                if (!r.u32(loop1_count)) {
                    out.error = "truncated type-21 ext header";
                    return false;
                }
                if (!r.skip(7 * 4 + 12 + 4 + 24)) {
                    out.error = "truncated type-21 ext header (mid)";
                    return false;
                }
                if (loop1_count > 100000) {
                    out.error = "type-21 loop1 count looks corrupt";
                    return false;
                }

                PropBlock t21_block;
                t21_block.offset = e.offset;
                t21_block.type = e.type;
                t21_block.model_path = e.str_a;
                t21_block.lod_model_path = e.str_b;
                t21_block.instances.reserve(loop1_count);

                if (out.version == 11) {
                    for (uint32_t k = 0; k < loop1_count; ++k) {
                        PropInstance inst;
                        inst.record_file_offset = (uint32_t)r.i;
                        inst.count_file_offset = loop1_count_off;
                        inst.record_size = 4 * 4;
                        inst.pos_file_offset = (uint32_t)r.i;
                        inst.lev_rec_kind = 2;
                        for (int j = 0; j < 4; ++j) {
                            if (!r.f32(inst.values[j])) {
                                out.error = "truncated type-21 v11 loop1 body";
                                return false;
                            }
                        }
                        inst.values[7] = 1.0f;
                        inst.values[9] = inst.values[10] = inst.values[11] = 1.0f;
                        t21_block.instances.push_back(inst);
                    }
                } else {
                    for (uint32_t k = 0; k < loop1_count; ++k) {
                        const uint32_t pos_off = (uint32_t)r.i;
                        float pos[3];
                        if (!r.f32(pos[0]) || !r.f32(pos[1]) || !r.f32(pos[2])) {
                            out.error = "truncated type-21 v12 instance vec3";
                            return false;
                        }
                        float qx, qy, qz, qw, scale;
                        if (!r.half(qx) || !r.half(qy) ||
                            !r.half(qz) || !r.half(qw) ||
                            !r.half(scale)) {
                            out.error = "truncated type-21 v12 instance quat/scale";
                            return false;
                        }
                        PropInstance inst;
                        inst.record_file_offset = pos_off;
                        inst.count_file_offset = loop1_count_off;
                        inst.record_size = 3 * 4 + 5 * 2;
                        inst.pos_file_offset = pos_off;
                        inst.lev_rec_kind = 3;
                        inst.values[0] = pos[0];
                        inst.values[1] = pos[1];
                        inst.values[2] = pos[2];

                        const float num = 2.0f * (qw * qz + qx * qy);
                        const float den = 1.0f - 2.0f * (qy * qy + qz * qz);
                        const float mag = std::sqrt(num * num + den * den);
                        if (mag > 1e-6f) {
                            inst.values[6] = num / mag;
                            inst.values[7] = den / mag;
                        } else {
                            inst.values[6] = 0.0f;
                            inst.values[7] = 1.0f;
                        }

                        const float s = (scale > 0.0f) ? scale : 1.0f;
                        inst.values[9]  = s;
                        inst.values[10] = s;
                        inst.values[11] = s;
                        t21_block.instances.push_back(inst);
                    }
                }

                const uint32_t loop2_count_off = (uint32_t)r.i;
                uint32_t loop2_count = 0;
                if (!r.u32(loop2_count)) {
                    out.error = "truncated type-21 loop2 count";
                    return false;
                }
                if (loop2_count > 100000) {
                    out.error = "type-21 loop2 count looks corrupt";
                    return false;
                }

                size_t loop2_emitted = 0;
                size_t loop2_skipped = 0;
                for (uint32_t k = 0; k < loop2_count; ++k) {
                    const uint32_t rec_off = (uint32_t)r.i;
                    float a_val, b_val;
                    float p1[3], p2[3];
                    if (!r.f32(a_val) || !r.f32(b_val) ||
                        !r.f32(p1[0]) || !r.f32(p1[1]) || !r.f32(p1[2]) ||
                        !r.f32(p2[0]) || !r.f32(p2[1]) || !r.f32(p2[2]))
                    {
                        out.error = "truncated type-21 loop2 body";
                        return false;
                    }

                    auto in_bounds = [](float v, float lo, float hi) {
                        return std::isfinite(v) && v >= lo && v <= hi;
                    };
                    const bool plausible =
                        in_bounds(p1[0], -2048.0f, 2048.0f) &&
                        in_bounds(p1[1], -2048.0f, 2048.0f) &&
                        in_bounds(p1[2], -512.0f,   512.0f) &&
                        (std::fabs(p1[0]) + std::fabs(p1[1]) + std::fabs(p1[2]) > 0.5f);
                    if (!plausible) {
                        ++loop2_skipped;
                        continue;
                    }

                    PropInstance inst;
                    inst.record_file_offset = rec_off;
                    inst.count_file_offset = loop2_count_off;
                    inst.record_size = 8 * 4;
                    inst.pos_file_offset = rec_off + 8;
                    inst.lev_rec_kind = 4;
                    inst.values[0] = p1[0];
                    inst.values[1] = p1[1];
                    inst.values[2] = p1[2];
                    const float fxy =
                        std::sqrt(p2[0] * p2[0] + p2[1] * p2[1]);
                    if (std::isfinite(fxy) && fxy > 0.001f && fxy < 100.0f) {
                        inst.values[6] = p2[1] / fxy;
                        inst.values[7] = p2[0] / fxy;
                    } else {
                        inst.values[6] = 0.0f;
                        inst.values[7] = 1.0f;
                    }
                    const float s = (std::isfinite(a_val) &&
                                     a_val > 0.05f && a_val < 100.0f)
                                        ? a_val : 1.0f;
                    inst.values[9]  = s;
                    inst.values[10] = s;
                    inst.values[11] = s;
                    t21_block.instances.push_back(inst);
                    ++loop2_emitted;
                }
                (void)loop2_emitted;
                (void)loop2_skipped;

                if (!t21_block.instances.empty()) {
                    out.prop_blocks.push_back(std::move(t21_block));
                }
                break;
            }
            default: {
                std::ostringstream uos;
                uos << "  unknown entry type 0x"
                    << std::hex << e.type << std::dec
                    << " (" << e.type << ") at offset 0x"
                    << std::hex << e.offset << std::dec
                    << " - last 6 entries:";
                OutputLog::warn(uos.str());
                const size_t n = out.entries.size();
                for (size_t k = (n > 6 ? n - 6 : 0); k < n; ++k) {
                    const auto& pe = out.entries[k];
                    std::ostringstream ros;
                    ros << "    [" << k << "] type=" << pe.type
                        << " @ 0x" << std::hex << pe.offset
                        << "  size=" << std::dec << pe.size;
                    if (!pe.str_a.empty()) ros << "  a=" << pe.str_a;
                    if (!pe.str_b.empty()) ros << "  b=" << pe.str_b;
                    OutputLog::info(ros.str());
                }
                e.size = 0;
                out.entries.push_back(e);
                out.ok = true;
                return true;
            }
        }
        e.size = r.i - e.offset;
        out.entries.push_back(std::move(e));
    }

    out.ok = true;
    return true;
}

}
