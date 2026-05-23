#include "AnimDecoder.h"

#include <array>
#include <cmath>
#include <cstring>

namespace Anim {

namespace {

constexpr float kQ16Scale = 2.0f / 65535.0f;
constexpr float kQ16Bias  = -1.0f;
constexpr float kEpsSmall = 1e-5f;
constexpr uint32_t kFloatsPerBoneFrame = 7;
constexpr uint32_t kBlockStride        = 8;
constexpr uint32_t kSamplesPerBlock    = 9;

inline float dequant_unit(uint32_t raw_16) {
    return (float)(raw_16 & 0xFFFF) * kQ16Scale + kQ16Bias;
}

inline float dequant_unit_bits(uint32_t raw, uint32_t bits) {
    if (bits == 0) return 0.0f;
    if (bits >= 31) bits = 30;
    const float max_raw = (float)((1u << bits) - 1u);
    return (float)raw * (2.0f / max_raw) - 1.0f;
}

inline float dequant_world_bits(uint32_t raw, uint32_t bits) {
    if (bits == 0) return 0.0f;
    if (bits >= 31) bits = 30;
    const float max_raw = (float)((1u << bits) - 1u);
    return ((float)raw * (512.0f / max_raw)) - 256.0f;
}

inline float dequant_world24(uint32_t raw_24) {
    return dequant_world_bits(raw_24 & 0xFFFFFFu, 24);
}

inline float mode3_curve_factor(uint32_t curve_index,
                                uint32_t step,
                                uint32_t run) {
    if (run == 0) return 0.0f;
    const float t = (float)step / (float)run;
    const float u = 1.0f - t;
    const float b1 = 3.0f * u * u * t;
    const float b2 = 3.0f * u * t * t;
    const float b3 = t * t * t;

    float c1 = 1.0f / 3.0f;
    float c2 = 2.0f / 3.0f;
    if (curve_index != 0) {
        uint32_t pair = curve_index - 1u;
        const uint32_t linear_pair = 32u * 64u + 32u;
        if (pair >= linear_pair) ++pair;
        const uint32_t a = pair / 64u;
        const uint32_t b = pair & 63u;
        const float x = (float)a * 0.03125f - 1.0f;
        const float y = (float)b * 0.03125f - 1.0f;
        c1 = std::fabs(x) * x * 4.0f + (1.0f / 3.0f);
        c2 = std::fabs(y) * y * 4.0f + (2.0f / 3.0f);
    }
    return b1 * c1 + b2 * c2 + b3;
}

uint32_t read_u32_be(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}

struct BitReader {
    const uint8_t* base = nullptr;
    size_t         size = 0;
    uint32_t       cursor_bits = 0;

    bool good() const { return base && cursor_bits / 8 < size; }

    void seek_bits(uint32_t bit_offset) { cursor_bits = bit_offset; }

    uint32_t read(uint32_t n) {
        if (!base || n == 0 || n > 32) return 0;
        const uint32_t bit_pos = cursor_bits & 0x1F;
        const uint32_t word_off = (cursor_bits >> 5) * 4;
        if (word_off + 4 > size) { cursor_bits += n; return 0; }
        uint32_t lo = read_u32_be(base + word_off);
        uint32_t out;
        if (bit_pos + n > 32) {
            if (word_off + 8 > size) { cursor_bits += n; return 0; }
            uint32_t hi = read_u32_be(base + word_off + 4);
            out = ((hi << (32 - bit_pos)) | (lo >> bit_pos));
        } else {
            out = lo >> bit_pos;
        }
        if (n < 32) out &= ((1u << n) - 1u);
        cursor_bits += n;
        return out;
    }
};

}

bool AnimDecoder::decode(const AnimClip& clip, float time_seconds,
                         DecodedPose& out) {
    out.ok = false;
    out.bone_quats.clear();
    out.bone_trans.clear();

    if (!global_data_file().is_open()) return false;
    auto h = global_data_file().parse_clip_header(clip);
    if (!h.ok || h.bone_count == 0) return false;

    if (h.bone_count > 4096 || h.frame_count > 65536) return false;

    const uint32_t frame_count =
        h.frame_count ? h.frame_count : 1;
    if (clip.fps <= 0.0f) return false;

    float frame_f = clip.fps * time_seconds;
    if (frame_f < 0.0f) frame_f = 0.0f;
    const float frame_max = (float)(frame_count - 1) - 0.001f;
    if (frame_max > 0.0f && frame_f > frame_max) frame_f = frame_max;

    const uint32_t frame_int = (uint32_t)frame_f;
    const uint32_t block_idx = frame_int >> 3;
    const float    frac_in_block = frame_f - (float)(block_idx << 3);

    auto sp = global_data_file().clip_bytes(clip);
    if (sp.empty()) return false;

    const uint32_t hash = cache_hash(sp.data, block_idx);
    Block& slot = cache_[hash];
    const bool hit =
        slot.clip_blob_ptr == sp.data &&
        slot.block_idx == block_idx &&
        slot.bone_count == h.bone_count;

    if (!hit) {
        if (!decode_block(h, sp.data, sp.size, h.bone_count,
                          block_idx, slot)) {

            slot = Block{};
            return false;
        }
        slot.clip_blob_ptr = sp.data;
        slot.block_idx     = block_idx;
        slot.bone_count    = h.bone_count;
    }

    interpolate(slot, frac_in_block, out);
    out.ok = true;
    return true;
}

void AnimDecoder::clear_cache() {
    for (auto& s : cache_) s = Block{};
}

bool AnimDecoder::decode_block(const AnimDataFile::ClipHeader& hdr,
                               const uint8_t* clip_blob,
                               size_t         clip_blob_size,
                               uint32_t bone_count,
                               uint32_t block_idx,
                               Block& out) {

    if (bone_count == 0 || bone_count > 4096) return false;
    const size_t n = (size_t)bone_count * kSamplesPerBlock * kFloatsPerBoneFrame;
    out.data.assign(n, 0.0f);

    for (uint32_t b = 0; b < bone_count; ++b) {
        for (uint32_t f = 0; f < kSamplesPerBlock; ++f) {
            const size_t base =
                ((size_t)b * kSamplesPerBlock + f) * kFloatsPerBoneFrame;
            out.data[base + 3] = 1.0f;
        }
    }

    const uint32_t body_off = (uint32_t)hdr.packed_body_offset;
    if (body_off > clip_blob_size) return false;
    BitReader br;
    br.base = clip_blob + body_off;

    br.size = clip_blob_size - body_off;

    auto write_channel_constant =
        [&](uint32_t bone, uint32_t channel, float value) {
            for (uint32_t f = 0; f < kSamplesPerBlock; ++f) {
                const size_t base =
                    ((size_t)bone * kSamplesPerBlock + f) *
                    kFloatsPerBoneFrame;
                if (channel < kFloatsPerBoneFrame)
                    out.data[base + channel] = value;
            }
        };

    auto read_bits_at =
        [&](uint32_t bit_offset, uint32_t n) -> uint32_t {
            BitReader tmp = br;
            tmp.seek_bits(bit_offset);
            return tmp.read(n);
        };

    auto decode_packed_value =
        [&](uint32_t raw, bool use_world_24, uint32_t bits) -> float {
            return use_world_24
                ? dequant_world_bits(raw, bits)
                : dequant_unit_bits(raw, bits);
        };

    auto decode_mode3_channel =
        [&](uint32_t bone, uint32_t channel, bool use_world_24) -> bool {
            const uint32_t start = br.cursor_bits;
            const uint32_t first_raw = br.read(use_world_24 ? 24u : 16u);
            const float first_value =
                decode_packed_value(first_raw, use_world_24,
                                    use_world_24 ? 24u : 16u);

            const uint32_t mid = start + (use_world_24 ? 24u : 16u);
            br.seek_bits(mid);
            const uint32_t scale_raw = br.read(12);
            const float value_scale = (float)scale_raw * 0.00001f;
            const uint32_t packed = br.read(9);
            const uint32_t index_bits = packed & 0xFu;
            const uint32_t value_bits = packed >> 4;

            uint32_t segment_count = 0;
            const uint32_t index_pos = mid + 21u;
            br.seek_bits(index_pos);
            if (index_bits)
                segment_count = br.read(index_bits);
            const uint32_t initial_value_int = value_bits
                ? br.read(value_bits)
                : 0u;

            const uint32_t frame_page_count =
                (hdr.frame_count > 1) ? ((hdr.frame_count - 1u) >> 5) : 0u;
            const uint32_t page_table_pos =
                index_pos + index_bits + value_bits;
            const uint32_t segment_pos =
                page_table_pos +
                (index_bits + hdr.bone_idx_bits) * frame_page_count;
            const uint32_t final_pos =
                segment_count * (value_bits + 17u) + segment_pos;
            if ((size_t)((final_pos + 7u) / 8u) > br.size)
                return false;

            const auto value_from_int =
                [&](uint32_t value_int) -> float {
                    return first_value + value_scale * (float)value_int;
                };

            const auto read_segment_target =
                [&](uint32_t segment_index) -> uint32_t {
                    const uint32_t segment_cursor =
                        segment_pos +
                        segment_index * (value_bits + 17u);
                    return value_bits
                        ? read_bits_at(segment_cursor + 17u, value_bits)
                        : 0u;
                };

            const uint32_t block_start = block_idx * kBlockStride;
            if (block_start >= hdr.frame_count) {
                br.seek_bits(final_pos);
                return true;
            }

            uint32_t segment_index = 0;
            uint32_t prev_frame = 0;
            uint32_t prev_value_int = initial_value_int;

            const uint32_t page = block_start >> 5;
            if (page > 0) {
                const uint32_t page_rec =
                    page_table_pos +
                    (page - 1u) * (index_bits + hdr.bone_idx_bits);
                segment_index = index_bits ? read_bits_at(page_rec, index_bits) : 0u;
                if (segment_index > segment_count)
                    segment_index = segment_count;
                prev_frame = hdr.bone_idx_bits
                    ? read_bits_at(page_rec + index_bits, hdr.bone_idx_bits)
                    : 0u;
                prev_value_int = segment_index
                    ? read_segment_target(segment_index - 1u)
                    : initial_value_int;
            }

            uint32_t cursor = segment_pos + segment_index * (value_bits + 17u);
            uint32_t written = 0;
            std::array<bool, kSamplesPerBlock> frame_written{};
            while (segment_index < segment_count &&
                   cursor + 17u <= final_pos &&
                   written < kSamplesPerBlock) {
                const uint32_t packed_seg = read_bits_at(cursor, 17);
                cursor += 17u;
                const uint32_t run = (packed_seg & 0x1Fu) + 1u;
                const uint32_t curve_index = packed_seg >> 5;
                const uint32_t target_value_int = value_bits
                    ? read_bits_at(cursor, value_bits)
                    : 0u;
                cursor += value_bits;

                const uint32_t next_frame = prev_frame + run;
                const float prev_value = value_from_int(prev_value_int);
                const float next_value = value_from_int(target_value_int);

                for (uint32_t f = 0; f < kSamplesPerBlock; ++f) {
                    const uint32_t abs_frame = block_start + f;
                    if (abs_frame >= hdr.frame_count) continue;
                    if (abs_frame < prev_frame || abs_frame > next_frame) continue;
                    const float t = mode3_curve_factor(curve_index,
                                                       abs_frame - prev_frame,
                                                       run);
                    const float val = prev_value + (next_value - prev_value) * t;
                    const size_t base =
                        ((size_t)bone * kSamplesPerBlock + f) *
                        kFloatsPerBoneFrame;
                    if (channel < kFloatsPerBoneFrame)
                        out.data[base + channel] = val;
                    if (!frame_written[f]) {
                        frame_written[f] = true;
                        ++written;
                    }
                }

                prev_frame = next_frame;
                prev_value_int = target_value_int;
                ++segment_index;
            }

            if (written == 0) {
                write_channel_constant(bone, channel,
                                       value_from_int(prev_value_int));
            } else if (channel < kFloatsPerBoneFrame) {
                for (uint32_t f = 0; f < kSamplesPerBlock; ++f) {
                    const uint32_t abs_frame = block_start + f;
                    if (abs_frame >= hdr.frame_count || frame_written[f])
                        continue;
                    const size_t base =
                        ((size_t)bone * kSamplesPerBlock + f) *
                        kFloatsPerBoneFrame;
                    out.data[base + channel] =
                        value_from_int(prev_value_int);
                }
            }
            br.seek_bits(final_pos);
            return true;
        };

    auto decode_one_channel =
        [&](uint32_t bone, uint32_t channel) -> bool {
            const uint32_t mode = br.read(2);
            switch (mode) {
                case 0:
                    write_channel_constant(bone, channel, 0.0f);
                    return true;
                case 1:
                    write_channel_constant(bone, channel, 1.0f);
                    return true;
                case 2: {
                    const bool use_world_24 = channel >= 4;
                    const uint32_t raw = br.read(use_world_24 ? 24u : 16u);
                    const float val = use_world_24
                        ? dequant_world24(raw)
                        : dequant_unit(raw);
                    write_channel_constant(bone, channel, val);
                    return true;
                }
                case 3: {
                    if (!decode_mode3_channel(bone, channel, channel >= 4))
                        return false;
                    return true;
                }
                default:
                    return false;
            }
        };

    const uint32_t n_items = hdr.bone_count;
    for (uint32_t b = 0; b < n_items; ++b) {
        if (b >= hdr.bone_offsets.size()) break;
        br.seek_bits(hdr.bone_offsets[b]);

        const uint32_t mode_hdr  = br.read(8);   (void)mode_hdr;
        const uint32_t count_sel = br.read(2);

        bool aborted = false;
        for (uint32_t ch = 0; ch < 7 && !aborted; ++ch) {
            if (!decode_one_channel(b, ch))
                aborted = true;
        }
        if (aborted) continue;

        const uint32_t n_extras = count_sel;
        for (uint32_t e = 0; e < n_extras && !aborted; ++e) {
            if (!decode_one_channel(UINT32_MAX, UINT32_MAX))
                aborted = true;
        }
    }

    return true;
}

void AnimDecoder::interpolate(const Block& blk, float frac_frame,
                              DecodedPose& out) const {
    const uint32_t bones = blk.bone_count;
    out.bone_quats.assign((size_t)bones * 4, 0.0f);
    out.bone_trans.assign((size_t)bones * 3, 0.0f);

    uint32_t f0 = (uint32_t)frac_frame;
    if (f0 >= kSamplesPerBlock - 1) f0 = kSamplesPerBlock - 2;
    const uint32_t f1 = f0 + 1;
    const float t  = frac_frame - (float)f0;
    const float it = 1.0f - t;

    for (uint32_t b = 0; b < bones; ++b) {
        const size_t a_base =
            ((size_t)b * kSamplesPerBlock + f0) * kFloatsPerBoneFrame;
        const size_t b_base =
            ((size_t)b * kSamplesPerBlock + f1) * kFloatsPerBoneFrame;

        const float ax = blk.data[a_base + 0];
        const float ay = blk.data[a_base + 1];
        const float az = blk.data[a_base + 2];
        const float aw = blk.data[a_base + 3];
        float bx = blk.data[b_base + 0];
        float by = blk.data[b_base + 1];
        float bz = blk.data[b_base + 2];
        float bw = blk.data[b_base + 3];
        if (ax * bx + ay * by + az * bz + aw * bw < 0.0f) {
            bx = -bx;
            by = -by;
            bz = -bz;
            bw = -bw;
        }
        float qx = ax * it + bx * t;
        float qy = ay * it + by * t;
        float qz = az * it + bz * t;
        float qw = aw * it + bw * t;
        const float q_len2 = qx*qx + qy*qy + qz*qz + qw*qw;
        const float q_inv = (q_len2 > 1e-12f)
            ? 1.0f / std::sqrt(q_len2) : 1.0f;
        qx *= q_inv; qy *= q_inv; qz *= q_inv; qw *= q_inv;
        out.bone_quats[(size_t)b * 4 + 0] = qx;
        out.bone_quats[(size_t)b * 4 + 1] = qy;
        out.bone_quats[(size_t)b * 4 + 2] = qz;
        out.bone_quats[(size_t)b * 4 + 3] = qw;

        const float tx = blk.data[a_base + 4] * it + blk.data[b_base + 4] * t;
        const float ty = blk.data[a_base + 5] * it + blk.data[b_base + 5] * t;
        const float tz = blk.data[a_base + 6] * it + blk.data[b_base + 6] * t;
        out.bone_trans[(size_t)b * 3 + 0] = tx;
        out.bone_trans[(size_t)b * 3 + 1] = ty;
        out.bone_trans[(size_t)b * 3 + 2] = tz;
    }
}

AnimDecoder& global_decoder() {
    static AnimDecoder inst;
    return inst;
}

}
