# Fable 2 .tex File Format

## File layout (extracted .tex = vh + vm + vb concatenated)

The file you see in `extracted/` is `globals_texture_headers.bnk` content
(`vh`) followed by optional `1024mip0_textures.bnk` content (`vm`) followed
by `globals_textures.bnk` content (`vb`). All three were already
zlib-decompressed by the BNK reader.

The whole concatenated file is a **chain of mip records** — there is **no
top-level header**. The 010 template the project originally had described
just a single record's structure but the parser was treating the first
record's fields as if they were a global header; that's why the original
parser was brute-forcing offset tables.

## Mip record (12 uint32 + body, all big-endian on disk)

```
struct MipRecord {
    uint32 CompFlag;     // 0..11 dispatched (12+ traps); 24 observed but
                         // filtered upstream and never reaches the codec
    uint32 DataOffset;   // always 48 — body starts immediately after the
                         // 12-uint32 header
    uint32 DataSize;     // size of body that follows
    uint32 u3;           // 0 normally; 3 = "this is a half of a BC3 mip"
                         // OR "separate mip0 chunk follows" (file-level)
    uint32 u4;           // when u3==3 at file level: absolute offset of
                         //                            separate mip0 chunk
    uint32 u5;           // when u3==3 at file level: size of that chunk
    uint32 u6..u11;      // observed all zero in normal mips
    // body[DataSize]:
    if (CompFlag == 7) {
        ubyte raw_dxt[DataSize];        // raw BC1/BC3/BC5
    } else {
        uint16 mip_w;
        uint16 mip_h;
        ubyte UnkData[440];
        ubyte MipMapData[DataSize - 444];
    }
};
```

Field naming in the codebase: `TexInfo::MipDef` mirrors this. Important:
`MipDataOffset` in `TexInfo::MipDef` points **after** the 4-byte w/h +
440-byte UnkData (i.e. at just the entropy-coded payload). The codec wants
the **whole** body — reconstruct via `m.DefOffset + 48` with size
`m.DataSize`.

## Per-mip CompFlag distribution (323 .tex files in our set)

| CompFlag | Count | Role                                                |
|---------:|------:|-----------------------------------------------------|
| 1        | 761   | Lionhead BC1-style codec — **DONE** (sub_82B8C1C8)  |
| 7        | 393   | Raw BC1/BC3 bytes, big-endian on disk — DONE        |
| 11       | 81    | Open — falls through `tex_decode_mip` switch        |
| 3        | 16    | Variant — calls `tex_decode_variant_2_3_4`          |
| 24       | 1     | Filtered upstream, never decoded                    |

## Pixel-format codes (TexInfo.PixelFormat field, file-level)

| Code | Meaning                       |
|-----:|-------------------------------|
| 35   | BC1 / DXT1 (verified)         |
| 39   | BC3 / DXT5 (verified)         |
| 40   | BC5 / ATI2 normals (verified) |

## Dispatcher map (function `tex_decode_mip` @ 0x82B8E320)

```
if (CompFlag >= 12)                              -> trap (filtered upstream)
if (CompFlag == 7)                               -> raw block copy
                                                    (PixelFormat 0/12 = 8 b/blk,
                                                     PixelFormat 1/2/3 = 16 b/blk,
                                                     PixelFormat 4 / 10 = special raw)

PixelFormat 0/12 (BC1, single sub-block):
  switch CompFlag {
    case 1: tex_decode_BC1_compressed   <- ported as lh_decode_compressed_mip
    // (no other cases observed)
  }

PixelFormat 1/2/3 (BC3, TWO sub-blocks per mip — see "BC3 dual-record" below):
  iterate v69 = 0, 1:
    v72 = &a1[3 * v69]                  // a1[0..2] for v69=0, a1[3..5] for v69=1
    switch *v72 {
      case 1: tex_decode_BC1_compressed(a1 + v72[1], output_slot, ...)
    }

PixelFormat 4 (BC5/ATI2-normals, single iteration):
  switch CompFlag (= a1[0]) {
    case 2,3,4: tex_decode_variant_2_3_4   (sub_82B8D010)  <- TODO
    case 5:     tex_decode_variant_5       (sub_82B8DFA0)  <- TODO
    case 6:     constant fill: output[i] = a1[1]
    case 8,9,10: predictor: output[i] = output[i + (8 - CompFlag)*stride]
  }
```

## BC3 dual-record (still hypothesis, needs verification with a real BC3 .tex)

For BC3 textures, ONE logical mip is stored as ONE 12-uint32 header that
contains TWO sub-headers stacked:

```
offsets in the 48-byte mip header:
  0..11  : (CompFlag_color, DataOffset_color=48, DataSize_color)
  12..23 : (CompFlag_alpha, DataOffset_alpha,   DataSize_alpha)
  24..47 : zero / unused
```

So a BC3 mip body has TWO bodies inside one record. The dispatcher iterates
twice, one per sub-block. The current `parse_tex_info` does not split BC3
records into sub-blocks; it treats them as a single record. This is why
many "comp=11" / "comp=3" entries the user saw in the chain may actually be
the alpha sub-block's CompFlag (= `a1[3]` of the BC3 record).

## Codec algorithm (the part that's done — CompFlag = 1)

See `CODEC.md` in the same folder for full spec.

Summary: 3 Huffman trees of sizes 256 / 62 / 122. Three packed code-length
tables in the 440-byte UnkData (each byte = `(b & 0xF) << (b >> 4)` →
frequency). Per-block decoder picks an op (62-entry tree) that says "copy
endpoints from neighbor at (dx, dy), optionally apply RGB delta from the
122-entry tree". Indices are 4 symbols from the 256-entry tree, then
nibble-transposed in Z-order:

```
output[4] = (B1 & 0xF0)        | ((B0 >> 4) & 0xF)   // top row
output[5] = ((B1 & 0xF) << 4)  |  (B0 & 0xF)         // 2nd row
output[6] = (B3 & 0xF0)        | ((B2 >> 4) & 0xF)   // 3rd row
output[7] = ((B3 & 0xF) << 4)  |  (B2 & 0xF)         // bottom row
```

The 4 symbols B0..B3 = 2x2 sub-blocks in Z-order (top-left, top-right,
bottom-left, bottom-right).
