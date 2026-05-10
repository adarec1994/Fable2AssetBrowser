// About window — fixed-size popup showing the app's logo, a short
// description, and a small key/value list (version, authors, source
// code link). Modeled after the TT Lab "About TT Lab" dialog the user
// referenced as a visual target.
//
// The contents are intentionally configured by editing a single
// constants block at the top of AboutWindow.cpp — no JSON, no INI, no
// extra plumbing. To change the description / version / authors / URL
// edit `kAboutDescription`, `kAboutEntries`, etc. To change the image,
// drop a new PNG at `include/image/about_image.png` and rebuild (the
// image is pulled in via fable.rc as RCDATA).

#pragma once

#ifdef _WIN32
struct ID3D11Device;
#endif

namespace About {

// Mark the window as "should be open" — UI_Main calls this from the
// menu item. Calling it again while the window is already open is a
// no-op (the existing instance just stays focused).
void open();

// Draw one frame of the about window if it's open. Pulls the image
// lazily on first show so we don't pay the cost at startup. Safe to
// call every frame — does nothing when closed.
#ifdef _WIN32
void draw(ID3D11Device* device);
#else
void draw();
#endif

// Release the GPU image resource. Called at process teardown so D3D
// doesn't leak the SRV across the device shutdown. Safe to call
// repeatedly / when nothing's allocated.
void release_resources();

} // namespace About
