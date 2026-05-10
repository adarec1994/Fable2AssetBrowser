# Fable 2 Animation Format

Reverse-engineering notes from the Xbox 360 `default.xex` (IDA) and the
two animation files shipped on disc:

```
data/animation/fable2_anims.animation_toc       # 481 KB  (string + event index)
data/animation/fable2_anims.animation_data      # 25 MB   (compressed clips)
```

## TL;DR

**Fable 2 uses a custom Lionhead format**, NOT Havok hka. The binary
*does* link the Havok 6.5 hka classes (you can find the strings
`hkaInterleavedSkeletalAnimation`, `hkaWaveletSkeletalAnimation`,
`hkaDeltaCompressedSkeletalAnimation`, `hkaAnimationBinding`,
`hkaSkeletonMapper`, etc.) but they're vestigial — the type-info gets
registered for Havok's RTTI machinery and that's it. The actual clip
data in `fable2_anims.animation_data` is a Lionhead-authored
bit-packed compression with a per-bone offset table.

The earlier "must be Havok" guess was wrong. Sorry, future-self.

## Update — Phase D findings (post on-disk inspection)

Every clip blob in `.animation_data` starts with the same magic as
the master container — 0xCEA5EBED — and is a self-contained file.
The TOC's `AnimRecord.data_offset` points directly to the start of
one of these blobs.

```
Per-clip header (24 bytes, big-endian):
  +0x00  u32  magic         = 0xCEA5EBED      (always)
  +0x04  u32  version       = 7               (always)
  +0x08  u32  field_8       ≈ 0xFF            (a few outliers — 0x40, 0x2E, 0x32, 0x31)
  +0x0C  u32  field_C       (varies 22..153, peaks at 135/137/138/146 —
                              probably frame count or reference-pose stride)
  +0x10  u32  bone_count    (= TOC AnimRecord+12)
  +0x14  u32  bone_idx_bits = ceil(log2(bone_count)) — observed mapping:
                              bones=1 → 1
                              bones=2..3 → 2
                              bones=4..7 → 3
                              bones=8..15 → 4
                              bones=16..30 → 5
                              bones=32..50 → 6
                              bones=64..128 → 7
                              bones=128..256 → 8
  +0x18  u32  zero pad
  +0x1C  u32[bone_count]  per-bone track offsets, relative to clip start
   ...    bit-packed track data
```

This was determined by parsing the TOC + dumping all 4082 clip
prefixes; results are reproducible via `tools/peek_clips.py`.

The "compression kind" I expected to find at u32[5] is just the bit
width of bone indices in the body — necessary because the body uses
variable-width bit-packing rather than fixed-byte tracks. Havok-style
type dispatch (interleaved / delta / wavelet) doesn't apply.

**Implications for the decoder**:
* No Havok loader can read these blobs. We're writing the decoder
  from scratch.
* Format is regular though: 24-byte header → bone-count zero pad →
  per-bone offset table → bit-packed bodies. Each per-bone body is
  independently sliceable.
* Bit-packing layout still needs more reverse engineering — likely
  per-frame quantised quaternion + position with the per-clip
  reference pose stored somewhere in the body (or in the
  `field_C`/`field_8` regions of the header).

---

## Bootstrap path (where the game opens these files)

`sub_822F3B18` (called from main app init) does the equivalent of:

```cpp
auto data = make_string("fable2_anims.animation_data");
auto toc  = make_string("fable2_anims.animation_toc");
sub_82A492B0();                  // critical-section init
auto file_toc  = sub_821F0108(toc);
auto bank      = sub_82A48B68(data, file_toc);   // see below
sub_82A493A8(bank);              // finalise
```

`sub_82A48B68` allocates a 60-byte AnimBank singleton at
`dword_8349F518`, then:

```cpp
sub_82B80D70(bank, &toc_path);   // parse fable2_anims.animation_toc
sub_82B820A8(bank, &data_path);  // mmap fable2_anims.animation_data
sub_82B82248(bank);              // finalise: bind toc records → data offsets
```

Three functions, three distinct jobs.

---

## animation_toc — fully decompiled

`sub_82B80D70` parses this end-to-end. Header layout (big-endian):

```
+0x00  char[8]   magic "AnimBank"
+0x08  u32       version       == 1
+0x0C  u32       sub-version   == 5
+0x10  u32       num_anim_records       (e.g. 0x0FF2 = 4082)
+0x14  u32       num_special_records    (e.g. 0x012B =  299)
+0x18  u32       num_strings            (e.g. 0x0A7B = 2683)
+0x1C  string-table follows: num_strings null-terminated strings
                 (concatenated, indexed by 32-bit offsets into the table)
       AnimRecord[num_anim_records]      — each is 36 bytes
       SpecialRecord[num_special_records]
```

The string table holds animation names (`SE_FINISHER_PULL_OUT`,
`STANCE GOOFY`, `STRIKE_START RIGHT_HAND`, …), action verbs
(`ACTION PERFORM`, `IK_TARGET_SET`, …) and parameters (`MyBone=…`).

### AnimRecord (36 bytes)

```
+0x00  u32        clip-key (high half — used as a hash for sorted lookup)
+0x04  u32        clip-id  (low half — looks up data offset in the bank)
+0x08  u32        offset_in_data    ← byte offset into fable2_anims.animation_data
+0x0C  u32        length_or_end     ← passed to clip ctor as a3
+0x10  f32        duration / framerate (passed as a4)
+0x14  u32        event_count
+0x18  ptr        event_array       (allocated, filled below)
+0x1C  ptr        event_array_end
+0x20  u32        unused / padding
```

Followed by `event_count` events, each:

```
struct AnimEvent {
    f32  time;
    u32  name_idx;        // index into the string table
    u32  param_idx;       // index into the string table (or 0xFFFFFFFF)
};   // 12 bytes
```

### SpecialRecord (60 bytes after construction)

Used for "ability bindings" — graph nodes that pair an animation with
context (combat type, stance, finisher, etc.). The on-disk shape is a
6-tuple of u32 fields followed by a list of `(string-index,
string-index)` pairs and a final `(float, u32)` pair. Less critical for
playback; this is metadata, not pose data.

---

## animation_data — header only (from on-disk inspection)

The body is Havok-format and would be decoded by Havok runtime calls,
which we won't be re-implementing from scratch — see the next section.

```
+0x00  u32        magic    0xCEA5EBED
+0x04  u32        version  7
+0x08  u32        ?        0xFF   (max bones?)
+0x0C  u32        ?        0x87   (clip count?)
+0x10  u32        ?        0x30
+0x14  u32        ?        0x06
+0x18  u32        0
+0x1C  u32[N]     master offset table — N entries, each absolute file
                 offset of one Havok-serialised clip blob
```

The master table runs ~99 entries (until offset 0x1A8) and references
clip blobs further down. **Each clip blob itself starts with another
offset table** for per-bone tracks, then the bit-packed quantised data.
At a single byte level it looks completely random
(`@._...7....8080f6a8…`) which is consistent with Havok's wavelet /
delta quantisation — see `hkaWaveletSkeletalAnimationQuantizationFormat`.

The TOC's `AnimRecord.offset_in_data` indexes directly into this file.
At runtime the game does
`bank.data_buf + record.offset_in_data` and stores the resulting
pointer at `clip+60` (see `sub_82B722E8`, the clip ctor — its 80-byte
class is laid out `vtable, refcount, refcount2, …, parent_string,
flags, sample_owner, duration, offset, *clip_data, file_obj, …`).

---

## Decoding strategy for the asset browser

Don't write a Havok decoder from scratch. Two cheaper paths:

1. **Convert at extract-time using a public Havok loader.**
   Tooling exists: `havok-classes` (Python), `chcInfo`/`hkxcmd`,
   the NifTools havok-* projects. They take an hka blob and produce
   either a serialised `hkaInterleavedSkeletalAnimation` (uncompressed
   per-frame matrices) or directly emit something we can map to glTF.

2. **Implement only the interleaved + delta paths.**
   Wavelet is the painful one. Interleaved is just `frames × bones × 4
   floats (quat) + 3 floats (pos)`. Delta is interleaved on top of a
   fixed reference frame plus quantised per-component deltas — also
   well-documented. We ship interleaved/delta playback and refuse to
   load wavelet clips with a clear log line.

The TOC parser is the easy part — we already have a complete spec for
it (see above) and could write a one-pass C++ reader in ~150 lines.
That's what to land first; clip decode comes after.

---

## Update — Phase F findings (decoder located, port pending)

The full decode pipeline lives in `default.xex`:

```
sub_82515540   character init, builds an in-place clip and samples at t=0
    └── sub_822351F0   sample wrapper: clamps time, computes frame =
        │              fps * time, looks up the bone remap table, dispatches
        │              the per-bone target pose into the output buffer
        └── sub_822E1DA8  ★ the real decoder ★
             ├── header read (a1 = clip blob ptr — same as our ClipHeader)
             │     a1+0x0C  = field_C   (loop count, clamped)
             │     a1+0x10  = bone_count or frame_count (still ambiguous)
             │     a1+0x14  = bone_idx_bits
             │     a1+0x18  = directory base (we call it bone_offsets[])
             ├── time → frame_block lookup (frame_block = floor(frame) >> 3)
             │     8 frames per decoded block; the result is cached in a
             │     1024-bucket open-addressing hash keyed by
             │       (clip_blob_ptr >> 4) ^ frame_block
             │     so re-sampling within the same 8-frame window doesn't
             │     re-decode.
             ├── bitstream loop (per-entry, iterating
             │     min(a1+0x0C, target_count) times):
             │     v51 = directory[i]                   // BIT offset, not
             │                                          // byte offset
             │     bits = read 8 bits   → mode/header
             │     bits = read 2 bits   → v71 (count selector? 0-3)
             │     bits = read 2 bits   → v75 → switch
             │       case 0 (uncompressed): write identity, advance
             │                              and read 2 more bits → nested
             │                              switch (9 levels deep, one per
             │                              channel: qx, qy, qz, px, py,
             │                              pz, sx, sy, sz)
             │       cases 1-3: dispatch into off_822E23E4[v75] —
             │                  per-channel decoders that produce
             │                  variable-length outputs
             │     produces a 14-vec128 (224-byte) chunk per iteration —
             │     the 8 frames worth of transforms for the next bone
             ├── inner decoders (when a 2-bit code != 0):
             │     sub_82B6ECC8   → decode one channel at compression
             │                       level 1
             │     sub_82234790   → decode one channel at compression
             │                       level 2-3
             └── sub_82234A10   interpolator: takes the 8-frame block
                                  and the fractional frame within it
                                  (frac = float_frame - 8*frame_block),
                                  outputs the final per-bone transforms
                                  into the caller's buffer
```

Per-bone offsets in the directory are **bit offsets into the post-
directory bit-packed body**, not byte offsets. That means
`AnimDataFile::ClipHeader::bone_offsets` is misnamed — it's a bit
cursor into a continuous bit-stream. Need to revisit the parser when
we land the decoder.

The encoding is **proprietary, multi-level, entropy-coded** — 2-bit
codes select among 4 sub-encodings per channel, and each case can
recursively sub-divide further. It's not a Havok format and it's not
a simple quantisation either. Closer to a custom adaptive
predictor + variable-bit-width quantisation hybrid.

### Why we're pausing the port here

`sub_822E1DA8` is ~2000 lines of decompiled code with 11 jump-table
based switches. Faithfully porting it requires:

1. Decoding every per-channel jump table (`off_822E23E4`,
   `off_822E256C`, `off_822E26E4`, `off_822E285C`, `off_822E29D4`,
   `off_822E2B4C`, `off_822E2CC4`, `off_822E2E54`, `off_822E2FDC`,
   `off_822E3154`, `off_822E32C0`) — each is the case-table for a
   compression-level switch.
2. Reverse-engineering the two inner decoders (`sub_82B6ECC8`,
   `sub_82234790`) — these are the level-2/3 channel decoders.
3. Re-reading + porting `sub_82234A10` — the 8-frame interpolator
   that consumes the decoded blocks.
4. Decoding the 14-vec128 working buffer layout (`v204` … `v222`)
   to figure out which slots hold quat-x/y/z, pos-x/y/z, scale-x/y/z
   etc.
5. Validating against ground truth — comparing our output against
   actual game-derived poses for at least one known clip.

That's a multi-day push, not a single landing. The infrastructure
to receive it is already in place (`AnimPlayer::apply_to_skeleton`
has the `TODO(phase-E-decoder)` block; data file is mmap'd; clip
header is parsed; bone offsets are exposed). The next session can
pick up directly from the IDA decompile + this doc.

### Validation milestone (Phase F session N+2) — corrected directory model

After dumping clip[6]'s `field_C = 135` directory entries past the
25 we'd been examining, we confirmed **all 135 entries are
monotonically increasing bit offsets**. So:

- The directory has **`field_C` entries (= a1[12])**, NOT
  `bone_count`. Iterating `bone_count` items missed 80% of the
  data.
- `body_base = clip + 24 + 4 * field_C`. The first directory
  entry is at clip+24 (the runtime's `*(_DWORD *)(4 * v191 + a1
  + 24)` indexing makes a1+24 the first entry).
- Each directory entry corresponds to a **keyframe / compression
  block** — likely encodes a `(bone_index, frame_or_slot)` pair
  via the 8-bit mode_hdr's first `bone_idx_bits` bits.

C++ landed: `AnimDataFile::ClipHeader.field_C` is now the directory
length; the decoder loop iterates `field_C` items at the corrected
body_base.

### Bit-consumption gap analysis (Phase F session N+2)

Brute-force: measured `(real_bits = dir[i+1] - dir[i])` minus
`(my_model_bits)` for items that decode without aborting. Results
across 50K+ items in 500 clips:

```
mode_pattern              n_items  most_common_gap (bits)
(0,0,0,1,0,0,0)            314    0 (267 of 314 = 85% of mode-0-only items match perfectly)
(0,0,0,1,0,0,2)            677    8 (always +8)
(0,0,0,1,0,2,0)            3214  32 (3053 of 3214)
(0,0,0,1,2,0,0)            5242  56 (3794 of 5242)
(2,2,2,2,2,0,0)             878  56 (5 mode-2s contiguous at front)
(2,2,2,2,0,0,2)             190   8 (5 mode-2s but split)
```

**Key findings**:

1. **Pure mode-0 items decode at 0 gap** in 85% of cases. The bit
   reader, header parse, channel dispatch, and count_sel inner
   block are all *correct* for the no-data case.
2. **Mode-2 has context-dependent extra cost**. Each channel
   position seems to add 24 bits of additional state when mode-2
   is present at certain positions. The pattern isn't a simple
   "16 bits per mode 2" — there's per-channel state expansion
   following a mode-2 read.
3. **Mode 1 is occasionally +8 bits** (47 of 314 in the all-zero
   pattern showed gap=8 instead of 0), suggesting mode 1 also
   has a variable component.

The most likely explanation: each channel's case body reads a
**per-channel state header** (basis index, scale group, etc.)
in addition to the core dispatch. The vperm + vor scaffolding
in the IDA disasm hides these reads inside what I'd interpreted
as alignment-only code.

### Why we can't continue purely from IDA

Without a debugger that can dump `(clip_blob, time, output)`
tuples for known clips at runtime, we're guessing at the
per-channel state layout. The IDA decompile compresses the
state-extraction work into vperm/SIMD scaffolding that's
notoriously hard to read line-by-line.

The realistic forward path:

1. **Set up Xenia** (Xbox 360 emulator) and run Fable 2
2. **Set a breakpoint at `0x822E1DA8`** (the decoder entry)
3. **Dump 5–10 known clips' input bytes + output transforms**
   for a single bone at frame 0, with all-mode-0 / all-mode-2 /
   mixed patterns
4. **Diff against our Python decoder output** until the bit
   consumption per case matches the runtime byte-for-byte
5. **Port the matching algorithm to AnimDecoder.cpp**

Estimate: 1 setup session + 1–2 validation sessions + 1
implementation session = 3–4 sessions of focused work to
finish Phase F. Ground truth is non-negotiable for a decoder
this complex.

### Validation milestone (Phase F session N)

A Python replica of `AnimDecoder` walked all 4082 clips / 294326
bones in the retail TOC. Stats:

```
Bones total:    294326
Bones clean:     94607  (32.1%)   <- every channel decoded, cursor consistent
Bones aborted:  199719  (67.9%)   <- hit a mode-3 channel

Mode distribution (over 1.3M total channels):
  mode 0:  53%   constant A (rest pose, no bits)
  mode 1:  15%   constant B (no bits)
  mode 2:  17%   inline 16-bit Q15
  mode 3:  14.5% multi-frame VLC (sub_82234108)

count_sel distribution:
  cs=0: 43%   no extras
  cs=1: 19%   1 extra channel
  cs=2: 19%   2 extras
  cs=3: 19%   3 extras
```

**32.1% bones decode cleanly** — proves the bit-stream model is
right structurally:
- Bit reader (LSB-first within u32 words, big-endian word order) ✓
- Per-bone header (8b mode_hdr + 2b count_sel) ✓
- 7-channel main block ✓
- count_sel inner block (0..3 extras) ✓
- Mode 0/1/2 bit consumption (0/0/16 bits) ✓
- Per-bone bit-offset directory ✓

The remaining 68% of bones contain at least one mode-3 channel
which calls `sub_82234108`. Porting that unlocks the rest.

A test on clip[3] (1 bone, 149 frames) gave a clean decode of one
mode-2 channel = `-0.99966`. The channel order (qx/qy/qz vs
qw/qx/qy/qz vs other) and the sign convention for the dequant are
not yet validated against ground-truth game state — that's the
final pre-release gate.

### Quick reference for the resume

* Decoder entry: `0x822E1DA8` — `sub_822E1DA8(blob, time_double,
  out_pose, n, n, idx_buf, scratch16, frame_float)`
* Sample wrapper: `0x822351F0` — does fps×time and bone remap
* Cache: 1024-bucket array at `dword_833703E0`, hash gate at
  `byte_8331460A`
* Per-channel jump tables: see addresses above
* Sample call site for ground-truth: `sub_82515540` calls the
  sampler at t=0.0 to load the bind pose. Easiest to set a
  conditional breakpoint there.

### How the inner decoders actually decode

Both `sub_82B6ECC8` and `sub_82234790` are **palette / VQ
reconstruction decoders**. The runtime stores a small set of
4-vector basis entries per bone and reconstructs each output sample
as a weighted dot product:

```
output_half = pack5_2_2(
    dot(
        palette_weights * basis[basis_idx] + offset,
        target_vector[target_idx]
    )
)
```

Where:
- `palette_weights` is a 4-float weight vector loaded from `result+16`
- `basis[basis_idx]` is a 4-float entry from a per-bone palette
  (one of N entries). Indexed by a u32 read from the variable-length
  stream.
- `offset` is a constant 4-float bias also from the palette area
- `target_vector[target_idx]` is from a *second* palette, also indexed
  by a u32 from the stream
- `pack5_2_2` is `vpkd3d128 ?, ?, 5, 2, 2` — packs a float to a
  16-bit half-float (or 16-bit fixed-point, TBD)
- Output is stored 8 bytes apart (one per frame in the 8-frame block)

The level-1 (`sub_82B6ECC8`) and level-2/3 (`sub_82234790`) decoders
differ in how many palette entries they read per sample and the size
of the basis vectors. `sub_82234790` has a `v3 == 9` fast path —
unrolled to read 9 samples in a single SIMD batch — and a generic
loop for arbitrary counts.

This is a hybrid of VQ codebooks + linear blending — at its heart
the same scheme as Animation Compression Library (ACL) "fix-pose"
encoding, just with Lionhead-flavoured indexing and pack format.

### Decoder constants (function-prologue values)

The runtime sets these once at function entry; they're used
throughout the decode:

```
Q15_SCALE     = 0.000030518044f       // = 1.0 / 32767.0
EPS_SMALL     = 0.0000099999997f      // = 1e-5 (clamp threshold)
NEG_256       = -256.0f               // shift constant for signed
                                      //   sign-extension paths
Q22_SCALE     = 0.000030517582f       // = 1.0 / 32768.0 (q15 alternate)
SHIFT_15      = vspltisw 0xF          // 15 (sign-bit position for q15)
SHIFT_8       = vspltisw 0x8          // 8  (used for byte-alignment work)
PERM_TABLE    = unk_820991C0          // 4 vperm masks for byte-level
                                      // shifts (alignment-dependent reads)
```

The Q15 scale is **the** dequantisation multiplier — every encoded
channel value is a 16-bit signed integer, dequantised as
`f = (int16_value) * Q15_SCALE`. Result lands in [-1.0, +1.0]
which is the natural range for normalised quaternion components and
unit-vector positions.

### Top-level switch (per channel, off_822E23E4)

After reading the 8-bit mode header + 2-bit count, each of the 9
channels is encoded with a 2-bit mode selector:

```
case 0 (0x822E23F4):
    v204 := v94               // CONSTANT_A vector — likely identity
    var_770 = 1               // count = 1 (single-frame channel)
    → fall through to next channel

case 1 (0x822E240C):
    v204 := v95               // CONSTANT_B vector — different constant
    var_770 = 1
    → fall through to next channel

case 2 (0x822E2424):  ★ inline single-value bit-packed
    bits = read_uniform_bits(width=2)        // current cursor &0x1F, etc.
    raw  = (load << shift) | (load2 >> ...)  // the standard bit-extract
    val  = vmaddfp(raw, scale, offset)       // dequantise: f = raw * s + b
    v204 := val
    var_770 = 1               // single value covers all 8 frames

case 3 (0x822E24D4):  ★ multi-frame variable-length palette decode
    call sub_82234108(...)
    var_770 = N               // N = decoded frame count (variable)
    → fall through

→ loc_822E2500: read 2 bits → next channel's mode, dispatch via
                off_822E256C
```

The same shape repeats nine levels deep — once for each of qx, qy,
qz, px, py, pz, sx, sy, sz. Mode 0 is "no animation, hold constant
A" — the identity case that 90%+ of static channels take. Mode 3 is
the heavyweight: an 8-frame variable-length stream.

### sub_82234108 — multi-frame variable-length decoder

This is the hot path for animated channels. It's a **predictor +
quantised-delta** encoder using lookup tables:

- `dword_82096140[N]` = `(1 << N) - 1` — power-of-two bit mask LUT
- `flt_833603E0[]`    = palette of basis float values
- `unk_83360350[]`    = secondary index table

Header decode (per channel):
```
bit_width  = read_n_bits(4)   // v22, 0..15
delta_n    = read_n_bits(5)   // v24, 0..31
mask       = dword_82096140[bit_width]   // (1 << bit_width) - 1
```

Then for each of the 8 frames in the block:
```
frame_idx_bits = read_n_bits(17)
selector = frame_idx_bits & 0x1F     // v65, picks float palette entry
hi       = frame_idx_bits >> 5       // v64 / v25, picks scale group
basis_a  = flt_833603E0[hi]          // basis vector to interp with
quant    = read_n_bits(bit_width)    // delta from previous frame
value    = predictor + (quant - 0.5*mask) * basis_a    // dequantise
predictor = value                    // for next frame
output[frame * 16 + slot] = pack_half(value)
```

The encoder also writes "key frame" markers (`stvewx`) — frames where
a different basis is used or a re-anchor occurs. These mark the
boundaries between sub-segments inside a block.

Channel runs of `count > 1` are produced by this path; the
interpolator (`sub_82234A10`) reads them as a sequence of
already-dequantised half-floats and lerps within the 8-frame block
at the final time.

### Working-buffer layout (v204 .. v222)

Inside `sub_822E1DA8`, the per-bone scratch space is a sequence of
14 vec128 slots (224 bytes) labelled v204, v206, v208, v210, v212,
v214, v216 in the IDA decompile, with v218, v220, v222 as
auxiliary. The decoded 8-frame block is packed half-floats into
`_R16` — a separate output region. The flow per bone is:

1. Read header (8b mode + 2b count + 2b top-mode).
2. For each of 9 channels, read 2 bits → mode for that channel:
   * `case 0`: store identity-equivalent vec128 into the channel's
     slot, set `chN = 1` (one frame's data is constant).
   * Other cases: dispatch into per-channel jump table; sub-decoder
     consumes more bits and writes to the slot.
3. After all 9 channels, the slots are packed via `vpkd3d128` into
   half-floats and written sequentially to `_R16` — 8 frames worth
   of (qx, qy, qz, px, py, pz) per bone.
4. If `count_sel != 0`, the channels with `chN > 1` are
   "expanded" through `sub_82234790` to produce multiple frame
   entries (this is where the variable-length compression kicks
   in for animated channels).

Note the channel order — qx/qy/qz before px/py/pz. **w is
reconstructed**, not stored: at the end of decode the decoder
ensures `q.w = sqrt(1 - qx² - qy² - qz²)` (sign bit lives in one of
the count/header bits we haven't pinned down). Position has all 3
explicit channels.

## Clip-name resolution — where the friendly names actually live

The TOC's `key0` is an FNV-1 hash, but the **strings it hashes from
are not in the global TOC**. Per-character configuration files map
clip-keys to friendly names, and the runtime resolves them at
character-init time via `sub_82515540`'s call:

```
hash   = sub_821F3C28("Animations", FNV_INIT)   // = 0xF96D7984
config = sub_829FB978(character_blob, hash, 6)  // type-tag 6 = list
```

### locomotion.loco (mostly Hero Male)

The standalone file at `data/entity/locomotion.loco` is the **global
locomotion state machine for Hero Male**. Magic `0xF007DA7A`,
version 7. Layout:

```
+0x00  u32  magic = 0xF007DA7A
+0x04  u32  version = 7
+0x08  u32  stride_A = 656         (fixed for v7)
+0x0C  u32  stride_B = 400         (fixed for v7)
+0x10  u32  stride_C = 4           (fixed for v7)
+0x14  u32  count_A                (= 79 in retail)
+0x18  u32  count_B                (= 266)
+0x1C  u32  count_C                (= 8410)
+0x20  u32  size_strings           (= 3264 — bytes of trailing string table)
+0x24  u32  size_data              (= 195128 — total bytes after the 40-byte header)
+0x28  blob:
       A[count_A] structs of size 656    — character archetype slots
       B[count_B] structs of size 400    — state parameters
       C[count_C] u32                    — state transition / index list
       strings (size_strings bytes)      — null-terminated, packed
```

Each **A record** is a "character archetype slot" with:
- `A[i] +0` u32 hash — character-archetype id (different per slot)
- `A[i] +20` u32 — string offset into the strings table for the
  per-character `.anim` file **path** (e.g.
  `Art\Characters\Heros\Unclothed Male\Exports\rpt_HeroMale.anim`)
- (other fields hold pointers into the B array, fixed up at load
  time by `sub_8290BE28`)

Each **B record** holds floats — speeds, blend times, distances
(typical values like `1.0f`, `3.5f`, `0.25f`). String slots inside
B reference state names like `Pose`, `Idle`, `WalkFootstepTurn45L`,
`WalkFootstep180TurnInto`, etc. These are **state slot names, not
clip names** — the clip bound to each state varies per character.

The strings region (3264 bytes / 91 entries) holds:
- 1 anim-file path
- ~90 state slot names

So `locomotion.loco` is a **state machine + slot name table** — it
DOESN'T contain the clip-id → friendly-name binding we need.

### Per-character .anim files (where clip names actually live)

The 79 A records in locomotion.loco all (except A[68], which is
empty) reference `Art\Characters\Heros\Unclothed Male\Exports\rpt_HeroMale.anim`
or similar. **These `.anim` files live inside the BNK file system**
(`globals_models.bnk` / `streaming.bnk`) — they're not standalone
files in `data/entity/`.

Each character archetype (Hero Male, Troll, Hobbe, Crow, …) has its
own `.anim` file which contains:
1. A list of friendly clip names (`SE_HOBBE_FOOTSTEP_WALK`,
   `STANCE GOOFY`, …)
2. For each name: an FNV-1 hash that matches a key in the global
   TOC's `AnimRecord.key0`

That's the file we'd need to parse to populate the asset browser
with friendly names.

### Update — `.anim` files don't exist on disk or in any BNK (key finding)

After indexing every `.bnk` in the install (including `streaming.bnk`,
`levels.bnk`, `globals_streaming.bnk`, all 127 archives) plus the
flat data folder, **zero `.anim` files were found**. The path string
`Art\Characters\Heros\Unclothed Male\Exports\rpt_HeroMale.anim`
inside locomotion.loco's strings table refers to an authoring-time
asset that **isn't shipped as a separate file in the retail data**.

Possibilities:
1. The `.anim` content was baked into `.hkx` (Havok) files at build
   time. globals_streaming.bnk has 323 `.hkx` files in
   `Art\...\Combatant\` and similar paths. The animation graphs and
   binding tables live in there as serialised Havok objects.
2. The clip-name → key bindings were stripped from retail and exist
   only in the debug build / developer SDK.
3. The mapping is stored in a non-`.anim` extension we haven't yet
   identified.

### Update — clip key0 values have structured low bytes

When examining sample clip key0 values:

```
0x20C86000   0x56628001   0x9D9BE001   0x456B6002   0x10C16007
```

Low bytes: `00, 01, 01, 02, 07` — these are NOT random FNV-1 hashes.
The pattern is `(upper_24_bits) | low_8_bits_index`, where the
upper 24 bits are a hash of a parent archetype/group and the low
byte (or two) is a sub-index. This explains the structure:

- `key1` (4082 clips → 299 SpecialRecords): the upper bits hash the
  SpecialRecord and the low bits discriminate clips that share it.
- `key0` (4082 unique values): similar structure with a wider key
  space.

So **a direct FNV-1(name) lookup will never match clip keys** —
the keys are constructed from `(name_hash, sub_id)` pairs, where
the sub_id is assigned at content-build time. The mapping
`(name_string, sub_id) → key` is a custom encoding the engine
generates, not a pure hash.

Practically: even if we found a `.anim` file, recovering names
requires understanding HOW the keys are derived from names + sub-ids
— another reverse-engineering pass.

### Forward plan for clip-name resolution

1. **Index .anim files**: extend `S.all_anim_files` (analogous to
   `S.all_mdl_files`) so the asset scanner picks them up from BNK
   directories. Right now they're invisible to the UI.
2. **Reverse the .anim file format**. Likely magic `F0 ?? ?? ??`
   matching the locomotion.loco/MDL family, version-stamped, with
   a "Animations" section discoverable by FNV-1 hash.
3. **Build a global hash → name map** at root-load time by
   scanning every .anim file in the BNKs and merging their
   "Animations" sections.
4. **Override clip names** with a second pass over `S.anim_clips`
   right after the TOC parse: for each clip, look up `key0` in
   the merged hash→name map; if found, replace `id_HHHHHHHH` with
   the friendly name. Per-character disambiguation (same clip,
   different names in different `.anim` files) — pick the first
   one and append `…  (+N other names)` in the tooltip.

### IDA addresses for resume

| Address | What it is |
|---------|------------|
| `0x8290BE28` | locomotion.loco loader (sets `dword_83496B98..0`) |
| `0x8290BEAC` | The `lis r?, 0xF007` instruction loading the magic |
| `dword_83496B98` | base of A[] in memory after relocation |
| `dword_83496B9C` | count_A |
| `dword_83496BA0` | base of B[] |
| `dword_83496BA4` | count_B |
| `dword_83496BA8` | base of C[] |
| `dword_83496BAC` | count_C |
| `dword_83496BB0` | base of strings region |
| `0x829FB978` | section lookup `(blob, hash, type) → entry` |
| `0x821E55C8` | inner section walk |
| `0x821E5530` | binary search by hash |
| `0x8229CA50` | type-compatibility check (1, 2, 4, 5, 6, 7, 8) |
| `0x82515540` | character init: calls `lookup("Animations", 6)` |

## Useful IDA addresses

| Address    | What it is                                       |
|------------|--------------------------------------------------|
| 0x822F3B18 | App init — opens both files                      |
| 0x82A48B68 | AnimBank constructor (loads toc + data)          |
| 0x82B80D70 | TOC parser (full decompile available)            |
| 0x82B820A8 | Data file loader (allocs buffer + reads file)    |
| 0x82B82248 | Bank finalise — binds AnimRecord → data offsets  |
| 0x82B722E8 | Clip ctor (80-byte runtime object)               |
| 0x82B723E8 | Clip dtor                                        |
| 0x82B73880 | Animation-graph node debug printer (CTRL/TRANS/  |
|            | WEIGHTED/LAYER/MASK/DELTA/NETWORK)               |
| 0x820FA26C | Clip vtable (5 entries; mostly stubs — decode is |
|            | NOT virtual on this class, lives in Havok side)  |
| 0x82025740 | hkaWaveletSkeletalAnimationCompressionParams str |
| 0x82025e08 | hkaDeltaCompressedSkeletalAnimationQuantizationFormat |
| 0x82025840 | HK_INTERLEAVED_ANIMATION                         |
| 0x82025820 | HK_DELTA_COMPRESSED_ANIMATION                    |
| 0x82025800 | HK_WAVELET_COMPRESSED_ANIMATION                  |
| 0x832926a0 | hkaWaveletSkeletalAnimation type-info registrar  |
| 0x83292b30 | hkaDeltaCompressedSkeletalAnimation type-info reg|
| dword_8349F518 | AnimBank singleton (set by 0x82A48B68)       |
| dword_834BDCF0 | "AnimBank" factory entry (resource type id)  |
| 0x822E1DA8     | ★ DECODER — sample(blob, time, out, n, …)    |
| 0x822351F0     | sample wrapper (fps×time, bone remap)        |
| 0x82234A10     | 8-frame block interpolator                   |
| 0x82234790     | per-channel decoder (level 2-3)              |
| 0x82B6ECC8     | per-channel decoder (level 1)                |
| 0x822E23E4     | jump table — top-level mode switch           |
| 0x822E256C     | jump table — channel 1                       |
| 0x822E26E4     | jump table — channel 2                       |
| 0x822E285C     | jump table — channel 3                       |
| 0x822E29D4     | jump table — channel 4                       |
| 0x822E2B4C     | jump table — channel 5                       |
| 0x822E2CC4     | jump table — channel 6                       |
| 0x822E2E54     | jump table — channel 7                       |
| 0x822E2FDC     | jump table — channel 8                       |
| 0x822E3154     | jump table — channel 9                       |
| 0x822E32C0     | jump table — count-1 fast path               |
| dword_833703E0 | 1024-bucket decode cache (8-frame blocks)    |
| byte_8331460A  | cache enable flag                            |
| 0x82515540     | character init — sample at t=0 for bind pose |
