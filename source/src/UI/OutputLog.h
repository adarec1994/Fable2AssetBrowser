#pragma once

#include <string>

namespace OutputLog {

enum class Level { Info, Success, Warn, Error };

void log    (Level lvl, std::string msg);
inline void info   (std::string m) { log(Level::Info,    std::move(m)); }
inline void success(std::string m) { log(Level::Success, std::move(m)); }
inline void warn   (std::string m) { log(Level::Warn,    std::move(m)); }
inline void error  (std::string m) { log(Level::Error,   std::move(m)); }

void draw();

float reserved_bottom_height();

}
