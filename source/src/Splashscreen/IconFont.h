// Loads Font Awesome 6 Solid into ImGui as a merged icon font, using
// the .ttf bytes embedded in the exe (IDR_FA_FONT). Call once after
// ImGui_ImplDX11_Init and before any frame uses icons.

#pragma once

namespace IconFont {

// Idempotent — safe to call multiple times. Returns true once the font
// is available.
bool ensure_loaded();

} // namespace IconFont

// Glyph code points for icons we use. Pulled from IconFontCppHeaders'
// IconsFontAwesome6.h — duplicated here so we don't need to include the
// whole 100KB header just for two icons.
//
//   ICON_FA_VOLUME_HIGH  → 0xF028  (sound on)
//   ICON_FA_VOLUME_XMARK → 0xF6A9  (muted)
#define ICON_FA_VOLUME_HIGH   "\xef\x80\xa8"
#define ICON_FA_VOLUME_XMARK  "\xef\x9a\xa9"
