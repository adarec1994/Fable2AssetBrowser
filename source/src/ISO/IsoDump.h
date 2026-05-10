#pragma once
// IsoDump — bulk raw-extract every loaded asset out of a mounted ISO
// into the user's configured export directory.
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

// Dump every .tex file the asset browser has scanned (S.all_tex_files).
// Same shape as dump_mdl_files, but with the third-component twist that
// textures need a global `1024mip0_textures.bnk` lookup in addition to
// the paired `_texture_headers.bnk`. The dumped buffer is the same
// header + mip0 + body concatenation `build_tex_buffer_for_name`
// produces for the in-app preview, so a downstream decoder can ingest
// it without any awareness that it came from three BNKs.
void dump_tex_files();

// Dump every .wav file the asset browser has scanned (S.all_wav_files).
// Wavs aren't split — this is just a per-BNK extract loop, no
// reconstruction. Faster than the model/texture dumps because there's
// no pair lookup or memory concat: the extract streams straight to
// disk under the export root, preserving the source path.
void dump_wav_files();

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
