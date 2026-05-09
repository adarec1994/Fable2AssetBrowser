# Lionhead BC1 codec — full spec

Reverse-engineered from `default.xex` (sub_82B8C1C8 in the Fable 2 retail
binary). Implemented in [`src/LhTexCodec.cpp`](../src/LhTexCodec.cpp) as
`lh_decode_compressed_mip(...)`.

This is the codec for `CompFlag == 1` mips (BC1/DXT1). Other CompFlags
(2..10) call separate sub-functions (`sub_82B8D010`, `sub_82B8DFA0`) — those
are NOT yet ported.

## Body layout

```
u16  mip_w
u16  mip_h
u8   UnkData[440]      // packed Huffman code-length tables
u8   MipMapData[N]     // entropy-coded payload (N = DataSize - 444)
```

The decoder treats this as **one continuous bit-stream** starting at byte
0. There is no separate "header" — width, height, code lengths, and payload
are read sequentially from the same stream.

## Bit reader

- 32-bit words, **big-endian on disk** (Xbox 360 native).
- Within each word, bits read **LSB-first**.
- Word-boundary crossing is OR'd from the next word as `((hi << (32-off)) | (lo >> off)) & mask`.

In practice the encoder packs LSB-first into a u32 register, then writes
the u32 to memory; on a BE machine the LSB of the u32 ends up at the
**last** byte of the 4-byte group. So the byte at file offset N is the
"4th byte read" of bit-stream word `N >> 2`, not the "first".

## Frequency byte encoding

Each of the 440 UnkData bytes encodes a frequency for one Huffman symbol:

```
freq = (b & 0x0F) << ((b >> 4) & 0x0F)
```

High nibble = left-shift exponent, low nibble = magnitude. So:
- `0x00` → 0
- `0x09` → 9
- `0x1B` → 11 << 1 = 22
- `0x3B` → 11 << 3 = 88
- `0xFF` → 15 << 15 = 491520

A frequency of zero means the symbol still gets a leaf in the tree (the
binary builder always pushes all 256/62/122 leaves), it just has the
deepest code.

## Three Huffman trees

| Tree         | Alphabet size | UnkData byte range | Used for           |
|--------------|--------------:|--------------------|--------------------|
| `tree_idx`   | 256           | 0..255             | BC1 index bytes    |
| `tree_op`    | 62            | 256..317           | Per-block opcode   |
| `tree_delta` | 122           | 318..439           | RGB565 delta value |

### Heap behavior (the gotcha)

The binary's priority queue is a **min-heap that swaps on equal-frequency
parents** during sift-up — i.e. **newest-pushed wins on ties**. During
sift-down the comparison is `<` (not `<=`), so when both children are
equal, **left wins**. Standard `std::heapq` doesn't match either rule, so
the C++ port has its own `BinaryStyleHeap`.

If the trees are built wrong, decoding still mostly works for the most
common symbols but fails on rare ones — manifests as "implausible mip
dimensions" or "out-of-range op_idx" errors.

## Per-block decode

For each 4x4 block in raster order:

```
op_sym = huffman_decode(tree_op)
(op, dx, dy) = OP_TABLE[op_sym]    // OP_TABLE has 62 entries

if op == 4:
    c0 = read_bits(16)             // literal 16-bit BC1 endpoint
    c1 = read_bits(16)
else:
    // Reference a neighbor block. Out-of-bounds reads return c0=c1=0
    // (matches the C decoder reading directly from the pre-zeroed
    // output buffer).
    ref_x, ref_y = bx + dx, by + dy
    c0, c1 = output_block_at(ref_x, ref_y)

    if op in (1, 3): c0 = apply_delta(c0, DELTA_TABLE[huffman_decode(tree_delta)])
    if op in (2, 3): c1 = apply_delta(c1, DELTA_TABLE[huffman_decode(tree_delta)])
    if c0 < c1: swap(c0, c1)        // BC1 invariant after deltas

if c0 == c1:
    indices = 0  // solid block
else:
    B0..B3 = [huffman_decode(tree_idx) for _ in 4]   // 4 symbols
    // Each Bn is a 2x2 sub-block in Z-order:
    //   B0=top-left, B1=top-right, B2=bottom-left, B3=bottom-right
    output[4] = (B1 & 0xF0)        | ((B0 >> 4) & 0xF)
    output[5] = ((B1 & 0xF) << 4)  |  (B0 & 0xF)
    output[6] = (B3 & 0xF0)        | ((B2 >> 4) & 0xF)
    output[7] = ((B3 & 0xF) << 4)  |  (B2 & 0xF)

write_block(bx, by, c0, c1, indices)   // little-endian BC1
```

`apply_delta` on RGB565: split into 5/6/5, add signed deltas, clamp to
[0,31]/[0,63]/[0,31], repack.

## OP_TABLE @ 0x83316898 (62 × `(op, dx, dy)`)

Indices 0..21: op=0 (pure neighbor copy, no delta).
Indices 22..37: op=1 (copy + delta on c0).
Indices 38..53: op=2 (copy + delta on c1).
Indices 54..60: op=3 (copy + delta on both).
Index 61:       op=4 (literal 16-bit endpoints).

Concrete entries verified against the binary; full table is hard-coded in
[`src/LhTexCodec.cpp`](../src/LhTexCodec.cpp) `kOpTable`.

## DELTA_TABLE @ 0x833162E0 (122 × `(dr, dg, db)` signed)

Small RGB565 deltas, range roughly dr ∈ [-3, 3], dg ∈ [-5, 5], db ∈ [-3, 3].
Hard-coded in `kDeltaTable`.

## What this gets you

For PixelFormat=35 (BC1) textures with CompFlag=1 mips, you get the full
mip pyramid back as raw little-endian BC1 — feed straight into a standard
BC1→RGBA decoder. Every comp=1 .tex file in our set decodes with bit-stream
usage 99.9%+ (the few unused trailing bits are byte-padding).

---

# Lionhead BC4/BC5 codec — variant_2_3_4 (CompFlag = 2/3/4)

Reverse-engineered from `tex_decode_variant_2_3_4` (sub_82B8D010 in the
Fable 2 retail binary). **NOT YET PORTED to C++.** This section is the
spec a future session can implement against.

CompFlag = 3 covers PixelFormat = 40 (BC5/ATI2 normal maps) — every BC5
texture in our set uses comp=3 for big mips and falls through to comp=7
(raw) for the smallest. CompFlag = 2 and 4 use the same codec but were
not observed in our 15-file standalone set.

## Caller arguments

`tex_decode_variant_2_3_4(a1, a2, a3, a4, a5, a6, a7, ..., a32, a33, a34)`

| Arg  | Meaning                                                          |
|-----:|-------------------------------------------------------------------|
| `a1` | bit-stream pointer (input body)                                   |
| `a2` | output buffer base                                                |
| `a3` | output mode flag — 1 = packed-3bit indices, 2 = BC4-block, 3+ = tiled |
| `a4` | image width (in pixels) — height-in-blocks = a4 >> 2              |
| `a5` | image height (in pixels) — width-in-blocks = a5 >> 2              |
| `a6` | inner stride (used when `a3 > 2`)                                 |
| `a7` | outer stride                                                      |
| `a32`| pointer to a 4th huffman tree state struct (32-entry "op" tree)   |
| `a34`| huffman_build_tree's second arg (likely an arena/allocator)       |

For BC5, the dispatcher likely calls this function **twice** (once for X,
once for Y), each call writing one BC4 sub-block per BC5 block. Mode 2
(BC4-block output) is the layout that matches BC5's storage.

## Body layout (bit stream, LSB-first within 32-bit BE words)

```
1.  u16 mw                   // declared width  (sanity-only? — sometimes 0)
2.  u16 mh                   // declared height
3.  u4  v94                  // mode flag: 1 = read 4-bit values, else 8-bit
4.  u8  reserved             // skipped
5.  64 × u8  freq[A]         // tree A code-length bytes (global tree @ 0x834BE188)
6.  64 × u8  freq[B] (8 bits each)  // tree B (global @ 0x834BE1AC, 32 entries)
7.  64 × u8  freq[C] (8 bits each)  // tree C (global @ 0x834BE1D0, 64 entries)
8.  N  × u8  freq[OP] (8 bits each) // op tree (passed in via a32, 32 entries)
9.  ... entropy-coded payload
```

Wait — re-reading the disasm carefully, items 5–8 read **8 bits per
frequency byte** (same encoding as BC1: `freq = (b & 0xF) << (b >> 4)`)
for `(tree_size_bytes / 16)` entries, so:
- Tree A: 64 frequencies × 8 bits = 64 bytes
- Tree B: 32 frequencies × 8 bits = 32 bytes
- Tree C: 64 frequencies × 8 bits = 64 bytes
- Op tree: 32 frequencies × 8 bits = 32 bytes

Total preamble after the 16+16+4+8 header bits: 192 bytes of frequency
data, then payload.

The bit-reader is the same one as BC1 (`bit_read` @ 0x82B8BA00).

## Three global Huffman trees (built once at module init via sub_82B8BEA0)

| Tree object  | Address       | Entries | Role                          |
|--------------|---------------|--------:|-------------------------------|
| `tree_A`     | `0x834BE188`  | 64      | Endpoint values (op-type 3)   |
| `tree_B`     | `0x834BE1AC`  | 32      | Inner op (op-type 3 sub-step) |
| `tree_C`     | `0x834BE1D0`  | 64      | (purpose TBD — likely for delta on c1?) |

Plus a 4th tree passed in by the caller via `a32` — the **per-block op
tree**, 32 entries, symbols = `(op_type << 3) | count`.

## Per-block decode loop

The output is iterated as `block_y ∈ [0, mh/4)` (outer) and
`block_x ∈ [0, mw/4)` (inner). For each block:

```
if (run_remaining > 0) {
    --run_remaining;
    // continue previous op_type, no fresh decode
    goto emit;
}

op_sym = huff_decode(tree_op)   // one symbol from the 32-entry op tree
                                // tree entry struct: 16 bytes,
                                //   [0]=symbol-leaf? entry[8] = 16-bit child
                                //   [4..7]=16-bit value at depth = symbol << 4
op_type = op_sym_value >> 3;    // 0..3
count   = op_sym_value & 7;
extra   = read_bits(count);     // count is in [0,7]
run_remaining = (1 << count) + extra;

if (op_type == 2) {
    // Decode an extra value 'v20' — used as the BC4 endpoint
    if (v94 == 1) bits = read_bits(4);
    else          bits = read_bits(8);

    if (a3 == 1)  // packed-bits output: replicate nibble to byte
        v20 = (bits & 0xFF) | ((bits & 0xF) << 4);  // 8 bits low | nibble<<8 etc — see disasm
    else          // BC4-block output: dequantize 4-bit to 8-bit
        v20 = byte_83491F70[bits & 0xFF];   // 4→8 bit lookup table
}

emit:
// ... see "op-type emit modes" below
```

## Op-type emit modes

**Outer dispatch table** at `0x82B8D5E8` (4 entries × 4 bytes):

| op_type | Handler addr   | What it emits                                       |
|--------:|----------------|------------------------------------------------------|
| 0       | `0x82B8D5F8`   | zero-fill (all 0x00)                                |
| 1       | `0x82B8D650`   | full-fill (all 0xFF)                                |
| 2       | `0x82B8D6A8`   | constant-fill with `v20` (3 sub-modes per `a3`)     |
| 3       | `0x82B8D724`   | full BC4-style decode (endpoints + 4-symbol indices) |

### op_type 0 / 1 (constant fills)

```
if (a3 > 2) {
    // tiled mode: 4×4 stride writes
    base = a2 + (block_y * 4 + block_x) * 4;
    for (int j = 0; j < 4; j++)
        for (int i = 0; i < 4; i++)
            base[j*a7 + i*a6] = (op_type==0 ? 0x00 : 0xFF);
} else {
    // linear: 8 contiguous bytes
    for (int i = 0; i < 8; i++) out[i] = (op_type==0 ? 0x00 : 0xFF);
}
```

### op_type 2 (constant fill of `v20`)

```
if (a3 == 1) {
    // packed-bits: 8 copies of v20 contiguous
    for (int i = 0; i < 8; i++) out[i] = v20;
} else if (a3 == 2) {
    // BC4-block: a0=a1=v20, indices=0 (so all 16 pixels return v20)
    out[0] = v20; out[1] = v20;
    out[2..6] = 0;     // 5 of the 6 index bytes (out[7] left from buffer init = 0)
} else {  // a3 > 2: tiled
    // Same 4×4 stride loop as op0/op1, value = v20
}
```

### op_type 3 (the hard one)

This is a full BC4-style decode, ~250 instructions. Sketch:

1. Decode an endpoint symbol from `tree_A`:
   - Tree-walk: starting at the root entry, while `entry[0] != 0`, read 1
     bit and step to `entry[8 + 2*bit]` (2-byte u16 child slots).
   - At leaf: `slot = entry[8]` (u16), `entry_payload = *(u32*)(tree_A.values_base + slot*16)` (rotlwi by 4 = ×16)
   - From the payload: `count = payload & 7`, `signed_val = payload >> 3` (arithmetic shift, can be negative)
   - If count > 0: read `count` bits, add to `signed_val << count` (or similar reconstruction).
   - This gives `a0_raw` (signed, can be negative or > 0xFF).
2. Decode another endpoint from `tree_C` for `a1_raw` (same procedure).
3. Compute `a0 = a0_raw + a1_raw`, clamp to [0, 0x3F]; `a1 = a1_raw`, clamp to [0, 0x3F]
   (`bge cr6, loc_82B8D834; cmpwi cr6, r9, 0x3F` etc. — the 0x3F max
   suggests these are 6-bit values that get dequantized via
   `byte_83491F10` (6→8 lookup)).
4. `a0_byte = byte_83491F10[a0]`, `a1_byte = byte_83491F10[a1]`
5. Loop 4 times reading 4 symbols from `tree_B` (32-entry):
   - Each symbol → `op_inner = sym >> 3, count = sym & 7`
   - Read `count` bits → reconstruct a 4-bit-ish index value for that
     stage, stored in stack `var_AC[0..3]` (lower nibbles) and `var_AC+8`
     (upper nibbles) and similar for the second pass.
6. Output stage based on `a3`:
   - **a3 == 2** (BC4 layout — what BC5 wants): swap-and-pack.
     - First 8 bytes: `[a0_byte, a1_byte, idx0..idx5]` where idx is 16
       indices × 3 bits = 48 bits = 6 bytes, packed via the inner switch
       at `0x82B8DB3C` that does the `srawi`/`slw`/`or` bit-merge.
     - Then byte-swap the index bytes 2↔7, 3↔6, 4↔5 (the swap loop at
       `0x82B8DD68`–`0x82B8DDC8`) — this is the BE↔LE alpha bit-stream
       conversion.
   - **a3 == 1** (packed-3bit-indices): straight dump of indices, 4×4
     interpolated bytes → packed into 6 bytes (no header, no swap).
   - **a3 > 2** (tiled): for each of 16 pixel positions, compute the
     interpolated value via the inner op table at `0x82B8D9E0` (8 modes,
     including `(2*a0+a1)/3`, `(a0+2*a1)/3`, `(4*a0+1*a1+3)/7`, etc. —
     the BC4 alpha interpolation formulas), clamp to [0,255], scatter
     into `out[i*a6 + j*a7]`.

### Inner op table at `0x82B8D9E0` (8 entries, BC4-alpha-style interpolation modes)

| inner_op | Formula                          | (BC4 standard equivalent)        |
|---------:|----------------------------------|----------------------------------|
| 0        | `a0`                             | endpoint 0                       |
| 1        | `a1`                             | endpoint 1                       |
| 2        | `(3*a0 + a1*3) / 1`?             | needs verification               |
| 3        | `(4*a0 + 2*a1 + 3) / 7`          | BC4 6/7 a0 + 1/7 a1 mode         |
| 4        | `(3*a0 + 3*a1 + 3) / 7`          | (custom)                         |
| 5        | `(2*a0 + 4*a1 + 3) / 7`          | BC4 2/7 a0 + 5/7 a1              |
| 6        | `(a0 + 5*a1) / 1`?               | needs verification               |
| 7        | `(3*a1 + a0) / 1`?               | needs verification               |

(Exact formulas need careful re-derivation from the disasm — a few use
`divw r11, r11, r23` where r23=7, confirming BC4-style 1/7th
interpolations. The simpler ones use direct register copies.)

Result is then clamped via `bge .. li r11, 0x21 (zero-clamp)` and
`cmpwi 0xFF, ble .. li r27 (255-clamp)` before being stored.

A second dispatch at `0x82B8DDFC` (also 8 entries) repeats the same logic
for the `a3 == 1` packed-bits path.

## Static lookup tables (populated by `sub_82B8BEA0`)

Already documented in STATE.md but copied here for completeness:

| Address      | Purpose                                  | Used by         |
|--------------|------------------------------------------|-----------------|
| `0x83491F70` | 4-bit → 8-bit dequantize                 | op_type 2 v20   |
| `0x83491F10` | 6-bit → 8-bit dequantize                 | op_type 3 a0/a1 |
| `0x83491F50` | 5-bit → 8-bit dequantize                 | (BC1 reuses)    |
| `0x83491E10` | 8-bit → 4-bit quantize                   | op_type 2 a3=1  |
| `0x83491D10` | 8-bit → 5-bit quantize                   | (BC1 reuses)    |
| `0x83491C10` | 8-bit → 6-bit quantize                   | (TBD)           |

## Estimated effort to port

~1-2 days of focused work. The hard parts:
- Op_type 3 endpoint reconstruction needs careful bit-level matching.
- The pack-and-swap step for `a3==2` output needs to produce byte-perfect
  BC4 alpha blocks that round-trip through a standard BC4 decoder.
- The Huffman tree-walk uses a non-standard 16-byte entry layout
  (`entry[0]` = is-leaf flag, `entry[4]` = 16-bit symbol value at offset
  `(__ROL4__(sym, 4))` from `tree.values_base`, `entry[8]/entry[10]` =
  child entry offsets) — all three "global" trees plus the op tree share
  this layout, distinct from the 256-entry index tree the BC1 codec uses
  but built by the same `huffman_build_tree` (sub_82B8BC08) routine.

## Verification plan

1. Pick one of our 6 BC5 test files (e.g. `bs_brick_english bond_norm.tex`).
2. After the port, decode the largest comp=3 mip → BC5 blocks.
3. Run those through `blit_bc5_to_rgba` (already in ModelPreview.cpp).
4. Compare against the comp=7 (smallest) mip, upscaled. Should agree on
   the LOW frequencies; high-frequency content can only be checked
   visually against the model.
