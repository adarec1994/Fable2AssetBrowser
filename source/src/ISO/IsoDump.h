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

} // namespace ISO
