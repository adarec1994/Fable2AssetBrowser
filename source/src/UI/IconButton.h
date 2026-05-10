#pragma once
// Solid-circle FontAwesome icon button — identical shape to the audio
// player transport (Stop / Play / Pause / Loop). Used by anything that
// wants the same look-and-feel for play controls.
//
// `primary`: blue accent fill (use for the central Play/Pause button).
// `active`: blue glyph tint (use to indicate a toggled-on state, e.g.
// loop currently enabled).
// `optical_dx_pct`: horizontal nudge to compensate for asymmetric
// glyphs (the play triangle's visible centroid sits left of its
// advance-box centre, so the call site passes ~0.17 to put the optical
// centre on the button centre).

namespace UI {

bool icon_button(const char* id, const char* icon_glyph, float diameter,
                 bool primary, bool active = false,
                 float optical_dx_pct = 0.0f);

} // namespace UI
