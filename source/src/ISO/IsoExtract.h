// Minimal XDVDFS (Xbox / Xbox 360 disc image) extractor. Reads an Xbox
// .iso file in any of the four common layouts (raw / XGD1 / XGD2 / XGD3)
// and writes every contained file out to a destination directory,
// preserving the on-disc directory structure.
//
// Reference for the format: extract-xiso (in@fishtank.com), 2003.
// We don't link the original C source — implementing just the read +
// extract path in C++ turned out to be ~150 lines and avoids dragging
// in POSIX-isms like chdir/getopt.

#pragma once

#include <string>
#include <functional>

namespace ISO {

// Extracts the ISO at `iso_path` into the directory at `dest_dir`. Creates
// the destination if it doesn't exist. Returns false on failure with a
// human-readable error in *err_out (if not null).
//
// `progress` (optional) is called periodically with (bytes_done, bytes_total)
// where bytes_total is the size of the ISO file. It's called from the
// extraction thread, so it must be safe to invoke from there.
bool extract_iso(const std::string& iso_path,
                 const std::string& dest_dir,
                 std::string* err_out = nullptr,
                 std::function<void(uint64_t, uint64_t)> progress = nullptr);

// Quick check: is the given file an Xbox/Xbox360 ISO we can read? Returns
// true if a valid XDVDFS volume descriptor is found at any of the known
// offsets. Doesn't extract anything.
bool is_xbox_iso(const std::string& iso_path);

} // namespace ISO
