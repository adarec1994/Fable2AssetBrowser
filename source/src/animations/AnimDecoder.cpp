// AnimDecoder — runtime decode for Fable 2 animation clips.
//
// Phase F port of default.xex sub_822E1DA8 (the bit-stream decoder)
// + sub_82234A10 (the 8-frame block interpolator). See
// docs/ANIM_FORMAT.md for the full reverse-engineering notes.
//
// Status: scaffolding only. The bit reader and the cache are real;
// the per-channel decoders that fill the block are stubbed (write
// rest pose). When the stubs get filled in, the rest of the
// pipeline (interpolator → pose → AnimPlayer::apply_to_skeleton →
// MP_ComputeWorldPose → shader) consumes the result without
// further work.

#include "AnimDecoder.h"

#include <cmath>
#include <cstring>

namespace Anim {

namespace {

// Decoder constants pulled from default.xex sub_822E1DA8's prologue.
// The runtime stores Q15_SCALE as a PowerPC float spill at
// &v189 = 0.000030518044f and uses it as the dequantisation
// multiplier for unsigned 16-bit reads.
//
// Critical: the bitstream uses UNSIGNED 16-bit dequant (the asm
// uses vcfux — unsigned-int-to-float conversion — followed by
// vmaddfp). The conversion lands in [-1.0, +1.0] via:
//   f = uint16_raw * (1/32767.5) - 1.0
// approximately. The runtime uses Q15_SCALE × raw + Q15_BIAS where
// Q15_BIAS is loaded from a vor128 of broadcast(-1.0) and a
// vperm pattern. We use the equivalent scalar form below.
constexpr float kQ15Scale = 1.0f / 32767.0f;        // ≈ 0.000030518044f
constexpr float kQ15Bias  = -1.0f;                   // shifts [0..2] → [-1..+1]
constexpr float kEpsSmall = 1e-5f;                   // clamp threshold

// Dequantise a 16-bit unsigned sample from the bitstream into
// [-1.0, +1.0]. Mirrors the runtime's
//   vcfux v0, v8, 0
//   vmaddfp v7, v0, v10, v9
// pattern in case-2 of the per-channel decoder.
inline float dequant_q15(uint32_t raw_16) {
    return (float)(raw_16 & 0xFFFF) * kQ15Scale + kQ15Bias;
}

// Big-endian 32-bit read at byte offset.
uint32_t read_u32_be(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}

// Bit-stream reader. The clip's per-bone "offsets" are bit cursors
// into the post-directory body — each one points at the start of
// that bone's encoded data within a contiguous bitstream. Reads are
// LSB-first within a u32 word, big-endian word order.
//
// Mirrors the runtime's inline pattern exactly:
//   bit_pos_in_word = cursor & 0x1F
//   word_offset     = (cursor >> 5) * 4
//   if (bit_pos + n_bits) > 32:
//     concat next word, shift, mask
//   else:
//     read this word, shift, mask
// — taken from sub_822E1DA8's repeated bit-extract macro.
struct BitReader {
    const uint8_t* base = nullptr;
    size_t         size = 0;
    uint32_t       cursor_bits = 0;

    bool good() const { return base && cursor_bits / 8 < size; }

    void seek_bits(uint32_t bit_offset) { cursor_bits = bit_offset; }

    // Read n bits (n <= 32), advance cursor.
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

} // anonymous

bool AnimDecoder::decode(const AnimClip& clip, float time_seconds,
                         DecodedPose& out) {
    out.ok = false;
    out.bone_quats.clear();
    out.bone_trans.clear();

    if (!global_data_file().is_open()) return false;
    auto h = global_data_file().parse_clip_header(clip);
    if (!h.ok || h.bone_count == 0) return false;

    // Sanity bound — corrupt headers can read absurd values for these,
    // and we'd otherwise allocate gigabytes (out.data sized
    // bone_count*8*7 floats) or read OOB. 4096 is comfortably above the
    // actual max in retail (~250 bones / ~1500 items) so a real clip
    // never trips the guard. Without this the decoder crashed playback
    // when the user clicked Play on a clip with a large field_C.
    if (h.bone_count > 4096 || h.field_C > 65536) return false;

    // Compute floating-point frame: fps × time, clamped to [0, frames-1).
    const uint32_t frame_count =
        h.field_C ? h.field_C : 1;   // field_C is the frame count
    if (clip.fps <= 0.0f) return false;

    float frame_f = clip.fps * time_seconds;
    if (frame_f < 0.0f) frame_f = 0.0f;
    const float frame_max = (float)(frame_count - 1) - 0.001f;
    if (frame_max > 0.0f && frame_f > frame_max) frame_f = frame_max;

    const uint32_t frame_int = (uint32_t)frame_f;
    const uint32_t block_idx = frame_int >> 3;          // 8 frames/block
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
            // Decode failed — keep the slot empty so we don't get a
            // stale-cache hit next time.
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
    // Block layout: 8 frames × bone_count bones × 7 floats (4 quat +
    // 3 trans). Initialise to rest pose so a partial decode (or the
    // current full stub) leaves the model in a stable state.
    constexpr uint32_t kFloatsPerBoneFrame = 7;
    constexpr uint32_t kFramesPerBlock     = 8;
    if (bone_count == 0 || bone_count > 4096) return false;
    const size_t n = (size_t)bone_count * kFramesPerBlock * kFloatsPerBoneFrame;
    out.data.assign(n, 0.0f);

    // Stamp identity quats (w=1) into every (bone, frame) slot.
    for (uint32_t b = 0; b < bone_count; ++b) {
        for (uint32_t f = 0; f < kFramesPerBlock; ++f) {
            const size_t base =
                ((size_t)b * kFramesPerBlock + f) * kFloatsPerBoneFrame;
            out.data[base + 3] = 1.0f;   // quat.w
        }
    }

    // ---- Bitstream walk -------------------------------------------------
    // For each bone we seek to the bit offset stored in the
    // directory (header.bone_offsets[bone]), then read the encoded
    // header + nested 2-bit codes that select per-channel decoders.
    //
    // The runtime equivalent (sub_822E1DA8) does the work into a
    // 14-vec128 per-bone scratch buffer, then writes 8 frames of
    // half-floats out. Our skeleton drops in here when the
    // per-channel decoders (TODO below) are filled in.

    // Body starts after the 24-byte header and the directory.
    // CORRECTION: directory is `field_C` entries, not bone_count.
    // The runtime accesses dir via `*(_DWORD *)(4 * v191 + a1 + 24)`
    // and computes body base as `4 * a1[12] + a1 + 24` where
    // a1[12] = field_C.
    const uint32_t body_off = 24 + hdr.field_C * 4;
    if (body_off > clip_blob_size) return false;
    BitReader br;
    br.base = clip_blob + body_off;
    // Real bound — caller threaded clip_blob_size in. Without this the
    // bit reader could pull from past blob_'s allocation and crash on
    // unmapped pages near the end of the heap chunk.
    br.size = clip_blob_size - body_off;

    // Stamp a per-channel constant value into all 8 frames of the
    // current bone. `channel` indexes the per-bone-frame slot:
    //   0..2 = quat xyz, 3 = quat w (reconstructed, not encoded),
    //   4..6 = translate xyz, 7..8 = scale (unused by skinning).
    auto write_channel_constant =
        [&](uint32_t bone, uint32_t channel, float value) {
            for (uint32_t f = 0; f < kFramesPerBlock; ++f) {
                const size_t base =
                    ((size_t)bone * kFramesPerBlock + f) *
                    kFloatsPerBoneFrame;
                if (channel < 3)            // qx, qy, qz
                    out.data[base + channel] = value;
                else if (channel < 6)       // px, py, pz
                    out.data[base + 4 + (channel - 3)] = value;
                // channel 6 — TBD (qw-sign? scale?), drop for now
            }
        };

    // Decode one channel — reads the 2-bit mode and dispatches.
    // Returns false if mode 3 was encountered (we can't safely
    // advance the cursor without a sub_82234108 port). Mirrors
    // the inner case bodies of off_822E23E4 and off_822E256C —
    // they're byte-identical apart from destination slot, so one
    // helper handles all 7 main channels AND the count_sel inner
    // extras. `bone` and `channel` are the destination address;
    // `channel == UINT32_MAX` means "consume bits but discard"
    // (used for the inner-block channels until we hook them to
    // proper output slots).
    auto decode_one_channel =
        [&](uint32_t bone, uint32_t channel) -> bool {
            const uint32_t mode = br.read(2);
            switch (mode) {
                case 0: return true;   // CONSTANT_A (identity)
                case 1: return true;   // CONSTANT_B (assumed identity for now)
                case 2: {
                    const uint32_t raw = br.read(16);
                    const float val = dequant_q15(raw);
                    if (channel != UINT32_MAX)
                        write_channel_constant(bone, channel, val);
                    return true;
                }
                case 3:
                default:
                    return false;   // mode-3 — TODO sub_82234108
            }
        };

    // Iterate `field_C` items, NOT `bone_count`. Each "item" is a
    // keyframe / compression block per the corrected layout — the
    // 8-bit mode_hdr probably encodes which bone this keyframe
    // applies to (via `bone_idx_bits` bits), but the channel-
    // dispatch shape per item matches what we already model.
    //
    // CRITICAL: the item index `b` is NOT the destination bone
    // index. Using it as one was the cause of the crash on Play —
    // when n_items > bone_count, write_channel_constant(b, …)
    // would index past the end of out.data. Until we port the real
    // mode_hdr → bone-index extraction (Phase F), every write is
    // discarded (channel = UINT32_MAX). The bit cursor still
    // advances correctly for the future port; the output stays at
    // rest pose, which is the same behaviour the user observed
    // before — just without the OOB.
    const uint32_t n_items = hdr.field_C;
    for (uint32_t b = 0; b < n_items; ++b) {
        if (b >= hdr.bone_offsets.size()) break;
        br.seek_bits(hdr.bone_offsets[b]);

        // ---- Per-item header ----
        const uint32_t mode_hdr  = br.read(8);   (void)mode_hdr;
        const uint32_t count_sel = br.read(2);

        // ---- 7 main channels (qx qy qz px py pz ?) ----
        // Cursor advances correctly for modes 0/1 (no extra bits)
        // and mode 2 (16 bits each). Mode 3 abandons the item —
        // the next item re-seeks via hdr.bone_offsets[b+1].
        bool aborted = false;
        for (uint32_t ch = 0; ch < 7 && !aborted; ++ch) {
            if (!decode_one_channel(UINT32_MAX, UINT32_MAX))
                aborted = true;
        }
        if (aborted) continue;

        // ---- count_sel inner block ----
        // Same discard behaviour for the extras until the real
        // decoder lands; just keep the bit cursor in sync for
        // when the destination slots get wired up.
        const uint32_t n_extras = count_sel;   // 0..3
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
    const uint32_t kFloatsPerBoneFrame = 7;
    const uint32_t kFramesPerBlock     = 8;

    out.bone_quats.assign((size_t)bones * 4, 0.0f);
    out.bone_trans.assign((size_t)bones * 3, 0.0f);

    // Frame indices (clamped to block bounds) + interp factor.
    uint32_t f0 = (uint32_t)frac_frame;
    if (f0 >= kFramesPerBlock - 1) f0 = kFramesPerBlock - 2;
    const uint32_t f1 = f0 + 1;
    const float t  = frac_frame - (float)f0;
    const float it = 1.0f - t;

    for (uint32_t b = 0; b < bones; ++b) {
        const size_t a_base =
            ((size_t)b * kFramesPerBlock + f0) * kFloatsPerBoneFrame;
        const size_t b_base =
            ((size_t)b * kFramesPerBlock + f1) * kFloatsPerBoneFrame;

        // Quaternion lerp + normalise. Standard slerp would use
        // angle/sin path; lerp+normalise is what the runtime uses
        // (vmaddfp + vrsqrtefp + vmulfp pattern in sub_82234A10) —
        // close-enough for short inter-frame deltas at 30 fps.
        float qx = blk.data[a_base + 0] * it + blk.data[b_base + 0] * t;
        float qy = blk.data[a_base + 1] * it + blk.data[b_base + 1] * t;
        float qz = blk.data[a_base + 2] * it + blk.data[b_base + 2] * t;
        float qw = blk.data[a_base + 3] * it + blk.data[b_base + 3] * t;
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

} // namespace Anim
