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
