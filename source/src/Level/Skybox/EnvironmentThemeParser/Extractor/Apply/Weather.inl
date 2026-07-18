    bool applyWeatherSkyRecord(WaterThemeRecordRef sky,
                               WeatherTheme& theme) const
    {
        if (!sky.valid) return false;

        bool any = false;
        auto read_param = [&](uint32_t hash, bool& flag, float& dst) {
            float v = 0.0f;
            if (readFloat(sky, hash, v)) {
                flag = true;
                dst = v;
                any = true;
            }
        };
        read_param(kHashRainDensity, theme.has_rain, theme.rain_density);
        read_param(kHashRainSize, theme.has_rain, theme.rain_size);
        read_param(kHashSnowFallSpeed, theme.has_snow,
                   theme.snow_fall_speed);
        read_param(kHashSnowSize, theme.has_snow, theme.snow_size);
        return any;
    }

    bool applyWeatherWindRecord(WaterThemeRecordRef wind,
                                WeatherTheme& theme) const
    {
        if (!wind.valid) return false;

        bool any = false;
        auto read_param = [&](uint32_t hash, float& dst) {
            float v = 0.0f;
            if (readFloat(wind, hash, v)) {
                theme.has_wind = true;
                dst = v;
                any = true;
            }
        };
        read_param(kHashWindStrengthMin, theme.wind_strength_min);
        read_param(kHashWindStrengthMax, theme.wind_strength_max);
        read_param(kHashWindStrengthVariation,
                   theme.wind_strength_variation);
        read_param(kHashWindXYRotationMin, theme.wind_xy_rotation_min);
        read_param(kHashWindXYRotationMax, theme.wind_xy_rotation_max);
        read_param(kHashWindElevationMin, theme.wind_elevation_min);
        read_param(kHashWindElevationMax, theme.wind_elevation_max);
        read_param(kHashWindChangeFrequency, theme.wind_change_frequency);
        read_param(kHashWindChangeDuration, theme.wind_change_duration);
        read_param(kHashWindDirectionVariation,
                   theme.wind_direction_variation);
        return any;
    }

    bool applyWeatherMistRecord(WaterThemeRecordRef mist,
                                WeatherTheme& theme) const
    {
        if (!mist.valid) return false;

        float v = 0.0f;
        if (readFloat(mist, kHashStrength, v)) {
            theme.has_ground_mist = true;
            theme.ground_mist_strength = v;
            return true;
        }
        return false;
    }

    bool applyWeatherThemeRecord(WaterThemeRecordRef theme_record,
                                 WeatherTheme& theme) const
    {
        if (!theme_record.valid) return false;

        bool any = false;
        WaterThemeRecordRef sky =
            resolveRecordField(theme_record, kHashSky);
        if (sky.valid) {
            any |= applyWeatherSkyRecord(sky, theme);
        } else {
            any |= applyWeatherSkyRecord(theme_record, theme);
        }

        WaterThemeRecordRef wind =
            resolveRecordField(theme_record, kHashWind);
        if (wind.valid) {
            any |= applyWeatherWindRecord(wind, theme);
        } else {
            any |= applyWeatherWindRecord(theme_record, theme);
        }

        WaterThemeRecordRef mist =
            resolveRecordField(theme_record, kHashGroundMist);
        if (mist.valid) {
            any |= applyWeatherMistRecord(mist, theme);
        }
        return any;
    }

    bool applyWeatherThemeField(WaterThemeRecordRef owner,
                                uint32_t field_hash,
                                WeatherTheme& theme) const
    {
        WaterThemeRecordRef target = resolveRecordField(owner, field_hash);
        return target.valid && applyWeatherThemeRecord(target, theme);
    }
