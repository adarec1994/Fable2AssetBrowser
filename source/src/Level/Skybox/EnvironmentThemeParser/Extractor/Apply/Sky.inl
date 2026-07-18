    bool applySkyRecord(WaterThemeRecordRef sky,
                        SkyTheme& theme) const
    {
        if (!sky.valid) return false;

        bool any = false;
        float colour[3] = {};
        if (readColourRecordField(sky, kHashSkyColour, colour) ||
            readFlatColour(sky, kHashSkyColourRed, kHashSkyColourGreen,
                           kHashSkyColourBlue, kHashSkyColourFactor,
                           colour)) {
            std::copy(std::begin(colour), std::end(colour),
                      std::begin(theme.sky_colour));
            theme.has_sky_colour = true;
            any = true;
        }
        if (readColourRecordField(sky, kHashSkyComplementaryColour,
                                  colour) ||
            readFlatColour(sky, kHashSkyComplementaryColourRed,
                           kHashSkyComplementaryColourGreen,
                           kHashSkyComplementaryColourBlue,
                           kHashSkyComplementaryColourFactor, colour)) {
            std::copy(std::begin(colour), std::end(colour),
                      std::begin(theme.complementary_colour));
            theme.has_complementary_colour = true;
            any = true;
        }
        if (readColourRecordField(sky, kHashSunsetColour, colour) ||
            readFlatColour(sky, kHashSunsetColourRed,
                           kHashSunsetColourGreen, kHashSunsetColourBlue,
                           kHashSunsetColourFactor, colour)) {
            std::copy(std::begin(colour), std::end(colour),
                      std::begin(theme.sunset_colour));
            theme.has_sunset_colour = true;
            any = true;
        }

        auto read_param = [&](uint32_t hash, bool& flag, float& dst) {
            float v = 0.0f;
            if (readFloat(sky, hash, v)) {
                flag = true;
                dst = v;
                any = true;
            }
        };

        read_param(kHashSunIntensity, theme.has_sun_intensity,
                   theme.sun_intensity);
        read_param(kHashSkyBetaRayleighMultiplier, theme.has_rayleigh,
                   theme.rayleigh);
        read_param(kHashSkyBetaMieMultiplier, theme.has_mie,
                   theme.mie);
        read_param(kHashSkyComplementaryColourBias,
                   theme.has_complementary_bias,
                   theme.complementary_bias);
        read_param(kHashSunAxisElevation, theme.has_sun_axis,
                   theme.sun_axis_elevation);
        read_param(kHashSunAxisZOffset, theme.has_sun_axis,
                   theme.sun_axis_z_offset);
        read_param(kHashSunAxisXYRotationOffset, theme.has_sun_axis,
                   theme.sun_axis_xy_rotation);
        {
            float tf = 1.0f;
            if (readFloat(sky, kHashTimeFactor, tf) &&
                std::isfinite(tf) && tf > 0.0f) {
                theme.main_light_time_factor = tf;
            }
        }

        auto read_texture = [&](uint32_t hash, bool& flag, uint32_t& dst) {
            WaterThemeFieldRef field;
            if (findField(sky, hash, 0xFF, field) &&
                field.raw != 0 && field.raw != kHashNull) {
                flag = true;
                dst = field.raw;
                any = true;
            }
        };
        read_texture(kHashSkyOverlayTexture,
                     theme.has_sky_overlay_texture,
                     theme.sky_overlay_texture_hash);
        read_texture(kHashMoonTexture,
                     theme.has_moon_texture,
                     theme.moon_texture_hash);
        read_texture(kHashMoonGlareTexture,
                     theme.has_moon_glare_texture,
                     theme.moon_glare_texture_hash);
        read_texture(kHashDiscTexture,
                     theme.has_sun_disc_texture,
                     theme.sun_disc_texture_hash);
        read_texture(kHashSunBeamsTexture,
                     theme.has_sun_beams_texture,
                     theme.sun_beams_texture_hash);
        read_texture(kHashGlareTexture,
                     theme.has_sun_glare_texture,
                     theme.sun_glare_texture_hash);

        read_param(kHashMoonIntensity, theme.has_moon_params,
                   theme.moon_intensity);
        read_param(kHashMoonSize, theme.has_moon_params,
                   theme.moon_size);
        read_param(kHashMoonGlareIntensity, theme.has_moon_params,
                   theme.moon_glare_intensity);
        read_param(kHashMoonGlareSize, theme.has_moon_params,
                   theme.moon_glare_size);
        read_param(kHashMoonTransparency, theme.has_moon_params,
                   theme.moon_transparency);
        read_param(kHashMoonAxisElevation, theme.has_moon_axis,
                   theme.moon_axis_elevation);
        read_param(kHashMoonAxisZOffset, theme.has_moon_axis,
                   theme.moon_axis_z_offset);
        read_param(kHashMoonAxisXYRotationOffset, theme.has_moon_axis,
                   theme.moon_axis_xy_rotation);
        read_param(kHashStarBrightness, theme.has_star_brightness,
                   theme.star_brightness);

        return any;
    }

    bool applySunRecord(WaterThemeRecordRef sun, SkyTheme& theme) const
    {
        if (!sun.valid) return false;

        bool any = false;
        auto read_param = [&](uint32_t hash, bool& flag, float& dst) {
            float v = 0.0f;
            if (readFloat(sun, hash, v)) {
                flag = true;
                dst = v;
                any = true;
            }
        };
        read_param(kHashDiscSize, theme.has_sun_disc_params,
                   theme.sun_disc_size);
        read_param(kHashDiscIntensity, theme.has_sun_disc_params,
                   theme.sun_disc_intensity);
        float colour[3] = {};
        if (readColourRecordField(sun, kHashDiscColour, colour) ||
            readFlatColour(sun, kHashDiscColourRed, kHashDiscColourGreen,
                           kHashDiscColourBlue, kHashDiscColourFactor,
                           colour)) {
            std::copy(std::begin(colour), std::end(colour),
                      std::begin(theme.sun_disc_colour));
            theme.has_sun_disc_colour = true;
            any = true;
        }
        read_param(kHashSunBeamsWidth, theme.has_sun_beams,
                   theme.sun_beams_width);
        read_param(kHashSunBeamsHeight, theme.has_sun_beams,
                   theme.sun_beams_height);
        read_param(kHashSunBeamsIntensity, theme.has_sun_beams,
                   theme.sun_beams_intensity);
        read_param(kHashGlareIntensity, theme.has_sun_glare,
                   theme.sun_glare_intensity);
        read_param(kHashGlareSize, theme.has_sun_glare,
                   theme.sun_glare_size);
        auto read_texture = [&](uint32_t hash, bool& flag, uint32_t& dst) {
            WaterThemeFieldRef field;
            if (findField(sun, hash, 0xFF, field) &&
                field.raw != 0 && field.raw != kHashNull) {
                flag = true;
                dst = field.raw;
                any = true;
            }
        };
        read_texture(kHashDiscTexture,
                     theme.has_sun_disc_texture,
                     theme.sun_disc_texture_hash);
        read_texture(kHashSunBeamsTexture,
                     theme.has_sun_beams_texture,
                     theme.sun_beams_texture_hash);
        read_texture(kHashGlareTexture,
                     theme.has_sun_glare_texture,
                     theme.sun_glare_texture_hash);
        return any;
    }

    bool applyFogRecord(WaterThemeRecordRef fog,
                        SkyTheme& theme) const
    {
        if (!fog.valid) return false;

        bool any = false;
        float colour[3] = {};
        if (readColourRecordField(fog, kHashCloseFogColour, colour) ||
            readFlatColour(fog, kHashCloseFogColourRed,
                           kHashCloseFogColourGreen,
                           kHashCloseFogColourBlue,
                           kHashCloseFogColourFactor, colour)) {
            std::copy(std::begin(colour), std::end(colour),
                      std::begin(theme.fog_colour));
            theme.has_fog_colour = true;
            any = true;
        }

        float v = 0.0f;
        if (readFloat(fog, kHashNearDistance, v)) {
            theme.near_distance = v;
            theme.has_near_fog = true;
            any = true;
        }
        if (readFloat(fog, kHashNearDensity, v)) {
            theme.near_density = v;
            theme.has_near_fog = true;
            any = true;
        }
        if (readFloat(fog, kHashFarDistance, v)) {
            theme.far_distance = v;
            theme.has_far_fog = true;
            any = true;
        }
        if (readFloat(fog, kHashFarDensity, v)) {
            theme.far_density = v;
            theme.has_far_fog = true;
            any = true;
        }
        if (readFloat(fog, kHashCloseFogMaxDistance, v)) {
            theme.close_fog_max_distance = v;
            any = true;
        }
        if (readFloat(fog, kHashFoggingStart, v)) {
            theme.fogging_start = v;
            theme.has_fogging_start = true;
            any = true;
        }

        return any;
    }

    bool applySkyThemeRecord(WaterThemeRecordRef theme_record,
                             SkyTheme& theme) const
    {
        if (!theme_record.valid) return false;

        bool any = false;
        WaterThemeRecordRef sky =
            resolveRecordField(theme_record, kHashSky);
        if (sky.valid) {
            any |= applySkyRecord(sky, theme);
        } else {
            any |= applySkyRecord(theme_record, theme);
        }

        WaterThemeRecordRef fog =
            resolveRecordField(theme_record, kHashFogging);
        if (fog.valid) {
            any |= applyFogRecord(fog, theme);
        } else {
            any |= applyFogRecord(theme_record, theme);
        }

        WaterThemeRecordRef sun =
            resolveRecordField(theme_record, kHashSunRecord);
        if (sun.valid) {
            any |= applySunRecord(sun, theme);
        } else if (sky.valid) {
            any |= applySunRecord(sky, theme);
        }

        WaterThemeRecordRef lighting =
            resolveRecordField(theme_record, kHashLighting);
        if (lighting.valid) {
            float colour[3] = {};
            if (readColourRecordField(lighting, kHashMainLightColour,
                                      colour) ||
                readFlatColour(lighting, kHashMainLightColourRed,
                               kHashMainLightColourGreen,
                               kHashMainLightColourBlue,
                               kHashMainLightColourFactor, colour)) {
                std::copy(std::begin(colour), std::end(colour),
                          std::begin(theme.main_light_colour));
                theme.has_main_light_colour = true;
                any = true;
            }
        }

        return any;
    }

    bool applySkyThemeField(WaterThemeRecordRef owner,
                            uint32_t field_hash,
                            SkyTheme& theme) const
    {
        WaterThemeRecordRef target = resolveRecordField(owner, field_hash);
        return target.valid && applySkyThemeRecord(target, theme);
    }
