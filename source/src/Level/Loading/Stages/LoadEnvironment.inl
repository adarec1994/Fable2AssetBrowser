            {
                std::vector<const std::vector<uint8_t>*> water_theme_gdbs;
                water_theme_gdbs.reserve(1 + supplemental_gdbs.size());
                water_theme_gdbs.push_back(&gdb_bytes);
                  for (const auto& db : supplemental_gdbs) {
                      water_theme_gdbs.push_back(&db.bytes);
                  }

                  auto colour_text = [](const float (&c)[3]) {
                      std::ostringstream ss;
                      ss << std::fixed << std::setprecision(3)
                         << c[0] << ',' << c[1] << ',' << c[2];
                      return ss.str();
                  };

                  Gdb::WaterTheme water_theme;
                  if (Gdb::ExtractWaterTheme(water_theme_gdbs, water_theme)) {
                      g_pending_level_water_theme = water_theme;
                      std::ostringstream ss;
                      ss << "water theme: GDB env params found";
                    if (water_theme.has_shallow_colour) {
                        ss << " shallow=("
                           << colour_text(water_theme.shallow_colour) << ')';
                    }
                    if (water_theme.has_deep_colour) {
                        ss << " deep=("
                           << colour_text(water_theme.deep_colour) << ')';
                    }
                    if (water_theme.source_time_of_day >= 0.0f) {
                        ss << " time="
                           << std::fixed << std::setprecision(3)
                           << water_theme.source_time_of_day;
                    }
                    OutputLog::success(ss.str());
                } else {
                    g_pending_level_water_theme = Gdb::WaterTheme{};
                      OutputLog::info(
                          "water theme: no GDB environment water params found; "
                          "using shader fallback");
                  }

                  Gdb::SkyTheme sky_theme;
                  if (Gdb::ExtractSkyTheme(water_theme_gdbs, sky_theme)) {
                      g_pending_level_sky_theme = sky_theme;
                      std::ostringstream ss;
                      ss << "sky theme: GDB env params found";
                      if (sky_theme.has_sky_colour) {
                          ss << " sky=("
                             << colour_text(sky_theme.sky_colour) << ')';
                      }
                      if (sky_theme.has_fog_colour) {
                          ss << " fog=("
                             << colour_text(sky_theme.fog_colour) << ')';
                      }
                      if (sky_theme.source_time_of_day >= 0.0f) {
                          ss << " time="
                             << std::fixed << std::setprecision(3)
                             << sky_theme.source_time_of_day;
                      }
                      OutputLog::success(ss.str());
                  } else {
                      g_pending_level_sky_theme = Gdb::SkyTheme{};
                      OutputLog::info(
                          "sky theme: no GDB environment sky params found; "
                          "using preview fallback");
                  }

                  Gdb::CloudTheme cloud_theme;
                  if (Gdb::ExtractCloudTheme(water_theme_gdbs,
                                             cloud_theme)) {
                      g_pending_level_cloud_theme = cloud_theme;
                      std::ostringstream ss;
                      ss << "cloud theme: GDB env params found layers="
                         << cloud_theme.layer_count;
                      if (cloud_theme.layer_count > 0) {
                          const auto& layer = cloud_theme.layers[0];
                          ss << " first=(height="
                             << std::fixed << std::setprecision(1)
                             << layer.height
                             << ", transparency="
                             << std::setprecision(2)
                             << layer.transparency << ')';
                      }
                      if (cloud_theme.source_time_of_day >= 0.0f) {
                          ss << " time="
                             << std::fixed << std::setprecision(3)
                             << cloud_theme.source_time_of_day;
                      }
                      OutputLog::success(ss.str());
                  } else {
                      g_pending_level_cloud_theme = Gdb::CloudTheme{};
                      OutputLog::info(
                          "cloud theme: no GDB environment cloud params found; "
                          "using clear sky");
                  }

                  Gdb::WeatherTheme weather_theme;
                  if (Gdb::ExtractWeatherTheme(water_theme_gdbs,
                                               weather_theme)) {
                      g_pending_level_weather_theme = weather_theme;
                      std::ostringstream ss;
                      ss << "weather theme: GDB env params found";
                      if (weather_theme.has_rain) {
                          ss << " rain(density="
                             << std::fixed << std::setprecision(3)
                             << weather_theme.rain_density
                             << ", size=" << weather_theme.rain_size << ')';
                      }
                      if (weather_theme.has_snow) {
                          ss << " snow(fallspeed="
                             << std::fixed << std::setprecision(3)
                             << weather_theme.snow_fall_speed
                             << ", size=" << weather_theme.snow_size << ')';
                      }
                      if (weather_theme.has_wind) {
                          ss << " wind(strength="
                             << std::fixed << std::setprecision(2)
                             << weather_theme.wind_strength_min << ".."
                             << weather_theme.wind_strength_max << ')';
                      }
                      if (weather_theme.has_ground_mist) {
                          ss << " mist(strength="
                             << std::fixed << std::setprecision(2)
                             << weather_theme.ground_mist_strength << ')';
                      }
                      OutputLog::success(ss.str());
                  } else {
                      g_pending_level_weather_theme = Gdb::WeatherTheme{};
                      OutputLog::info(
                          "weather theme: no GDB environment weather params "
                          "found; weather effects idle");
                  }

                  Gdb::EnvironmentThemeTimeline env_timeline;
                  if (Gdb::ExtractEnvironmentThemeTimeline(
                          water_theme_gdbs, env_timeline)) {
                      g_pending_level_environment_timeline = env_timeline;
                      std::ostringstream ss;
                      ss << "day/night cycle: GDB env day-set found keyframes="
                         << env_timeline.keyframes.size();
                      if (!env_timeline.keyframes.empty()) {
                          ss << " span=["
                             << std::fixed << std::setprecision(3)
                             << env_timeline.keyframes.front().time_of_day
                             << ".."
                             << env_timeline.keyframes.back().time_of_day
                             << "]";
                      }
                      OutputLog::success(ss.str());
                  } else {
                      g_pending_level_environment_timeline =
                          Gdb::EnvironmentThemeTimeline{};
                      OutputLog::info(
                          "day/night cycle: no multi-keyframe GDB day-set; "
                          "using fixed environment theme");
                  }
              }
