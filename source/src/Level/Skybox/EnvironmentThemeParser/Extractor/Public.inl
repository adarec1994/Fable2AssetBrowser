    explicit EnvironmentThemeExtractor(
        const std::vector<const std::vector<uint8_t>*>& gdbs)
    {
        views_.reserve(gdbs.size());
        for (const auto* bytes : gdbs) {
            if (!bytes) continue;
            views_.emplace_back(*bytes);
        }
    }

    bool extract(WaterTheme& out_theme) const
    {
        out_theme = WaterTheme{};
        if (views_.empty()) return false;

        bool applied = false;

        WaterThemeRecordRef day_set = findDaySet(true);
        if (day_set.valid) {
            WaterThemeRecordRef day_theme;
            float day_time = -1.0f;
            if (selectThemeFromDaySet(day_set, day_theme, day_time)) {
                out_theme.source_time_of_day = day_time;
                applied |= applyThemeRecord(day_theme, out_theme);
            }
        }

        if (!applied) {
            for (const WaterThemeRecordRef& level :
                 lookupAllInDb(kHashLevelData, 0)) {
                applied |= applyThemeField(level, kHashEnvThemeGlobal,
                                           out_theme);
            }
        }

        if (!applied) {
            WaterThemeRecordRef owner =
                firstRecordWithFieldInDb(kHashEnvThemeGlobal, 0);
            if (owner.valid) {
                applied |= applyThemeField(owner, kHashEnvThemeGlobal,
                                           out_theme);
            }
        }

        if (!applied) {
            for (const WaterThemeRecordRef& global :
                 lookupAllInDb(kHashEnvThemeGlobal, 0)) {
                applied |= applyThemeRecord(global, out_theme);
            }
        }

        if (!applied) {
            day_set = findDaySet(false);
            WaterThemeRecordRef day_theme;
            float day_time = -1.0f;
            if (day_set.valid &&
                selectThemeFromDaySet(day_set, day_theme, day_time)) {
                out_theme.source_time_of_day = day_time;
                applied |= applyThemeRecord(day_theme, out_theme);
            }
        }

        if (!applied) {
            for (const WaterThemeRecordRef& global :
                 lookupAll(kHashEnvThemeGlobal)) {
                applied |= applyThemeRecord(global, out_theme);
            }
        }

        if (!applied) {
            applied = applyFirstWaterLikeRecord(out_theme);
        }

        out_theme.has_any = applied;
        return applied;
    }

    bool extractSky(SkyTheme& out_theme) const
    {
        out_theme = SkyTheme{};
        if (views_.empty()) return false;

        bool applied = false;

        WaterThemeRecordRef day_set = findDaySet(true);
        if (day_set.valid) {
            WaterThemeRecordRef day_theme;
            float day_time = -1.0f;
            if (selectThemeFromDaySet(day_set, day_theme, day_time)) {
                out_theme.source_time_of_day = day_time;
                applied |= applySkyThemeRecord(day_theme, out_theme);
            }
        }

        if (!applied) {
            for (const WaterThemeRecordRef& level :
                 lookupAllInDb(kHashLevelData, 0)) {
                applied |= applySkyThemeField(level, kHashEnvThemeGlobal,
                                              out_theme);
            }
        }

        if (!applied) {
            WaterThemeRecordRef owner =
                firstRecordWithFieldInDb(kHashEnvThemeGlobal, 0);
            if (owner.valid) {
                applied |= applySkyThemeField(owner, kHashEnvThemeGlobal,
                                              out_theme);
            }
        }

        if (!applied) {
            for (const WaterThemeRecordRef& global :
                 lookupAllInDb(kHashEnvThemeGlobal, 0)) {
                applied |= applySkyThemeRecord(global, out_theme);
            }
        }

        if (!applied) {
            day_set = findDaySet(false);
            WaterThemeRecordRef day_theme;
            float day_time = -1.0f;
            if (day_set.valid &&
                selectThemeFromDaySet(day_set, day_theme, day_time)) {
                out_theme.source_time_of_day = day_time;
                applied |= applySkyThemeRecord(day_theme, out_theme);
            }
        }

        if (!applied) {
            for (const WaterThemeRecordRef& global :
                 lookupAll(kHashEnvThemeGlobal)) {
                applied |= applySkyThemeRecord(global, out_theme);
            }
        }

        if (!applied) {
            applied = applyFirstSkyLikeRecord(out_theme);
        }

        out_theme.has_any = applied;
        return applied;
    }

    bool extractWeather(WeatherTheme& out_theme) const
    {
        out_theme = WeatherTheme{};
        if (views_.empty()) return false;

        bool applied = false;

        WaterThemeRecordRef day_set = findDaySet(true);
        if (day_set.valid) {
            WaterThemeRecordRef day_theme;
            float day_time = -1.0f;
            if (selectThemeFromDaySet(day_set, day_theme, day_time)) {
                out_theme.source_time_of_day = day_time;
                applied |= applyWeatherThemeRecord(day_theme, out_theme);
            }
        }

        if (!applied) {
            for (const WaterThemeRecordRef& level :
                 lookupAllInDb(kHashLevelData, 0)) {
                applied |= applyWeatherThemeField(level, kHashEnvThemeGlobal,
                                                  out_theme);
            }
        }

        if (!applied) {
            WaterThemeRecordRef owner =
                firstRecordWithFieldInDb(kHashEnvThemeGlobal, 0);
            if (owner.valid) {
                applied |= applyWeatherThemeField(owner, kHashEnvThemeGlobal,
                                                  out_theme);
            }
        }

        if (!applied) {
            for (const WaterThemeRecordRef& global :
                 lookupAllInDb(kHashEnvThemeGlobal, 0)) {
                applied |= applyWeatherThemeRecord(global, out_theme);
            }
        }

        if (!applied) {
            day_set = findDaySet(false);
            WaterThemeRecordRef day_theme;
            float day_time = -1.0f;
            if (day_set.valid &&
                selectThemeFromDaySet(day_set, day_theme, day_time)) {
                out_theme.source_time_of_day = day_time;
                applied |= applyWeatherThemeRecord(day_theme, out_theme);
            }
        }

        if (!applied) {
            for (const WaterThemeRecordRef& global :
                 lookupAll(kHashEnvThemeGlobal)) {
                applied |= applyWeatherThemeRecord(global, out_theme);
            }
        }

        out_theme.has_any = applied;
        return applied;
    }

    bool extractClouds(CloudTheme& out_theme) const
    {
        out_theme = CloudTheme{};
        if (views_.empty()) return false;

        bool applied = false;

        WaterThemeRecordRef day_set = findDaySet(true);
        if (day_set.valid) {
            WaterThemeRecordRef day_theme;
            float day_time = -1.0f;
            if (selectThemeFromDaySet(day_set, day_theme, day_time)) {
                out_theme.source_time_of_day = day_time;
                applied |= applyCloudThemeRecord(day_theme, out_theme);
            }
        }

        if (!applied) {
            for (const WaterThemeRecordRef& level :
                 lookupAllInDb(kHashLevelData, 0)) {
                applied |= applyCloudThemeField(level, kHashEnvThemeGlobal,
                                                out_theme);
            }
        }

        if (!applied) {
            WaterThemeRecordRef owner =
                firstRecordWithFieldInDb(kHashEnvThemeGlobal, 0);
            if (owner.valid) {
                applied |= applyCloudThemeField(owner, kHashEnvThemeGlobal,
                                                out_theme);
            }
        }

        if (!applied) {
            for (const WaterThemeRecordRef& global :
                 lookupAllInDb(kHashEnvThemeGlobal, 0)) {
                applied |= applyCloudThemeRecord(global, out_theme);
            }
        }

        if (!applied) {
            day_set = findDaySet(false);
            WaterThemeRecordRef day_theme;
            float day_time = -1.0f;
            if (day_set.valid &&
                selectThemeFromDaySet(day_set, day_theme, day_time)) {
                out_theme.source_time_of_day = day_time;
                applied |= applyCloudThemeRecord(day_theme, out_theme);
            }
        }

        if (!applied) {
            for (const WaterThemeRecordRef& global :
                 lookupAll(kHashEnvThemeGlobal)) {
                applied |= applyCloudThemeRecord(global, out_theme);
            }
        }

        if (!applied) {
            WaterThemeRecordRef clouds = firstCloudContainer();
            if (clouds.valid) {
                applied |= applyCloudThemeRecord(clouds, out_theme);
            }
        }

        finaliseCloudTheme(out_theme);
        out_theme.has_any = applied && out_theme.layer_count > 0;
        return out_theme.has_any;
    }

    bool extractEnvironmentTimeline(EnvironmentThemeTimeline& out_timeline)
        const
    {
        out_timeline = EnvironmentThemeTimeline{};
        if (views_.empty()) return false;

        WaterThemeRecordRef day_set = findDaySet(true);
        if (!day_set.valid) {
            day_set = findDaySet(false);
        }
#ifdef FABLE_THEME_PARSER_TRACE
        std::fprintf(stderr, "[timeline] day_set valid=%d db=%zu\n",
                     day_set.valid ? 1 : 0, day_set.db);
#endif
        if (!day_set.valid) return false;

        const GdbView& v = view(day_set);
        size_t sch = 0;
        uint32_t n = 0;
        if (!v.schema(day_set.record, sch, n)) return false;
#ifdef FABLE_THEME_PARSER_TRACE
        std::fprintf(stderr, "[timeline] fields=%u\n", n);
#endif

        const size_t descs = sch + 4 + size_t(n) * 4;
        if (descs + size_t(n) * 4 > v.hash_base) return false;

        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t desc =
                ReadBeU32(v.bytes.data() + descs + size_t(i) * 4);
            const uint8_t type = uint8_t(desc >> 24);
            if (type != 4 && type != 6 && type != 7) continue;

            const size_t slot = day_set.record + 4 + size_t(i) * 4;
            if (slot + 4 > v.body_end) continue;

            WaterThemeFieldRef item_field;
            item_field.owner = day_set;
            item_field.slot = slot;
            item_field.type = type;
            item_field.raw = ReadBeU32(v.bytes.data() + slot);
            item_field.f32 = ReadBeF32(v.bytes.data() + slot);

            WaterThemeRecordRef entry = fieldToRecord(item_field);
            if (!entry.valid) continue;

            WaterThemeRecordRef theme =
                resolveRecordField(entry, kHashTheme);
            if (!theme.valid) continue;

            float time = 0.5f;
            float read_time = 0.0f;
            if (readFloat(entry, kHashTimeOfDay, read_time)) {
                time = normalizeTimeOfDay(read_time);
            }

            EnvironmentThemeKeyframe key;
            key.time_of_day = time;

            const bool water_applied = applyThemeRecord(theme, key.water);
            key.water.has_any = water_applied;
            key.water.source_time_of_day = time;

            const bool sky_applied = applySkyThemeRecord(theme, key.sky);
            key.sky.has_any = sky_applied;
            key.sky.source_time_of_day = time;

            const bool cloud_applied =
                applyCloudThemeRecord(theme, key.clouds);
            finaliseCloudTheme(key.clouds);
            key.clouds.has_any =
                cloud_applied && key.clouds.layer_count > 0;
            key.clouds.source_time_of_day = time;

            const bool weather_applied =
                applyWeatherThemeRecord(theme, key.weather);
            key.weather.has_any = weather_applied;
            key.weather.source_time_of_day = time;

            if (key.water.has_any || key.sky.has_any ||
                key.clouds.has_any || key.weather.has_any) {
                out_timeline.keyframes.push_back(key);
            }
        }

        std::sort(out_timeline.keyframes.begin(),
                  out_timeline.keyframes.end(),
                  [](const EnvironmentThemeKeyframe& a,
                     const EnvironmentThemeKeyframe& b) {
                      return a.time_of_day < b.time_of_day;
                  });
        out_timeline.keyframes.erase(
            std::unique(out_timeline.keyframes.begin(),
                        out_timeline.keyframes.end(),
                        [](const EnvironmentThemeKeyframe& a,
                           const EnvironmentThemeKeyframe& b) {
                            return std::fabs(a.time_of_day -
                                             b.time_of_day) < 0.0005f;
                        }),
            out_timeline.keyframes.end());

        out_timeline.has_any = out_timeline.keyframes.size() >= 2;
        return out_timeline.has_any;
    }

private:
