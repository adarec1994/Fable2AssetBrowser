# Project state — handoff notes

Read this if you're a new Claude Code session picking up this work.

## Done

- **`.tex` chained-mip parser** ([`src/TexParser.cpp`](../src/TexParser.cpp))
  walks the record chain, pulls out `MipDef` per record. Note: it does
  NOT split BC3 records into 2 sub-blocks — see "Open issue: BC3" below.
- **Lionhead BC1 codec** ([`src/LhTexCodec.cpp`](../src/LhTexCodec.cpp))
  decodes `CompFlag == 1` mips into raw little-endian BC1. Verified
  visually on `beetle_basic.tex` mips 0-3.
- **Texture window** (double-click a .tex in the file list) and
  **hex-view auto-preview** both pick the largest mip and route through
  the codec. Errors go to `tex_errors.log` next to the exe.
- **Model preview** (`MP_Render` in
  [`src/UI/ModelPreview.cpp`](../src/UI/ModelPreview.cpp)) loads each
  mesh's diffuse texture via the same path. The preview popup has a
  "Textures" panel on the right with checkboxes to hide/show each
  diffuse on the model.

## Open

### CompFlag = 11 (~81 mips in the BNK-extracted set)

Not present in the standalone `tex files/` set we have for testing —
appears only in BNK-extracted textures. Doesn't match any case in
`tex_decode_mip`'s switch (which handles 1, 2, 3, 4, 5, 6, 8, 9, 10
inside the BC4/BC5 path; 1 inside the BC1/BC3 path). Two leading
possibilities:

1. **Wrong parse of BC3 dual-record.** A BC3 mip is actually ONE
   12-uint32 header that bundles TWO `(CompFlag, DataOffset, DataSize)`
   sub-headers (color + alpha). The current parser treats each as a
   separate record. The "second record's CompFlag" the parser sees
   could actually be `a1[3]` of a BC3 record — which IS used as a
   second CompFlag by the dispatcher. Need to confirm with a real
   PixelFormat=39 sample.
2. **Different pre-dispatcher.** Some other code path the runtime takes
   for non-2D textures (cubes, volumes) — see `tex_dispatch_cube` at
   `0x82B8EE58`.

Test: when you can extract BNK textures again, look for one with
PixelFormat=39 + comp=11 and check whether `u3` of the previous record
is non-zero.

### Static lookup tables (used by both BC1 and BC5 codecs)

`sub_82B8BEA0` (called once at first decode) populates these:

| Address      | Size | Purpose                                |
|--------------|-----:|----------------------------------------|
| `0x83491F70` | 16   | 4-bit → 8-bit dequantize  (× 17.0)    |
| `0x83491F50` | 32   | 5-bit → 8-bit dequantize  (× 8.2258)  |
| `0x83491F10` | 64   | 6-bit → 8-bit dequantize  (× 4.0476)  |
| `0x83491E10` | 256  | 8-bit → 4-bit quantize    (× 15/255)  |
| `0x83491D10` | 256  | 8-bit → 5-bit quantize    (× 31/255)  |
| `0x83491C10` | 256  | 8-bit → 6-bit quantize    (× 63/255)  |

All use round-to-nearest with clamp. We've already seen `byte_83491C10`
and `byte_83491D10` in the BC1 codec (referenced as `v43`/`v44` in the
disassembly when extracting RGB565 channels). The 4-bit pair is needed
by the BC5 variant codec which apparently does some 4-bit quantization
of alpha endpoints/indices.

### CompFlag = 3 (BC5 normal-map encoding)

**Confirmed**: PixelFormat=40 (BC5/ATI2) uses CompFlag=3 throughout.
Of the 15 standalone tex files we have, 6 are BC5 normal maps, all
using comp=3 for mips 0..N-1 and comp=7 for the smallest mip. The big
mips (comp=3) still need the variant_2_3_4 codec to be ported, but the
**comp=7 raw fallback is now wired up** — BC5 textures display their
smallest mip via `blit_bc5_to_rgba` (R=X, G=Y, B=reconstructed Z) so
normal maps no longer fail outright. See `decode_bc4_block`,
`swap_bc5_endian`, `blit_bc5_to_rgba` in
[`src/UI/ModelPreview.cpp`](../src/UI/ModelPreview.cpp).

Handled by `tex_decode_variant_2_3_4` (sub_82B8D010). Algorithm (from
disassembly, partial understanding):

1. Read header from bit-stream:
   - 16 bits `mw`
   - 16 bits `mh`
   - 4 bits mode flag (called `v94` in decompile; selects 4-bit vs 8-bit
     read path later, controls a remap step)
   - 8 bits unused / reserved

2. Build 4 Huffman trees with `(b & 0xF) << (b >> 4)` frequency encoding:
   - `unk_834BE188` (64-entry global)
   - `unk_834BE1AC` (32-entry global)
   - `unk_834BE1D0` (64-entry global)
   - `a32` parameter (32-entry, passed in by caller)

3. Per-block (4x4 pixels) outputs an 8-byte BC4-style block (or
   16 bytes for BC5 = 2 channels). Decode an op_symbol from the `a32`
   tree, split into `op_type = sym >> 3` and `count = sym & 7`. Read
   `count` more bits to assemble a value `v69 = (1 << count) + extra`.
   Then switch on op_type:
   - **0**: write 8 zero bytes (or 4×4 stride-spread zeros for tiled mode)
   - **1**: read 1 bit, decode value via `unk_834BE188` huffman or via
     `byte_83491E10` lookup (if `v94 != 1`); fill 8 bytes with that value
   - **2**: similar lookup but reads 4 or 8 bits (depending on `v94`),
     uses `byte_83491F70` lookup; outputs an alpha-block-shaped pattern
     (2 endpoint bytes + 6 index bytes that interpolate)
   - **3..N**: more variants (alpha endpoints + index byte rolls)

4. Op_type encoding bits seem to mirror a BC4 alpha-block structure:
   - 2 endpoint bytes (a0, a1)
   - 6 bytes = 16 indices × 3 bits

The lookup tables `byte_83491E10` (256 bytes) and `byte_83491F70`
(another 256 bytes) at `0x83491E10` / `0x83491F70` are static remap
tables — likely "encode 4-bit alpha indices to canonical 3-bit BC4
values" or similar. Need to dump them and analyze.

**Status**: NOT ported. Disassembly is heavy (~993 instructions, fully
mapped now — see CODEC.md "variant_2_3_4" section for the complete
spec including the outer op-type dispatch table @ 0x82B8D5E8 (4 entries),
the inner BC4-interpolation table @ 0x82B8D9E0 (8 entries), and the
secondary dispatch @ 0x82B8DDFC). Uses ~10 stack temporaries, multiple
decision points per block. Estimate ~1-2 days of focused work to port
faithfully — but the spec is now complete enough that porting is
mechanical rather than exploratory.

### CompFlag = 5

Handled by `tex_decode_variant_5` (sub_82B8DFA0). Not yet decompiled
in detail. Same procedure as comp=3 when we get to it. Not needed for
any of the 15 test textures (none of them use comp=5).

### CompFlag = 6, 8, 9, 10

Trivial — decompiled inline in `tex_decode_mip`:

- `case 6`: constant fill, `output[i] = a1[1]` (= the byte stored in the
  DataOffset field for this sub-block).
- `case 8/9/10`: predictor (delta-from-neighbor without entropy coding).
  `output[i] = output[i + (8 - CompFlag) * stride]`. Used inside BC4/BC5
  flow.

These can probably be ported in <50 lines once the BC4/BC5 framing
around them is understood.

### CompFlag = 24 (1 mip)

Above the trap (`>= 12`). Probably never reaches the codec. Likely
filtered by the loader as "skip this texture" or pulled from a different
path. Lowest priority.

## Open issue: BC3 dual-record parse

Hypothesis from the dispatcher disassembly: a BC3 mip is encoded as ONE
12-uint32 mip header carrying TWO `(CompFlag, DataOffset, DataSize)`
sub-headers (one for color, one for alpha), with two separate bodies. The
current parser treats every 12-uint32 header + body as one record — fine
for BC1 (PixelFormat=35) but means BC3 mips get split into two records
where the second record's "CompFlag" is actually the alpha sub-block's
CompFlag.

To verify: pick a BC3 .tex (PixelFormat = 39) and check whether (a) the
first record's `u3` field is non-zero, and (b) the next record looks like
a continuation rather than a smaller mip.

Fix would be: when `PixelFormat == 39`, parse each record as TWO
sub-records.

## Where to look in IDA

The IDA database is at:
```
C:\Users\pwd12\OneDrive\Documents\X360\fable 2\Fable II (USA, Europe) (En,Zh,Ko,Pl,Cs,Hu,Sk) (Rev 2)\default.xex.i64
```

Already-renamed key functions:

| Address      | Name                              |
|--------------|-----------------------------------|
| `0x82B8E320` | `tex_decode_mip` (top dispatcher) |
| `0x82B8C1C8` | `tex_decode_BC1_compressed` (ported) |
| `0x82B8D010` | `tex_decode_variant_2_3_4` (TODO)  |
| `0x82B8DFA0` | `tex_decode_variant_5` (TODO)      |
| `0x82B8BA00` | `bit_read`                        |
| `0x82B8BC08` | `huffman_build_tree`              |
| `0x82B8EBD0` | `tex_codec_state_ctor`            |
| `0x83316898` | `g_op_table` (62 × 12 bytes)      |
| `0x833162E0` | `g_delta_table` (122 × 12 bytes)  |

## Useful test artifacts

- `source/Archive/` has the older Python prototype (`bnk_reader.py`,
  `convert.py`).
- The `tex files/` folder at the repo root has 15 sample .tex files for
  ad-hoc testing.
- The previous `cmake-build-debug/extracted/` had ~323 textures dumped;
  re-create it via the app's "extract all" if needed.
- Python reference decoder for comp=1 was in
  `cmake-build-debug/tex_decode.py` (now gone with the build dir — see
  CODEC.md for the algorithm).

## How to continue

### Quick path: BC5 (CompFlag=3)

The full spec is now in [`CODEC.md`](./CODEC.md) under "variant_2_3_4".
What's left is purely a port (no more reverse-engineering needed):

1. Add `lh_decode_variant_2_3_4(body, body_size, mode, w, h, op_tree, out_buf, err)`
   to `LhTexCodec.cpp`. Body parse: u16 mw, u16 mh, u4 v94, u8 reserved,
   then 4 frequency tables (64+32+64+32 = 192 bytes), then payload.
2. Build 4 huffman trees (3 globals + caller-provided op tree) using the
   same builder as BC1 (`huffman_build_tree`). Note: the variant trees
   use a 16-byte entry layout, not the 4-byte one BC1 uses — see CODEC.md.
3. Per-block: decode op_sym from op tree, split into op_type/count,
   read `count` extra bits, then dispatch on op_type via the 4-entry
   table:
   - **0**: zero block
   - **1**: solid 0xFF block
   - **2**: constant `v20` fill (3 sub-modes per `mode` arg)
   - **3**: full BC4 decode — endpoints from trees A & C, indices from
     4 reads of tree B, then pack via the inner 8-entry interpolation
     table at 0x82B8D9E0
4. For BC5 textures (PixelFormat=40) the dispatcher likely calls this
   function twice per BC5 block (X then Y) — confirm via the BC5 caller
   in `tex_decode_mip`. The function writes BC4 alpha-block format
   when called with mode=2.
5. Run output BC4 blocks through the existing `decode_bc4_block` /
   `blit_bc5_to_rgba` helpers in ModelPreview.cpp.

### Quick path: CompFlag = 11

Need to:
1. Re-populate `extracted/` from BNK files via the app
2. Find a comp=11 sample and inspect its raw bytes
3. Check whether the previous record's `u3` field is 3 (suggesting BC3
   dual-record bundling)
4. If yes: fix `parse_tex_info` to split BC3 records into two
   sub-records and re-classify

### General approach

In IDA, decompile `tex_decode_variant_2_3_4` and `tex_decode_variant_5`
fully — write the same kind of spec we did for `tex_decode_BC1_compressed`
(see CODEC.md as template).

Then add new entry points to `LhTexCodec.cpp`:
- `lh_decode_variant_2_3_4(...)`
- `lh_decode_variant_5(...)`

In `decode_tex_to_rgba` (`src/UI/ModelPreview.cpp`), add a switch on
CompFlag that routes to the correct codec. Preserve the existing BC1
path for `CompFlag == 1`.

## Build

Project uses CMake + nmake (32-bit MSVC). The exe lives at
`source/cmake-build-debug/Fable_2_Asset_Browser.exe`. Errors at runtime
are written to `tex_errors.log` next to the exe.
