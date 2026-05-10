#pragma once
// OutputLog — a session-wide rolling log of user-visible events
// (export successes, decode errors, etc.). Thread-safe; the public
// log_*() helpers can be called from worker threads.
//
// The UI is a slide-up panel pinned to the bottom of the main window.
// One always-visible 26 px strip with the "Output Log" title acts as
// the toggle: click → animates open, click again (or click outside the
// expanded panel) → animates closed. Levels are colour-coded, latest
// entries auto-scroll to view.

#include <string>

namespace OutputLog {

enum class Level { Info, Success, Warn, Error };

void log    (Level lvl, std::string msg);
inline void info   (std::string m) { log(Level::Info,    std::move(m)); }
inline void success(std::string m) { log(Level::Success, std::move(m)); }
inline void warn   (std::string m) { log(Level::Warn,    std::move(m)); }
inline void error  (std::string m) { log(Level::Error,   std::move(m)); }

// Render the panel + bottom strip. Call exactly once per frame from
// the main UI loop; always-visible.
void draw();

// Reserved height (in pixels) the bottom strip currently consumes.
// Other UI code can subtract this from the available work-area height
// so it doesn't get drawn behind the strip.
float reserved_bottom_height();

} // namespace OutputLog
