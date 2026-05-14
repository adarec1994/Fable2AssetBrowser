# Fable 2 Level Format Notes

Working notes from the `defaultscenario` sample (Bloodstone). The end goal is
loading the terrain with its textures and props. This document captures what is
known after the first IDA pass; everything is incremental and is meant to be
extended as more pieces fall into place.

## 1. Filesystem inventory

All paths are relative to a level directory. Sizes are from the Bloodstone
default scenario; structure repeats for every level.

| File | Magic (first 4 bytes / ASCII) | Size | Role |
|------|--------------------------------|------|------|
| `level.vfsconfig` | XML `<VFSConfig ID="level">` | 1.9 KB | VFS / BNK composition manifest. Lists which `.bnk` files supply textures + models for this level. |
| `defaultscenario.list` | ASCII path list | 1.1 KB | Plain-text inventory of all loose files the level needs. |
| `defaultscenario.engine_data` | `EngineResourceList\0` | 84 KB | Engine-side resource registry. Read by `level_parse_EngineResourceList_ctor` @ `0x83285840`. |
| `defaultscenario.engine_level` | `LevelGraphicsFile\0` | 525 KB | **Master graphics index** for the level. Read by `level_parse_LevelGraphicsFile` @ `0x82AAAC20`. |
| `defaultscenario.gdb` | `GDB\0` | 744 KB | Game database (entities/quest/script state). Format TBD. |
| `defaultscenario.havok_scenario` | `0x57E0E057 0x10C0C010` (HK_PACKFILE) | 4.7 MB | Standard Havok 6.5-era physics packfile (collision world + per-prop shapes). |
| `defaultscenario.lmp` | gzip → `LightmapFile\0` | 4.4 MB / 7.0 MB raw | Lightmap pack. Read by `level_parse_LightmapFile` @ `0x82A4AF78`, dispatched from `level_dispatch_lmp_load` @ `0x82AAAA18`. |
| `defaultscenario.save` | `<?xml version="1.0"?><Level>` | 239 KB | XML save-state for the level (default scenario layout). |
| `defaultscenario.texture_atlas` | `0xFFFFFFFE` | 21.3 MB | Custom `.tex` container — same magic as the loose `.tex` files we already decode. Holds atlas pages used by the terrain shaders. |
| `lightprobesdata.dat` | BE floats / probe records | 978 KB | Per-level lightprobe samples (irradiance volume). |
| `new_heightfield_..._id_874c5ee2.ghf` | gzip → BE-float blob | 1.5 MB / 6.9 MB raw | **Terrain heightfield raw data.** Tight binary, no string magic. Big-endian. |
| `new_heightfield_..._id_874c5ee2.ehf` | `HeightFieldGraphicsFile\0` v18 | 3.2 MB | **Terrain graphics descriptor** (LOD meshes / texture-blend references). |
| `new_heightfield_..._id_874c5ee2.genv` | BE floats | 30.7 KB | Graphics environment data (probably terrain-specific lighting/material params). |
| `new_heightfield_..._id_874c5ee2.hdb` | BE floats | 31.4 KB | Heightfield database (terrain-prop placements?). |
| `new_heightfield_..._id_874c5ee2.ama` | `ADMP\0\0\0\x03` | 8.9 KB | Ambient/decal map. 8888 / 192 / 48 floats in header. Format-version 3. |
| `new_heightfield_..._id_874c5ee2.amm` | `ADMP\0\0\0\x03` | 108 B | Same `ADMP` magic — tiny variant of above. Probably "metadata" companion. |
| `new_heightfield_..._id_874c5ee2.amr` | `ADMP\0\0\0\x03` | 40 B | Same `ADMP` magic — even smaller, single record. |
| `new_heightfield_..._id_874c5ee2.mist` | 8 bytes | 8 B | Mist parameters (only 1 int + 1 zero). |
| `new_heightfield_..._id_874c5ee2.water` | BE u32s + floats | 19 KB | Water patch spawn data (height, plane equations). |

A gzip header (`1F 8B 08 08`) on `.ghf` / `.lmp` decompresses to a payload
whose first 32 bytes after the gzip metadata is the actual file content
(see `.lmp` decompresses to `LightmapFile\0\0\0\x0c…`).

## 2. `level.vfsconfig` — BNK composition

Quoted excerpt (whitespace trimmed):

```xml
<VFSConfig ID="level">
  <Group ID="level_textures">
    <Bank ID="level_texture_headers"
          Path="…\defaultscenario_texture_headers.bnk" Mode="memory"/>
    <Composite>
      <Required><Ref ID="level_texture_headers"/></Required>
      <Optional><Ref ID="1024mip0_textures"/></Optional>
      <Required><Bank Path="…\shared\shared_3706.bnk" Mode="memory"/></Required>
    </Composite>
    <Composite> … shared_2545.bnk … </Composite>
    <Composite> … defaultscenario\textures.bnk … </Composite>
  </Group>

  <Composite>      <!-- level-specific models -->
    <Required><Ref ID="globals_model_headers"/></Required>
    <Required><Bank Path="…\defaultscenario_models.bnk" Mode="disk"/></Required>
  </Composite>
  <Group ID="level_preloadables"> … defaultscenario_models.bnk (memory) … </Group>
  <Bank Path="…\defaultscenario_streaming.bnk" Mode="memory"/>
</VFSConfig>
```

Takeaways:

- Textures used by this level live in **three composite stacks**, each composed
  of `defaultscenario_texture_headers.bnk` + the optional `1024mip0` mip BNK +
  one of three body BNKs (`shared_3706`, `shared_2545`, or the level's own
  `textures.bnk`).
- Models come from `globals_model_headers.bnk` + `defaultscenario_models.bnk`
  (loaded twice: once `Mode="disk"` for streaming, once `Mode="memory"` for
  preload).
- `defaultscenario_streaming.bnk` is referenced loose at the bottom — that's
  the streaming-residency tracker.

These BNKs aren't in this dump folder; they live alongside the rest of the
game's `.bnk` set (`worlds\albion\bloodstone\defaultscenario\…`). Loading the
level for real means resolving these paths through the same `BnkCache` path
that `build_any_tex_buffer_for_name` already uses.

## 3. `LevelGraphicsFile` (`defaultscenario.engine_level`)

Parser: **`level_parse_LevelGraphicsFile` @ `0x82AAAC20`** (renamed).

### Header

```c
struct LevelGraphicsFileHeader {
    char     magic[17];      // "LevelGraphicsFile" (no null in stream)
    uint32_t version;        // BE; valid 11 or 12
    uint32_t entry_count;    // BE
};
```

The decompile reads the magic via `stream_read_string_fixed(buf, stream, 17)`
and rejects anything that fails `string_compare(buf, "LevelGraphicsFile")`.
Version mismatch produces the literal error string "Unsupported engine level
version <N> for file + <path>. Should be at least 11 and at most 12."

### Entries

After the header the file is a flat list of `entry_count` typed records.
Each entry begins with a `uint32_t type` and is dispatched through a
`switch`:

| Type | Handler (renamed) | What it does |
|------|---------------------|---------------|
| `2`     | `lgf_handle_type2` @ `0x82AA9A28` | **Instance placements.** Reads 4 strings + per-instance records (8-byte hash, three 1-byte flags, four 16-byte vector lanes, ten floats — that's a packed transform + LOD bounds). Each record is published via `sub_821E1B00` (the generic "register typed object" sink). |
| `4`     | inline in parser | String + 8 bytes of payload; pushed via `sub_821E1B00`. |
| `5`     | `lgf_handle_type5_string` @ `0x82AA9548` | Opens the named subfile (`sub_82C63FB8(name, mode=1, flags=2)`), reads two u32s (hash + count), publishes via `sub_82A87D88`. Looks like *texture composite registration*. |
| `0x15` (21) | `lgf_handle_type15_section` @ `0x82AAA530` | Reads 2 strings + 8-byte hash + 2 flag bytes → object via `sub_82B1AD00`, registered through `sub_821E1B00`. Receives the file version as `a3`. |
| `0x20` (32) | `lgf_handle_type20_string` @ `0x82AA9728` | Opens the named subfile, reads `uint32 count`, then `count` × `(u32, u32)` pairs (hash pairs), publishing one 16-byte object per pair (vtable `off_820F5E50`). Reads like a *streaming index* or *atlas-page list*. |

The Bloodstone scenario file begins with a type-4 entry pointing at the path
`worlds\…` (the level's own asset prefix), so the master index of every other
file you see in the directory is enumerated here.

## 4. `LightmapFile` (`defaultscenario.lmp` decompressed)

Parser: **`level_parse_LightmapFile` @ `0x82A4AF78`** (renamed). Dispatched
from `level_dispatch_lmp_load` @ `0x82AAAA18`, which composes the path
`<base>.lmp`, opens it, and calls the parser.

### Header

```c
struct LightmapFileHeader {
    char     magic[12];      // "LightmapFile"
    uint32_t version;        // BE; valid 10..12
    uint32_t v12_extra;      // only when version >= 12 (stored at level+240)
    uint32_t count_A;        // texture / atlas references
};
```

### Sections (in order)

1. **Texture references** — `count_A` × { u64 hash, u32 ?, u32 ?, u32 strlen, char[strlen] path }.
   Each record creates a wrapper via `sub_82A46D90` and registers it at
   `level + 28`.
2. **Per-area data** — `count_B` × records keyed by u64 hash. A 1-byte flag
   selects between two encodings: an external blob (recurses via
   `sub_82A4AE70`) or an inline data array (`count_D` × u32). Registered at
   `level + 16` or `level + 4`.
3. **Shadowmaps** — `count_C` × { u64 hash, u32 count, u32 array }. Registered
   at `level + 40`.
4. **(version ≥ 11) Probes** — `count_D` × 64-byte fixed records. Registered
   at `level + 52`.

The "level" structure (parser's `a1`) holds the lightmap and probe registries
at the fixed offsets above. Anything that needs to sample a lightmap looks up
the entity hash in one of these maps.

## 5. Primitive-instance enum

Built up in `primitive_instance_enum_register` @ `0x82AA7208`. Index 6 is
`PRIMITIVE_INSTANCE_HEIGHTFIELD` — that's the renderable identity terrain
chunks register themselves under. Full table (0..47) is in the decompile;
notable ones for the terrain pipeline:

- `2` STATIC_MESH
- `3` TREE_MESH / `41`–`43` HIGH/LOW/SHADOW TREE_MESH (foliage variants)
- `4` REPEATED_MESH
- `6` HEIGHTFIELD ← **terrain**
- `7` WATERPATCH
- `27` MIST
- `33` PORTAL
- `38` CUSTOM_ATLAS_TEXTURE
- `46` PRELOAD_RESOURCE
- `47` OCEANWATER

## 6. `HeightFieldGraphicsFile` (`.ehf`)

Magic string at `0x820F677C`. No direct function xrefs — the string is
referenced *only* from a type-info / class-descriptor at `0x83317E90`
(adjacent floats: `0x3E4CCCCD` = 0.2 and several `1.0` / `0.0` values, plus a
small u16 index buffer at `0x83317E40` that looks like a fixed mesh-index
table).

That class-descriptor is the dispatch hook: the engine looks resources up by
type-tag, finds this descriptor, and calls a virtual `parse(stream)` method.

### Layout (empirical, from the Bloodstone sample)

```
0x00..0x16   "HeightFieldGraphicsFile" (23 ASCII, no NUL)
0x17..0x1A   u32 BE version          (= 18 in our sample)
0x1B..0x1E   f32 BE prefix_float     (= 64.0 — same world-scale constant
                                       as .ghf / .genv / .hdb)
0x1F..       a stream of TYPED SECTIONS, each beginning with:
               +0   u32 marker / Sign (0xFFFFFFFE on most)
               +4   u32 RawDataSize  (length of body that follows the
                                      header)
               +0x10..+0x14   u32 W, u32 H (= 641, 769 — terrain grid)
               +0x18          u32 PixelFormat-like tag
               ... padding ...
               +0x70..+0x74   u32 raw_size  (uncompressed bytes)
               +0x74..+0x78   u32 comp_size (deflate stream length)
               +0x78..        zlib stream (78 DA ...)
```

So each section is essentially **a size-prefixed deflate-compressed blob**
with metadata declaring W×H + format.  Same compression family that the
`.texture_atlas` uses, just embedded inside the heightfield-graphics file
instead of in a separate texture container.

In our sample:

| Section | Offset | raw_size | inflated content (likely) |
|---|---|---:|---|
| #1 | `0x1F` | 1,077,248 | u16 grid — low-precision height LOD or per-cell index pyramid (538,624 records — ~1.09 × terrain cells) |
| #2 | `0x9E5E2` | 688,128 | `PF=40` (BC5) at 1024×672 — terrain normal map OR splat / blend map |
| #3 | … | … | more sections continue every ~30 KB through the rest of the 3.2 MB file |

The complete inventory and the per-cell UV mapping needed to texture the
terrain properly hasn't been pinned down yet — these need to be cross-
checked against the `heightfield_handler_main @ 0x826884C8` call graph
(particularly the `sub_82687920..sub_82688E08` cluster) to decode every
section's role.  Tracking issue: bind per-cell atlas indices from one of
these later sections instead of the global UV-tiling approximation
currently used.

## 6b. File-type dispatcher table (`0x8215CA40`)

The engine registers per-extension/per-keyword handlers via the global
registrar `sub_82CA3700`. A C++ static-initializer table at `0x8215CA40`
holds 12 entries — each 8 bytes (`{fnptr, flags=0x40001003}`). Each pointed-to
function does:

```c
// example: .genv
sub_8222CF18(&dword_83498FA4, ".genv", -1);  // construct STL string at global
return sub_82CA3700(sub_832A4430);            // registers .genv dtor
```

The pattern is: a static-init *constructs* the extension-key STL string into
a fixed global, then passes a destructor pointer to `sub_82CA3700`. The
**actual handler function** is found by xref — it's the *other* function
that references the same extension-string global.

| Init function (registered) | Key | Destructor | **Real handler** (renamed) |
|------|------|------|------|
| `static_init_register_engine_theme` @ `0x832588D8` | `.engine_theme` | `sub_832A43F0` | (not located yet) |
| `static_init_register_save` @ `0x83258918`         | `.save` | `sub_832A4400` | (not located yet) |
| `static_init_register_gdb` @ `0x83258958`          | `.gdb` | `sub_832A4410` | (not located yet) |
| `static_init_register_ai_config` @ `0x83258998`    | `.ai_config` | `sub_832A4420` | (not located yet) |
| `static_init_register_genv` @ `0x832589D8`         | `.genv` | `sub_832A4430` | **`genv_handler_main`** @ `0x825F45A8` |
| `static_init_register_bnk` @ `0x83258A18`          | `.bnk` | `sub_832A4440` | (not located yet) |
| `static_init_register_havok_scenario` @ `0x83258A58` | `.havok_scenario` | `sub_832A4450` | (not located yet) |
| `sub_83258A98` | "DefaultScenario" | `sub_832A4460` | (keyword for scenario XML) |
| `sub_83258AD8` | "Version" | `sub_832A44D8` | (XML key) |
| `sub_83258B18` | "StartTimeInFrames" | `sub_832A44E8` | (XML key) |
| `sub_83258B58` | "StartTimeInSeconds" | `sub_832A44F8` | (XML key) |
| `sub_83258B98` | "QuestObjective" (hashed via `sub_821F3C28(-2128831035)`) | — | (quest XML key) |

Notably this 12-entry table does **not** include `.ehf`, `.ghf`, `.ama`,
`.amm`, `.amr`, `.hdb`, `.water`, `.mist` — those are registered in a
**second registrar list** elsewhere. The one keyword that IS there for
terrain is **`"heightfield"`** (note: no leading dot, no extension), which
was found by searching for the literal:

- `static_init_register_heightfield` @ `0x8325A1D0` constructs the string
  `"heightfield"` at `dword_8349AE18` and registers a dtor `sub_832A4DC8`.
- The **real heightfield handler** is **`heightfield_handler_main` @
  `0x826884C8`** — it's a 356-instruction function with 33 basic blocks
  that calls into a cluster at `sub_82687920..sub_82688E08` and matrix /
  geometry helpers at `sub_82D506F8..sub_82DB30C8`. That cluster is where
  the `.ehf`/`.ghf` byte layout is decoded.

So the next IDA pass to actually unlock terrain rendering should focus on
the call graph rooted at `heightfield_handler_main`:

```
heightfield_handler_main (0x826884C8)
├── sub_82687920
├── sub_82688E08
├── sub_82D506F8  ──┐
├── sub_82D7A018    │── matrix / vertex math helpers
├── sub_82DB0DC8 ───┘
├── sub_82DB2480
├── sub_82DB30C8
└── sub_8238A848    ── probably the stream/bytecode reader
```

The same registrar pattern almost certainly also stores `"genv"`,
`"havokscenario"`, and the heightfield-asset triplet keys somewhere — once
we find where `dword_8349AE18` ("heightfield") is *read* during file
dispatch, we should also see siblings for `.ehf` / `.ghf` etc.

Open questions for `.ehf` to chase next session:

- Where is the per-LOD mesh data (vertices + indices)?
- Where are the per-vertex texture-blend weights?
- Which entries in the engine_level type-`5` registry refer to terrain texture
  atlas pages, and how are they keyed against the `.texture_atlas` file?

## 7. Heightfield raw data (`.ghf`)

After gzip decompression (6.9 MB for the Bloodstone sample), the file is a
tight BE binary blob. Format empirically validated by decoding into a PNG
that matches the published Bloodstone heightmap (harbor + town outline).

### Header (20 bytes)

| offset | type    | meaning                                      |
|--------|---------|----------------------------------------------|
| 0x00   | f32 BE  | `tile_size` — world units between adjacent cells (sample = 64.0) |
| 0x04   | u32 BE  | padding (zero)                               |
| 0x08   | u32 BE  | padding (zero)                               |
| 0x0C   | u32 BE  | `W` — column count   (sample = 641)          |
| 0x10   | u32 BE  | `H` — row count      (sample = 769)          |

### Body — `W × H` cells, 14 bytes each

```c
struct GhfCell {
    float    height_be;        // +0  world-space Y at this vertex
    uint8_t  per_cell_data[10];// +4  apparently constant in our sample:
                               //     "00 00 00 00 02 3b 3f 6a 00 01"
                               //     — likely a material / blend tag.
};
```

The 14-byte stride exactly fills `(file_size - 0x14) = W * H * 14` bytes for
the Bloodstone sample (492,929 cells, 6.9 MB body). Heights range from
`+11.09` to `+154.03` world units — a sea-level-to-mountain spread that
visualises as a clearly-recognisable Bloodstone landscape.

Implementation: `Level::DecodeGhfHeights` in `src/Level/HeightfieldLoader.cpp`,
`Level::BuildTerrainMesh` turns the height grid into a renderable mesh
(positions + normals from gradient + UVs spanning `[0, 1]` + triangle
indices), which is then handed off via the `g_pending_terrain_load` /
`g_pending_terrain_mesh` globals to the renderer thread.

## 8. `ADMP` (`.ama` / `.amm` / `.amr`)

All three start `ADMP\0\0\0\x03` (version 3 BE). The trailing payload size
is wildly different (8.9 KB / 108 B / 40 B), so they're three siblings of the
same container, presumably:

- `.ama` – the ambient data (largest, has the meat)
- `.amm` – metadata / index
- `.amr` – references / lookup table

This is a guess from the suffix letters; needs IDA confirmation by searching
for the `ADMP` magic in code.

## 9. Renamed functions so far

| New name | Address | Original |
|----------|---------|----------|
| `level_parse_LevelGraphicsFile` | `0x82AAAC20` | `sub_82AAAC20` |
| `level_parse_LightmapFile` | `0x82A4AF78` | `sub_82A4AF78` |
| `level_parse_EngineResourceList_ctor` | `0x83285840` | `sub_83285840` |
| `level_dispatch_lmp_load` | `0x82AAAA18` | `sub_82AAAA18` |
| `lgf_handle_type2` | `0x82AA9A28` | `sub_82AA9A28` |
| `lgf_handle_type5_string` | `0x82AA9548` | `sub_82AA9548` |
| `lgf_handle_type15_section` | `0x82AAA530` | `sub_82AAA530` |
| `lgf_handle_type20_string` | `0x82AA9728` | `sub_82AA9728` |
| `stream_read_string_fixed` | `0x82A1B8C8` | `sub_82A1B8C8` |
| `stream_read_bytes_spilling` | `0x82A1B480` | `sub_82A1B480` |
| `stream_read_length_prefixed_string` | `0x82A1D5C8` | `sub_82A1D5C8` |
| `string_compare` | `0x8229AD78` | `sub_8229AD78` |
| `profiler_register_render_strings` | `0x82AA1C48` | `sub_82AA1C48` |
| `primitive_instance_enum_register` | `0x82AA7208` | `sub_82AA7208` |
| `heightfield_handler_main` | `0x826884C8` | `sub_826884C8` |
| `genv_handler_main` | `0x825F45A8` | `sub_825F45A8` |
| `static_init_register_engine_theme` | `0x832588D8` | `sub_832588D8` |
| `static_init_register_save` | `0x83258918` | `sub_83258918` |
| `static_init_register_gdb` | `0x83258958` | `sub_83258958` |
| `static_init_register_ai_config` | `0x83258998` | `sub_83258998` |
| `static_init_register_genv` | `0x832589D8` | `sub_832589D8` |
| `static_init_register_bnk` | `0x83258A18` | `sub_83258A18` |
| `static_init_register_havok_scenario` | `0x83258A58` | `sub_83258A58` |
| `static_init_register_heightfield` | `0x8325A1D0` | `sub_8325A1D0` |

## 9b. Heightfield render pipeline (per-chunk textures)

This section documents the **terrain renderer's runtime data model** as
reverse-engineered from IDA. Together these functions explain how the engine
goes from a parsed `.ehf` to drawn terrain.

### 9b.1 Class hierarchy / vtable

The HFG renderer is a class whose vtable lives at **`0x82132420`** (large form)
or **`0x820F6680`** (small form). Methods are stored as `(func_ptr, msg_tag)`
pairs (8 bytes each). Functions identified:

| Address | Role |
|---------|------|
| `0x82A7D470` | Constructor / vtable head |
| `0x82A7D4E0` | Per-frame state refresh (`sub_82A7D4E0`) — called when the chunk array or LOD threshold changes |
| `0x82A7D558` | Probably an LOD setter (one float arg) |
| `0x82A7D678` | **Full chunk-array reload** (msg-handler — accepts `result == 37`). Allocates the chunk array, walks a linked list of source nodes, decodes each via `sub_82A7CE00`, writes into the array via `sub_82A7D800`. |
| `0x82A7D788` | Single-chunk destructor (refcount-release on `+240` texture handle) |
| `0x82A7D800` | **Chunk-write** — copies 60 dwords of geometry + 1 dword texture handle from `a2` to `a1`, with refcount swap on the texture |
| `0x82A7D900` | **Partial chunk update** (msg-handler — accepts `result == 22`) — updates a subset of chunks from a smaller linked list, keyed by `node[2]` = chunk index |
| `0x82A7DA30` | **Terrain RENDER method** — iterates the chunk array and emits draw calls (see 9b.4) |
| `0x82A7E060` | Per-LOD setup (chunk's LOD slots at `+0, +80, +160`) |
| `0x82A7E0C0` | **Chunk-array resize** — `vector<chunk>::resize(new_count)`. New chunks default-constructed (no texture). |
| `0x82A7E1A0` | Chunk-array grow / element copy helper |

### 9b.2 Renderer-state struct (the `a1` passed to these methods)

Layout (offsets in bytes, from `sub_82A7D900` / `sub_82A7D678` / `sub_82A7DA30`):

| Offset | Field | Notes |
|--------|-------|-------|
| `+72`  | `double` low-height threshold | LOD boundary, written when a higher threshold arrives |
| `+80`  | `double` current LOD threshold | Compared against incoming threshold in `sub_82A7D678/900` |
| `+88`  | `float` (= `-1.0` sentinel) | Reset when threshold changes |
| `+96`  | chunk-array allocator state | Passed to `sub_82A7E0C0(a1+96, count)` to resize |
| `+100` | `chunk*` — array begin (= renderer_state[25]) | First byte of first chunk record |
| `+104` | `chunk*` — array end (= renderer_state[26]) | One past last chunk |
| `+112` | `byte` dirty flag | Set by partial-update path, drains via `sub_82A7D4E0` |
| `+116` | (probably allocator capacity / extra fields) | TBD |
| `+120..` | bbox extents for occlusion culling: `[+152]=minX, +156=minZ, +160=maxX, +164=maxZ` | Written by the upstream "begin render" function `sub_8234FBE0` |
| `+168` | `int` running count of buffered chunk-update commands | `++[+168]` in `sub_8234FBE0` |
| `+96` array | (vector control block — begin, end, end_of_storage) | Mirrors `std::vector` layout |

### 9b.3 Chunk record (244 bytes)

This is the per-chunk runtime data, one per 64×64-cell terrain tile (approx —
exact tile size TBD, but the render-loop walks 64-step world coords).

Layout per chunk:

| Offset | Size | Field |
|--------|------|-------|
| `+0..+79`    | 80 B | **LOD instance 0** (close range) |
| `+80..+159`  | 80 B | **LOD instance 1** (medium range) |
| `+160..+239` | 80 B | **LOD instance 2** (far range) |
| `+240`       | 4 B  | **Refcounted texture handle** ← per-chunk baked albedo |

Each LOD instance (the 80-byte block) packs:

| Offset (within instance) | Field |
|--------------------------|-------|
| `+0..+11`   | bbox / world corner (3 floats) |
| `+12..+19`  | 2 dwords (per-LOD metadata: vertex/index buffer handles?) |
| `+20..+31`  | bbox / world corner (3 floats) — second corner |
| `+32..+39`  | 2 dwords |
| `+40..+51`  | 3 floats |
| `+52..+59`  | 2 dwords |
| `+60..+71`  | 3 floats |
| `+72..+79`  | 2 dwords |

The 80-byte instance shape was derived from the `sub_82A7CE00` copy pattern:
out += 20 dwords, each instance interleaves 5 dwords of source data
(`a2[0..2], a2[12..13]`) with 15 dwords of geometry (`a2[3..11], a2[14..19]`).

### 9b.4 Render method (`sub_82A7DA30`)

```c
// Pseudocode of the relevant inner block
if ((v2[26] - v2[25]) / 244) {            // chunk array non-empty
  for (i = 0; i < count; ++i) {
    chunk* c = &chunk_array[i];           // = v45 + v2[25]
    sub_8221EC20(c->texture_handle, 0, 0, 0);   // BIND TEXTURE at +240
    if (c->texture_handle->[+84] != 0x7FFFFFFF) {  // texture is "ready" sentinel
      if (cur_lod >= 0)
        sub_821B7020(d3d_state, cur_lod, mask, mask);  // set LOD-shader state
      sub_8220A528(4, 2, &unk_83317F24, c->lod_instances + 80, 20);
      //                ^ prim type=4 (triangles)
      //                  ^ vertex-stream-count=2 (positions + UVs?)
      //                  ^ vertex-format descriptor
      //                  ^ pointer to LOD-1 instance data (middle LOD by default)
      //                  ^ 20 = vertex/index count or num primitives per draw
    }
  }
}
```

Three observations:

1. **Each chunk has its OWN texture** (refcount-bound at `+240`). The `.ehf`'s
   ~200 embedded BC1 `.tex` blobs are these per-chunk baked albedos plus their
   mip-chains.
2. **`*tex_handle[+84] != 0x7FFFFFFF`** is a "loaded" sentinel — chunks whose
   texture hasn't streamed in yet are skipped, not drawn black.
3. The draw call passes the **middle LOD instance** (offset `+80`) — the
   selected LOD is presumably picked elsewhere or by the shader from camera
   distance.

### 9b.5 Chunk source-data layout (the linked-list nodes parsed from `.ehf`)

`sub_82A7D900` and `sub_82A7D678` walk a linked list whose nodes have this
shape (deduced from the `i + 2`, `i + 3` offsets used to skip list metadata):

| Offset (in list node) | Field |
|-----------------------|-------|
| `+0`  | `next*` (linked-list pointer) |
| `+4`  | refcount or chunk index |
| `+8..+87` | **80 bytes** of geometry source data for **all 3 LOD instances** (= the `a2[0..19]` array `sub_82A7CE00` rearranges) |
| `+88..+91` | **Refcounted handle to the chunk's texture** (= `a2[20]` in `sub_82A7CE00`) |

`sub_82A7D900` reads `node[2]` = chunk index, `sub_82A7D678` doesn't (it loads
chunks sequentially by list position). Both pass `i + 2` (the source-data
pointer skipping `next + index`) into `sub_82A7CE00`.

### 9b.6 Decode pipeline (`sub_82A7CE00`)

```c
void chunk_decode(chunk_record* dst, source_dwords* src) {
  // 1. Rearrange geometry into 3 LOD instances of 80 bytes each
  for (int lod = 0; lod < 3; ++lod) {
    dst[lod*20 + 0..4]  = src[0,1,2, 12,13];   // 5 dwords header
    dst[lod*20 + 5..19] = src[3..11, 14..19];  // 15 dwords geom
  }
  // 2. Refcount-assign texture pointer to dst + 240
  refcount_swap(&dst[60], &src[20]);
}
```

So **the per-chunk texture pointer in the linked-list node is already a
decoded Texture2D-like object** by the time it reaches this function. The
actual `.tex`-blob → texture-object decode happens **earlier** in the
pipeline, inside whatever code builds the linked-list nodes.

### 9b.7 Single-chunk init handler (`sub_82A7EED0`) — message ID 35

This is the **most important discovery** for the texture decode. The renderer
also handles a "set ONE chunk" message (msg id `35`) that takes a small struct
of init data and copies it into a single chunk's source-node fields.

The init struct (`a2` parameter) is laid out like this:

| Offset | Type | Field |
|--------|------|-------|
| `+0`  | `vtable*` | Polymorphic dispatch tag (read at `(*(a2))()` and checked == 35) |
| `+4`  | `int`    | Chunk index (also doubles as `chunk[+8] |= 2 * idx`) |
| `+8`  | `int`/`float` | Origin X (cast `(int)` → `float` written to chunk `+76`) |
| `+12` | `int`/`float` | Origin Y (→ chunk `+80`) |
| `+16` | `int`/`float` | Origin Z (→ chunk `+84`) |
| `+20` | `int`/`float` | Extent X (→ chunk `+88`) |
| `+24` | `float`  | Extent Y (→ chunk `+92`) |
| `+28` | `float`  | Extent Z (→ chunk `+96`) |
| `+32` | `RefCounted<Texture2D>*` | **Per-chunk baked-albedo texture handle** (→ chunk `+72`, refcount-incremented) |
| `+36` | `int` | geom dword (→ chunk `+100`) |
| `+40` | `int` | geom dword (→ chunk `+104`) |
| `+44` | `int` | geom dword (→ chunk `+124`) |
| `+48` | `int` | geom dword (→ chunk `+128`) |
| `+52` | `int` | geom dword (→ chunk `+108`) |
| `+56` | `int` | geom dword (→ chunk `+112`) |
| `+60` | `int` | geom dword (→ chunk `+116`) |
| `+64` | `int` | geom dword (→ chunk `+120`) |

The `.ehf` parser builds this struct **once per terrain chunk** and dispatches
it through the message system. By the time the message arrives, `a2[32]`
already points to a fully decoded `Texture2D` object — meaning the BC1 decode
happens **before** this point.

### 9b.8 Full HFG renderer vtable (32 methods at `0x82132400`)

This is the entire class table for `HeightFieldGraphicsRenderer`. Each entry
is `(func_ptr u32, flags u32)`; `flags` encodes message ID and dispatch
properties.

| Idx | Func | Role (where known) |
|-----|------|--------------------|
| 0   | `0x82A7CE00` | `chunk_decode_from_source` — copies 3 LODs × 80 B + texture refcount swap |
| 1   | `0x82A7D0F0` | TBD |
| 2   | `0x82A7D360` | TBD |
| 3   | `0x82A7D470` | Constructor / vtable head |
| 4   | `0x82A7D4E0` | Per-frame state refresh (drains `+112` dirty flag) |
| 5   | `0x82A7D558` | LOD setter (1-float arg) |
| 6   | `0x82A7D678` | **Full chunk array reload (msg 37)** |
| 7   | `0x82A7D788` | Chunk destructor (refcount-release `+240` texture) |
| 8   | `0x82A7D800` | Chunk-record write (60 dwords + refcount-swap texture) |
| 9   | `0x82A7D900` | **Partial chunk update (msg 22)** — keyed by `node[2]` index |
| 10  | `0x82A7DA30` | **TERRAIN RENDER** — iterates chunk array, binds texture, draws |
| 11  | `0x82A7E060` | Per-LOD setup |
| 12  | `0x82A7E0C0` | `std::vector<chunk>::resize` |
| 13  | `0x82A7E1A0` | `std::vector<chunk>::reserve` / grow helper |
| 14  | `0x82A7E4E8` | TBD |
| 15  | `0x82A7E558` | TBD |
| 16  | `0x82A7E5C8` | TBD |
| 17  | `0x82A7E618` | TBD |
| 18  | `0x82A7E6A0` | TBD |
| 19  | `0x82A7E748` | TBD (482 bytes — largest method, possible "build chunks from heightmap") |
| 20  | `0x82A7EED0` | **Single chunk init (msg 35)** — texture handle at `a2[+32]` |
| 21  | `0x82A7F0C0` | TBD |
| 22  | `0x82A7F120` | TBD |
| 23  | `0x82A7F2A8` | TBD |
| 24  | `0x82A7F338` | TBD |
| 25  | `0x82A7F580` | TBD |
| 26  | `0x82A7F950` | TBD |
| 27  | `0x82A7F9A8` | TBD |
| 28  | `0x82A7FA88` | TBD |
| 29  | `0x82A7FB48` | TBD |
| 30  | `0x82A7FBC0` | TBD |
| 31  | `0x82A7FE80` | TBD |

### 9b.9 What's still missing — finding the `.ehf` → message-sender

We have a complete picture of the **renderer-side** message-handling code.
What we still don't have is the **producer**: the function that reads the
`.ehf` bytes and emits these three messages (35, 37, 22) carrying texture
handles.

Things to look for in the next IDA dive:

1. **Code that allocates a struct matching the msg-35 layout** (vtable, idx,
   6 floats, refcounted handle at +32, 8 dwords of geometry). The vtable
   pointer set at offset 0 will be an _emitter_ vtable that pairs with
   `sub_82A7EED0`. Finding either vtable's address will find the other side.

2. **Code that allocates 92-byte linked-list nodes** with a refcounted texture
   pointer at `+88` and pushes them to a list that's then sent as msg 37/22.

3. **The `.ehf` parser entry**: the file starts with magic
   `HeightFieldGraphicsFile\0` and version `18`. After that comes a struct
   we partially decoded above (per-cell BC5 normal map, an LOD pyramid of BC1
   baked albedo pages, and many small per-chunk BC1 textures). The parser
   pulls out the **per-chunk BC1 entries**, decodes each via
   `tex_decode_BC1_compressed` (the Huffman codec at `0x82B8C1C8`), wraps
   each result in a refcounted Texture2D, and emits one msg 35 per chunk.

4. **The chunk-position info** in the `.ehf`: each chunk knows its
   `(origin_xyz, extent_xyz)` plus 8 dwords of geometry. That data is in the
   `.ehf` somewhere — likely a header section before the texture blobs,
   keyed by chunk index. The geometry dwords (`a2[36..64]` in msg 35) probably
   point to vertex/index buffer offsets within the `.ehf` itself.

### 9b.10 Pragmatic next steps

Now that the per-chunk model is documented, two concrete code paths can be
implemented in C++:

**Option A — replicate the engine pipeline exactly.** Find the `.ehf` parser
in IDA (next dive), port the chunk-iteration to C++, decode each chunk's
BC1 via `lh_decode_compressed_mip` (we already have this), and bind each
chunk's texture to its own quad sub-mesh on the terrain.

**Option B — composite everything into one texture upfront.** Walk every
embedded BC1 tex in the `.ehf` (we know how — section 9b.7 patterns), decode
each via the Huffman codec, blit each into its `(origin_xyz)` slot of a single
big composite texture sized to the heightfield (e.g. 1281×1537 for Bloodstone
= 641×769 × 2 px/cell), and bind that one texture with linear UVs across the
whole terrain mesh.

Option B is simpler in C++ (one SRV, no shader rewrite, terrain mesh stays
unchanged) but needs the chunk-position info from the `.ehf` parser. So in
both cases, **the next IDA task is the same**: find the function that
populates the msg-35 struct, follow its `a2[+8..+28]` writes back to the
`.ehf` source bytes, and we'll have the chunk → world-space mapping.

### 9b.11 The `.ehf` parser — `sub_82A855A8` @ `0x82A855A8`

Found it. The HFG parser is `sub_82A855A8`. Key signals that pin it down:

* `stream_read_string_fixed(&v88, &v92, 23)` — reads a 23-byte magic. The
  literal `"HeightFieldGraphicsFile"` is exactly 23 characters.
* The string at `0x820F677C` is referenced only from a static
  `RTTI-class-descriptor` at `0x83317E9C`, never from code, because the magic
  is wrapped in a per-class string-init helper.
* The parser is reached via wrapper `sub_82A852E0 @ 0x82A852E0` (74 bytes),
  which is in turn registered in the loader's class-handler table at
  `0x821326A0`.

**The `.ehf` is not a flat file.** The parser opens it as a *bundle handle*
via `sub_82C64708(*a2, &v71)` — `*a2` is the level-loader-supplied resource
key for the `.ehf`. After that, every read goes through:

    (*(void **)(*(int *)v71 + 12))(v71, dst, offset, size, 0)

which is `bundle::read(dst, offset, size, flags)`. So `.ehf` parsing is a
sequence of `bundle_read` calls into separate slabs, not a linear `fread`
of the whole file.

**The header is exactly 63 bytes long.** The parser issues
`bundle::read(buf, offset=0, size=63, 0)` once at the top to pull the
header into RAM, wraps it in a stream, then dispatches the fixed-size
reads below. After parsing the header, the *body* offset and size are
known from the last `(u32, u32)` pair — and a second `bundle::read` pulls
the body into a separate RAM blob that the rest of the parse iterates.

**Header layout (63 bytes, big-endian)**:

| offset | size | meaning | landed at |
|-------:|-----:|---------|-----------|
| `+0`   | 23 | magic `"HeightFieldGraphicsFile"` (no NUL) | — |
| `+23`  | 4 | version `u32` (expects `18`) | — |
| `+27`  | 4 | `float` | `state[+68]` |
| `+31`  | 4 | `float` | `state[+64]` |
| `+35`  | 4 | `u32`  (vertices-per-cell-row or similar) | `state[+72]` |
| `+39`  | 4 | `u32`  (vertices-per-cell-col or similar) | `state[+76]` |
| `+43`  | 4 | `float` | `state[+80]` |
| `+47`  | 4 | `float` | `state[+84]` |
| `+51`  | 4 | `float` | `state[+88]` |
| `+55`  | 4 | `u32`  body offset (in the bundle) | local `v88` |
| `+59`  | 4 | `u32`  body size                    | local `v89` |

The 5 floats at state offsets `+64..+88` are very likely the
heightfield's world-space `(origin.xy, extent.xy, max_height)` tuple.
The two `u32`s at `+72/+76` are some per-cell resolution (probably
verts-per-tile or texels-per-cell — distinct from the chunk grid `W×H`
which is read out of the body, not the header).

After the header parse, the body is pulled in as a single
`bundle::read(buf, offset=v88, size=v89, 0)`.  But the body is **not**
a renderer-state struct as I first (incorrectly) read from the IDA
pseudocode — empirical decode of all 35 extracted `.ehf` files (see
`tools/ehf_dump.py`, `tools/ehf_body_decode.py`) shows the body is a
**zlib-compressed Xbox-360-tiled `.tex` blob** with `PixelFormat = 24`
(L8A8, 16 bpp) and dimensions `u0 × u1` from the header.

Decoded, it's a **per-cell terrain lightmap / ambient bake**:

* High byte (channel R after decode) — smooth grayscale resembling
  baked ambient occlusion + sky-light shadows; visible foliage-shadow
  clusters and clean shapes for buildings / paths / water.
* Low byte (channel G) — high-frequency noise / detail / dither
  pattern; smooth in flat-water and concrete regions, noisy across
  vegetated terrain.  Likely a secondary blend / vignette channel
  or 8-bit dither for the high byte's effective 16-bit precision.

What I'd previously called the "renderer-state stream parse" in IDA
must be reading something else (a different file or a later section
of the same bundle) — the actual `.ehf` body parses cleanly as
`.texture_atlas`-shaped data, and the `TextureAtlas::DecodeAtlas`
path now handles `PixelFormat = 24` for it.

**Leftover bytes after the body (= `.ehf` file size − body_offset −
body_size)** range from ~150 KB on small per-area `.ehf`s to ~4 MB on
the full-Bloodstone `.ehf`. This is where the rest of the file lives:

* The `EhfPalette::Parse` finds `art\...\xxx_diffuse.tex` /
  `_normal.tex` path pairs with per-entry `(tile_scale, intensity)`
  metadata.
* The *per-cell material-index data* that maps each heightfield cell
  to a palette entry is in here too — still unidentified format.
  Look here next: the leftover should contain another `0xFFFFFFFE`
  `.tex` blob (validated by scanning `bl_chapter3.ehf` — multiple
  blobs at offsets `0xEAB81` (PF=40, BC5 normal map of the same
  769×769 size), then PF=98 / PF=99 entries, then ~20 BC1 (PF=35)
  per-chunk diffuse pages forming a mip pyramid).

So the actual layout of a `.ehf` (validated against
`bl_chapter3_heightfield_id_9501a1af.ehf`, 6.5 MB, body_size 2.3 MB):

    0x00         63B header (§ 9b.11 documents this)
    +0x3F        .tex PF=24    lightmap        (W=769, H=769, 16 bpp)
    +0xeab81     .tex PF=40    BC5 normal map  (W=769, H=769)
    +0x16cfcb    BVH / octree culling tree     (~530 KB)
                   — starts with `42 b3 7a 60` (= max_height float, 89.74)
                   — header counts: 72, 96, 96 chunk grid
                   — 32-byte node records: {min_vec3, max_vec3, padding}
                   — terminated by `00 00 00 14` (=20, palette count)
    +0x1ee4d7    PALETTE  (40 entries × ~50B each = ~2 KB)
                   — each entry: diffuse_path\0 normal_path\0 + 13B meta
                   — meta: 0x00, f32 tile_scale, f32 intensity, 4B zero
    +0x1ef6fe    sentinel u32 = 0x00000001
    +0x1ef702    .tex PF=99   BC1-like baked albedo at HIGHER resolution
                              (W=1650, H=1815, padded to 1664×1920,
                               raw=1.5 MB BC1)
    +0x235f1b    body_end (header-stated body_size ends here)
    +0x235f1b    .tex PF=35   BC1 mip pyramids — per-region diffuse pages
                  (chapter3 has ~21 distinct textures × 3 mip levels each,
                   sizes 344×408 → 172×204 → 86×102, 248×280 → 124×140 →
                   62×70, etc.)

The `body_offset`/`body_size` in the header only names the FIRST region
(the lightmap + BC5 normal + BVH + palette + PF=99 baked albedo).
The PF=35 mip pyramids after `body_end` are the per-region diffuse
texture pages, used at high-LOD camera distances.

**The PF=99 BC1 baked albedo is the per-cell color data we wanted** —
but Python decode attempts (tools/ehf_splatmap.py) with the standard
Xbox 360 BC1 untile formula produce mostly black with scattered
correct-looking BC1 blocks.  The endpoint pattern and dimensions
(`1664 × 1920 / 2 bytes = raw_size` exactly) confirm BC1; the
remaining question is which tile-swizzle the engine uses (it does
NOT match `xg_address_2d_tiled_*` with `(block_pixel_size=4,
texel_byte_pitch=8)`).  Open work item — see TODO list.

**For the current renderer** the working visible-improvement path is
`Level::BakeEhfTerrainComposite`: decode the PF=24 lightmap → modulate
the first decodable palette diffuse by lightmap channel R (baked AO)
→ output a heightfield-sized composite RGBA with `uv_scale = 1.0`.
This yields terrain that shows the level's primary ground material
PROPERLY DARKENED by the baked shadow/AO pass — dramatically better
than the plain stretched-single-texture rendering.  Once we crack
the PF=99 untile, we can replace the stretched-material step with
the real per-cell baked diffuse.

**High-level shape of `sub_82A855A8`**:

```c
// 1. Open bundle handle keyed by *a2 (resource id)
sub_82C64708(*a2, &v71);                       // bundle::open(resid, &handle)

// 2. Read 23-byte magic "HeightFieldGraphicsFile" + a u32 (version?)
stream_read_string_fixed(&magic_buf, &reader, 23);
stream_read_u32(&reader, &version);            // expects 18

// 3. Build a debug label "#%08X" of *a2 (texture lookup key fallback)
sub_821E3A10(&v69, "#%08X", *a2);

// 4. Three sub-readers populate fixed pieces of the renderer-state struct
sub_82A84C08(state, &reader);                  // header / scenario name
sub_82A85358(state, &reader, &payload_size);   // body offset + size lookup

// 5. Open a NESTED stream over the body bytes (bundle_read into RAM blob)
//    — the rest of the parse happens on the in-memory blob `&v72`.

// 6. Two heap-allocated owner objects at state[+8], state[+16]:
//    8-byte refcounted handles for the texture-page / mesh references.
sub_82A16CA8(handle, &nested_reader);          // texture page table?
sub_82A16CA8(handle, &nested_reader);          // mesh table?

// 7. Float at state[+176]: probably max LOD distance or world-extent
read_be_float(&nested_reader, &state[+176]);

// 8. sub_82A850A0(state, &nested_reader)  — small extra header fields
// 9. sub_82A860E8(state, &nested_reader)  — more header fields
// 10. Two more refcounted handles at state[+24] and state[+32]
sub_82B86778(handle, &nested_reader, 0);
sub_82B86778(handle, &nested_reader, 0);

// 11. sub_82A85F20: reads `count` then alloc(240B) × count of LOD descriptors
//     stored at state[+52..+56] (start..end ptrs, 8-byte stride).
sub_82A85F20(state, &nested_reader);

// 12. sub_82A85DB0: reads the texture-pool index — count + count×8B
//     stored at state[+12..+16] (start..end ptrs). These are the
//     (texture_pool_key, handle) pairs that every chunk indexes into.
sub_82A85DB0(state, &nested_reader, &state[+0]);

// 13. Read W (numChunksX) and H (numChunksY) as big-endian u32s.
read_be_u32(&nested_reader, &state[+92]);      // W
read_be_u32(&nested_reader, &state[+96]);      // H
state[+100] = W * H;                           // total chunk count

// 14. Reserve / grow the chunk pointer array at state[+108..+112]
//     (4-byte stride, length W*H).

// 15. Per-chunk loop: for i in [0,W), j in [0,H)
//     v55 = j*W + i
//     chunk = alloc(112);                     // 112-byte chunk record
//     chunk[+0]   = &off_820F82CC;            // chunk vtable
//     chunk[+52]  = 0xFFFFFFFF;               // sentinel
//     chunk[+68..+96] zero
//     chunk[+88..+95] = 0;                    // texture handle slot (filled later)
//     chunk[+100] = 1;
//     chunk[+104] = 1;
//     chunk[+105] = 1;
//     state[+108][v55] = chunk;
//     sub_82B25728(chunk, &nested_reader, ...) // reads chunk fields:
//       chunk[+16..+31] = vec4  (origin)      // 32B via lvx128/stvx128
//       chunk[+32..+47] = vec4  (extent)
//       read count v16
//       chunk[+64..+68] = vector<lod>(count×48B)
//       per-LOD: sub_82B25930(...)            // populates each 48-byte LOD

// 16. sub_82A854D8(state, &nested_reader)
//     state[+44] = (read u8) != 0             // some bool flag
//     foreach 8-byte slot in state[+124..+128] : sub_82B24750(slot, reader)
//       — each `slot` is a (texture_pool_index, ?) pair; sub_82B24750
//       reads `count` + count×{u32 v42, u32 v43} into 28-byte sub-structs
//       at slot[+24]. These tie LODs to texture-pool entries.

// 17. close bundle handle, free temporaries.
```

**Texture handles are NOT decoded inline.** The `.ehf` parser builds:

* A vector of texture-pool **references** at `state[+12..+16]` (8B stride).
  Each entry is a key+handle pair into the global `Texture2D` cache
  (`unk_8349F7BC`, the hash-map looked up by `sub_821ABC00`).
* Per chunk, a vector of LOD descriptors at `chunk[+64..+68]` (48B stride).
* Per LOD, a sub-array at `lod[+24]` of `(texture_pool_idx, ?_dword)` pairs
  read by `sub_82B24750`.

When the renderer wants to draw a chunk, it walks the LOD's pool-index list,
looks each index up in `state[+12]` to get the pool-key, and then calls
`sub_821ABC00(&out_handle, pool_key)`. That function:

1. Probes the `unk_8349F7BC` map for `pool_key`.
2. On miss, allocates a 224-byte `Texture2D` (`sub_8221F388(224)`),
   initializes it via `sub_82A6D650(buf, &pool_key)`.
3. `sub_82A6D650` then calls `sub_82A6EA50(buf)`, which opens **another**
   bundle/resource handle (`sub_82C647D0(key, &handle, 0, 84, 1, 1)`) and
   memcpys 84 bytes of texture metadata at `buf + 132`.
4. The actual BC1 pixel decode happens lazily, inside the Texture2D's first
   GPU upload, via `tex_decode_mip → tex_decode_BC1_compressed` (the Huffman
   codec at `0x82B8C1C8`).

**Implication for the asset browser.** The `.ehf` doesn't contain the BC1
texture bytes at all — only **keys** that name entries in some other bundle
(`.tex_atlas` / texture pool). The 32-bit key `*a2` that the level loader
passes to `sub_82A855A8` is itself one of those keys, looked up against the
same bundle system. So to faithfully texture the heightfield we need:

1. The map of texture-pool keys → `.tex` blob inside whatever bundle the
   level was packaged with. That bundle is whatever `sub_82C64708` reads
   from — likely the level's main `.bnk` or a sibling.
2. The per-chunk pool-index list (`state[+108]`'s array), which lives inside
   the `.ehf` blob as the per-chunk LOD records we just walked.

**Per-chunk struct layout (112 bytes)** — final reading from `sub_82A855A8`
+ `sub_82B25728`:

| offset | size | meaning |
|-------:|-----:|---------|
| `+0`   | 4 | vtable (`off_820F82CC`) |
| `+4`   | 4 | refcount |
| `+8..+15` | 8 | flags / pad |
| `+16..+31` | 16 | `vec4` origin (world space, BE) |
| `+32..+47` | 16 | `vec4` extent (world space, BE) |
| `+48..+51` | 4 | ? |
| `+52`  | 4 | `0xFFFFFFFF` sentinel |
| `+56..+63` | 8 | ? |
| `+64`  | 4 | LOD-vector `begin` ptr |
| `+68`  | 4 | LOD-vector `end` ptr (stride 48B) |
| `+72..+87` | 16 | LOD-vector pad / capacity / allocator |
| `+88..+95` | 8 | Texture2D handle (set lazily by renderer) |
| `+96`  | 4 | ? |
| `+100` | 4 | `1` (visibility / dirty flag) |
| `+104` | 1 | `1` (flag A) |
| `+105` | 1 | `1` (flag B) |
| `+106..+111` | 6 | pad |

**Renderer-state offsets touched by `sub_82A855A8`** (subset):

| offset | meaning |
|-------:|---------|
| `+0`   | scenario name / debug string buf |
| `+8`   | refcounted handle (texture page table?) |
| `+16`  | refcounted handle (mesh table?) |
| `+24`  | refcounted handle |
| `+32`  | refcounted handle |
| `+44`  | u8 flag (set by `sub_82A854D8`) |
| `+52..+56` | LOD-descriptor vector (240B stride, count = `read u32`) |
| `+92`  | numChunksX (W) |
| `+96`  | numChunksY (H) |
| `+100` | W * H |
| `+108..+112` | chunk pointer array (4B stride, length W*H) |
| `+124..+128` | post-chunk per-LOD index pair array (8B stride) |
| `+176` | float (LOD distance? world bounds?) |
| `+248` | original `*a2` resource id |

This is what we needed. Implementing **Option B** (one composite texture)
now reduces to:

1. Re-implement `sub_82C64708`-style bundle reads from the level's BNK so
   we can fetch the texture blobs by 32-bit pool key.
2. Walk the chunk array in the parsed `.ehf`, fetch each chunk's primary
   LOD texture key from `chunk[+64]`'s LOD vector, decode it via our
   existing `lh_decode_compressed_mip`, and blit into the right
   `(origin.xy → uv)` cell of a big composite RGBA target.
3. Bind that composite as the heightfield's albedo with a single linear UV
   transform.

`chunk[+16..+31]` (origin) and `chunk[+32..+47]` (extent) give us the
world-space → composite-UV mapping for free.

### 9b.14 Full body parser — VALIDATED end-to-end against chapter3

A third IDA pass plus an empirical Python prototype
(`tools/ehf_body_walker.py`) produces a parser that consumes the
chapter3 `.ehf` body to *exactly* the right number of bytes
(`final pos = body_end`, `remaining = 0`).  Implemented in C++ as
`Level::ParseEhfBody` (`src/Level/EhfChunkParser.{h,cpp}`).

The full body layout, in stream order:

```
+0          Texture[0]: PF=24 lightmap (.tex blob, header 92B + zlib)
+next       Texture[1]: PF=40 BC5 normal map (.tex blob, header + zlib)
+next       u32-as-float  state[+176]  (max-height-ish)
+next       sub_82A850A0 vector — terrain mesh tiles
              u32 count
              count × {
                f32 f_a, f32 f_b, u32 w_sub, u32 h_sub
                w_sub × h_sub × 160B vertex grid
                24B trailer (2 × sub_82A1BEA8 reads, 12B each)
              }
+next       sub_82A860E8 vector — TBD (used by final-pass second read)
              f32 state[+40]
              u32 count
              count × 18B per entry (sub_82B24200: 3 floats + 6 bytes)
+next       2 more textures (typically PF=98 uncompressed — header 88B
                              + raw_size raw bytes, no zlib)
+next       sub_82A85F20 LOD vector — palette material refs
              u32 count
              count × {
                3 × null-terminated strings (texture paths, layer tags)
                12 bytes (float v182 + u32 v193 + float v183)
                3 × null-terminated strings
                12 bytes (3 floats)
              }
+next       sub_82A85DB0 vector — count + N textures (PF=99 lives here)
              u32 count
              count × .tex blob
+next       u32 chunk_w
+next       u32 chunk_h
+next       chunk_w × chunk_h × CHUNK records:
              vec3 origin (3 floats = 12B, builds vec4 via permute)
              vec3 extent (3 floats = 12B)
              u32 layer_count
              layer_count × LAYER {
                u32 (sub_82B25850 result)
                u32 name_index
                vec2 (2 floats — tile_uv parameters)
                4 × u8 (texture indices into the LOD vector)
                4 × u8 (blend weights 0..255)
              }
+next       sub_82A854D8 final pass
              u8 flag
              860E8_count × {
                u32 sub_count
                sub_count × 8 bytes
              }
+body_end
```

**Validated numbers for `bl_chapter3_heightfield_id_9501a1af.ehf`**:

* 20 LODs in the LOD vector (matches `EhfPalette::Parse`'s 40 = 20 × 2 entries)
* 24 × 24 = 576 chunks in the chunk grid
* Each chunk has 1–2 layers
* Chunk origin/extent values are in world units; the grid covers
  exactly the heightfield's world span (24 chunks × 16 wu = 384 wu =
  769 cells × 0.5 wu/cell).
* Per-layer texture indices `[0, 3, 0, 3]` mean "blend LOD 0's diffuse
  with LOD 3's diffuse" — typically a `(BaseLayer, DetailLayer)` pair.
* Final parser pos = `0x235edc` = `body_size` exactly.

**This is THE per-cell material binding.**  The `texture_idx[4]` /
`blend[4]` in each chunk's first layer drives the per-region terrain
material selection in the engine.

The current C++ bake (`BakeEhfTerrainCompositeWithBnk`) picks the
**primary texture (texture_idx[0])** per chunk and tile-samples it at
the chunk-layer's `tile_uv` rate.  Full multi-texture blending
(weighted average of all 4 textures via `blend[4]`) is the next
step for visual fidelity — for now the dominant material per chunk is
what gets rendered.

### 9b.12 Corrected understanding — PF=99 is NOT a splat map, per-chunk layers are

After a second IDA pass (May 14), the picture is significantly different
from what § 9b.11 first claimed.  The per-cell material binding is in
the **per-chunk layer vector**, not in the PF=99 `.tex` blob.

**Actual body parse flow** (re-read of `sub_82A855A8`):

1. Refcounted handles → `state[+8, +16, +24, +32]` (4 of them).  These
   are owner-ship handles for sub-objects, not strings.
2. Float → `state[+176]` (some LOD distance / max-height).
3. `sub_82A850A0`: read `u32 count`, then **count × 96-byte entries**
   into the vector at `state[+164..+168]`.  Each entry populated by
   `sub_82B250E8` from the stream.
4. `sub_82A860E8`: read float → `state[+40]`, then `u32 count`, then
   **count × 40-byte entries** into the vector at `state[+124..+128]`.
   Each entry has its own internal vector at `entry[+20..+24]` of 28B
   sub-entries, populated by `sub_82B24750` later.
5. `sub_82A85F20`: read `u32 count`, then **count × 240-byte LOD entries**
   into `state[+52..+56]`.  Each entry populated by `sub_82A81748`,
   which reads **6 string-refs and 6 floats** per entry.  This is where
   the palette texture references end up.  Note: `sub_82A81748` checks
   if the first two strings are `"BaseLayer"` — that's a layer-type tag
   in the engine's terrain shader.  The 6 textures map to state offsets
   `+176, +180, +184, +200, +204, +208` (diffuse + normal + others for
   one or two material layers).
6. `sub_82A85DB0`: read `u32 count`, then **count × 8-byte string refs**
   into a *local* vector `v61` (the per-chunk layer-name list — strings
   like `"BaseLayer"`, `"DetailLayer"`, etc. used as indices in step 8).
7. Read W (`u32 → state[+92]`) and H (`u32 → state[+96]`) — the
   **CHUNK GRID dimensions**, NOT the heightfield cell count and NOT
   the PF=99 texture dimensions.  Small numbers — for a 769×769
   heightfield this might be e.g. `25 × 25` or similar.
8. Allocate W×H chunk records (112 bytes each) at `state[+108..+112]`.
9. For each chunk, `sub_82B25728` reads:
   - `vec4 origin` (16B) at chunk `+16` — world-space position
   - `vec4 extent` (16B) at chunk `+32` — world-space size
   - `u32 layer_count` then `layer_count × 48-byte layer entries` at
     `chunk[+64..+68]`.  Each layer entry populated by `sub_82B25930`:
     - `u32 layer_name_index` (into v61, the local layer-name list)
     - `vec2` (2 floats) — tile/scale parameters
     - vec4 derived (16/inv_count, ...) at layer `+16..+31`
     - **4 bytes** at layer `+32..+35` (= 4 byte values)
     - **4 bytes** at layer `+36..+39` (= 4 byte values)
   - These two 4-byte groups are very likely
     `(texture_index_a, texture_index_b, texture_index_c, texture_index_d)`
     + `(blend_a, blend_b, blend_c, blend_d)` — the four textures and
     their weights for THIS chunk's THIS layer.  The texture indices
     reference the LOD vector at `state[+52..+56]` (which holds the
     actual diffuse/normal `.tex` references).
10. `sub_82A854D8` final pass: reads one byte flag → `state[+44]`, then
    walks the 40-byte vector from step 4 and for each entry calls
    `sub_82B24750`, which reads `u32 count` + `count × {u32, u32}` pairs
    into the entry's internal sub-vector.

**Where this leaves PF=99.** The PF=99 `.tex`-shaped blob at offset
`0x1ef702` for chapter3 is something *else* — possibly fine-grained
per-pixel blend data, possibly an entirely separate runtime resource.
The `.tex` magic + dimensions + zlib stream at that offset look genuine
(too much structure to be coincidence) but the inflated content is
**not** what the chunk parser reads — that's all done before reaching
this offset.  Possibilities:

- The bytes I thought were a PF=99 `.tex` header are actually part of
  the chunk grid serialisation and the `FF FF FF FE`-magic match is
  coincidental.  (Argues against this: the W/H/PF/mt fields directly
  after are too well-formed to be random.)
- The PF=99 blob is a *separate* fine-detail map that the renderer
  loads on demand, not in this parser at all.  The chunk parser may
  not advance past offset `0x1ef702` because all required data was
  consumed before then.

**The per-cell material binding is NOT in PF=99.**  It's in the per-chunk
layer entries' 4+4 byte arrays.  Decoding it requires implementing the
chunk-grid parser in C++ (still ahead of us).

### 9b.13 Forward plan

For a "the terrain looks correct" outcome we need to:

1. Implement a faithful C++ port of `sub_82A855A8`'s body parser:
   - Skip past the 4 refcounted handles + the first float
   - Parse the 96B and 40B entry vectors (or skip them — we may not
     need their content for rendering)
   - **Parse the 240B LOD vector** to get the palette of texture refs
     (this is what `EhfPalette::Parse` already pattern-matches — verify
     the 40 entries correspond to LOD entries × 6 strings each, after
     the `"BaseLayer"` early-out)
   - **Parse the 8B layer-name list**
   - Read W, H (the small chunk grid)
   - For each chunk: parse `vec4 origin`, `vec4 extent`, layer count
     and each layer's `(name_index, tile_scale, tex_indices[4],
     blend[4])`
   - Run `sub_82A854D8`'s final-pass byte+nested-vector reader so the
     stream offset reaches `body_end` correctly (validates the parser)

2. Use the parsed chunks to drive rendering.  Two viable approaches:
   - **CPU bake (Option B revisited)**: for each composite texel, find
     which chunk it falls into (via `vec4 origin`/`extent`), look up
     the chunk's primary layer's primary texture, sample at that
     layer's `tile_scale`, multiply by lightmap.  Produces a baked
     composite that visually matches the engine's per-chunk material
     choices.
   - **Multi-mesh approach**: emit one sub-mesh per chunk, bind each
     chunk's primary diffuse, let the renderer draw them separately.
     Closer to how the engine actually does it (the runtime
     `Chunk → Texture2D` handle map).

3. Leave PF=99 decode as an open question for now.  It might be:
   - A per-texel detail/blend map (1650×1815 finer-grained than chunks)
   - A heightfield-wide ambient-influence map
   - Something completely unrelated to terrain albedo
   The `tools/ehf_pf99_extract.py` script extracts raw .bin dumps so
   it can be loaded into ImageHeat or similar to test interpretations.

The current `BakeEhfTerrainCompositeAndSplatDebug` C++ path is built
on the wrong assumption (using PF=99 as a 4-bit splat map) and will
keep producing nonsense until replaced with a real chunk-grid parser.

## 10. Recommended path to terrain rendering

Working backwards from the end goal:

1. **Resolve the BNK paths from `level.vfsconfig`.** All three terrain-texture
   composites + the level model BNK + globals_model_headers must be added to
   the asset browser's BNK index. This is reusing existing code, no
   format-decode work.
2. **Decode `.texture_atlas`.** It's `0xFFFFFFFE` like a regular `.tex` —
   the asset browser already has a parser. The terrain shaders likely sample
   atlas pages directly.
3. **Decode `.ehf`.** Need to find the concrete `parse(stream)` for the
   `HeightFieldGraphicsFile` class descriptor at `0x83317E90` — that lets us
   read the per-tile mesh + per-vertex texture blend.
4. **Decode `.ghf`.** Raw heightmap; once `.ehf` tells us tile size and grid
   layout, the height values can be sampled per-vertex.
5. **Walk `LevelGraphicsFile`'s type-`2` (instance) entries** to populate the
   scene with model instances at the right transforms — those are the props,
   buildings, etc.

Items 1 and 2 are reusable code; item 3 is the next IDA dive; items 4 and 5
depend on it.
