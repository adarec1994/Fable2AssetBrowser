#pragma once
// IsoDump — bulk raw-extract every loaded asset out of a mounted ISO
// into the user's configured export directory.

// Pulled in for the TexExportFormat enum used by dump_tex_files_as.
// Slightly heavier than a forward declaration but TextureExport.h is
// already a leaf header so the cost is minimal.
#include "../textures/export/TextureExport.h"
//
// "Loaded" here means the three asset families the rest of the app
// scans the disc for: .bnk, .adb, .lua. Files are written byte-for-byte
// (no decoding, no re-pack) preserving the disc's directory tree under
// `${S.export_dir}`.
//
// Runs on a worker thread, drives the progress modal, posts a
// completion box at the end and logs status through OutputLog.

namespace ISO {

// Kicks off the dump in a detached worker. Returns immediately.
// Safe to call when no ISO is mounted — emits an error toast and
// returns. Multiple concurrent invocations are not supported; the
// caller (the File menu) gates the entry on `IsoMount::is_mounted()`.
void dump_iso_contents();

// Dump every .mdl file the asset browser has scanned (S.all_mdl_files).
// Goes through the same reconstruct path the model-preview loader uses
// — so nested-BNK MDLs get rebuilt with their full bitstream / vertex
// data the way the runtime would expect to read them, not just the
// raw header bytes. Writes to `${S.export_dir}/<asset_path>.mdl`,
// preserving the directory layout from each MDL's source BNK.
//
// Works in both ISO and folder modes — the reconstruct path doesn't
// care which backend the bytes come from.
void dump_mdl_files();

// Format selector for `dump_mdl_files_as` and the per-file
// `mdl_export_begin_named`. GLB / FBX both produce a single self-
// contained file with embedded textures, materials, skin, weights,
// UVs. RAW dumps the reconstructed `.mdl` bytes (header+body
// concat — same output as `dump_mdl_files()`).
enum class MdlExportFormat {
    GLB,
    FBX,
    RAW,
};

// Per-file MDL export — used by the right-click "Export to" menu
// items. Synchronous on the calling thread because a single MDL is
// fast (<200 ms typical). For batches use `dump_mdl_files_as`.
//
// Takes the BNK and the entry's index inside that BNK directly —
// the earlier `mdl_full_path` lookup was unreliable because the
// file tree / flat tabs only carry the leaf name (e.g.
// "Balverine_DLC_poison.mdl") while the BNK stores full asset
// paths (e.g. "art/characters/balverine/Balverine_DLC_poison.mdl").
// Right-click callers all already have the index in hand, so we
// just pass it through.
//
// `display_path` is used to compose the output filename and log
// line; if empty the function falls back to the BNK entry's stored
// name. The reconstructed-MDL helper inside the function still does
// the `_models.bnk` ↔ `_model_headers.bnk` pair lookup the same way
// the MDL preview path does.
//
// Output path: `${S.export_dir}/<asset_path>.<ext>` with the
// extension swapped to match `fmt` (.glb / .fbx / .mdl).
void mdl_export_begin_named(MdlExportFormat fmt,
                            const std::string& bnk_path,
                            int file_index,
                            const std::string& display_path,
                            bool from_nested);

// Decoded variant of dump_mdl_files. Walks S.all_mdl_files and
// writes every MDL as `fmt` to `${S.export_dir}/<asset_path>.<ext>`.
// `RAW` short-circuits to dump_mdl_files() (raw bytes — same path).
// Worker thread + progress modal so the UI stays responsive on a
// multi-thousand-MDL batch.
void dump_mdl_files_as(MdlExportFormat fmt);

// Dump every .tex file the asset browser has scanned (S.all_tex_files).
// Same shape as dump_mdl_files, but with the third-component twist that
// textures need a global `1024mip0_textures.bnk` lookup in addition to
// the paired `_texture_headers.bnk`. The dumped buffer is the same
// header + mip0 + body concatenation `build_tex_buffer_for_name`
// produces for the in-app preview, so a downstream decoder can ingest
// it without any awareness that it came from three BNKs.
void dump_tex_files();

// Decoded variant of dump_tex_files: walks S.all_tex_files and writes
// every texture as `fmt` (PNG / JPG / TIFF / DDS) to
//   `${S.export_dir}/<asset_path>.<ext>`
// Each entry runs through `tex_export_begin_named` — same plumbing
// the per-file right-click "Export to" menu uses, just looped over
// every flat-list entry. Worker thread + progress modal so the UI
// stays responsive on a 5000-texture batch.
//
// `fmt` must be one of PNG / JPG / TIFF / DDS; passing TEX falls
// through to `dump_tex_files()` (raw bytes — pointless to re-route
// through the decode pipeline just to land at the same bytes).
void dump_tex_files_as(TexExportFormat fmt);

// Dump every .wav file the asset browser has scanned (S.all_wav_files).
// Wavs aren't split — this is just a per-BNK extract loop, no
// reconstruction. Faster than the model/texture dumps because there's
// no pair lookup or memory concat: the extract streams straight to
// disk under the export root, preserving the source path.
//
// The bytes that come out are the raw XMA2-encoded WAV the BNK
// stores. To get a playable PCM .wav (or MP3 / AAC), use
// `dump_wav_files_as(AudioExportFormat::…)` instead.
void dump_wav_files();

// Format selector for `dump_wav_files_as`. Audio in retail Fable 2
// BNKs is XMA2-encoded, wrapped in a RIFF/WAVE header — playable
// only on the original Xbox 360 hardware decoder. The variants here
// run that source through XmaDecoder + (for MP3/AAC) a follow-up
// encoder before writing to disk.
enum class AudioExportFormat {
    WAV_RAW,   // XMA2-encoded bytes verbatim — same as dump_wav_files()
    WAV_PCM,   // XMA2 → 16-bit PCM RIFF/WAVE (plays in any external tool)
    MP3,       // XMA2 → PCM → MP3 (encoder not yet wired — stub)
    AAC,       // XMA2 → PCM → AAC (encoder not yet wired — stub)
};

// Decoded variant of dump_wav_files. Walks S.all_wav_files and writes
// every audio file as `fmt` to
//   `${S.export_dir}/<asset_path>.<ext>`
// preserving the BNK's internal directory layout (which already has
// `audio/...` for game audio, so each format's files land naturally
// under the audio tree). Worker thread + progress modal so the UI
// stays responsive on a multi-thousand-file batch.
//
// `WAV_RAW` short-circuits to dump_wav_files() — same bytes, same
// output path, no decode round-trip. The other variants run each
// entry through `XmaDecoder::decode_xma_to_pcm` and then write
// either a PCM RIFF/WAVE (`WAV_PCM`) or, when an encoder is wired
// up later, an MP3/AAC stream.
void dump_wav_files_as(AudioExportFormat fmt);

// Dump every file from every BNK indexed by the asset browser
// (top-level + nested). No reconstruction — paired bodies / headers
// land in distinct per-BNK subdirectories so they don't overwrite
// each other:
//   `${export_dir}/<bnk_stem>/<asset_path>`
// Use this when you want the raw archive contents side-by-side; use
// dump_mdl_files / dump_tex_files for decoder-ready reassembled
// buffers.
void dump_bnk_contents();

} // namespace ISO
