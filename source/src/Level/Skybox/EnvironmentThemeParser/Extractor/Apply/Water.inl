    bool applyWaterRecord(WaterThemeRecordRef water,
                          WaterTheme& theme) const
    {
        if (!water.valid) return false;

        bool any = false;
        float colour[3] = {};
        if (readColour(water,
                       kHashShallowWaterColourRed,
                       kHashShallowWaterColourGreen,
                       kHashShallowWaterColourBlue,
                       colour)) {
            std::copy(std::begin(colour), std::end(colour),
                      std::begin(theme.shallow_colour));
            theme.has_shallow_colour = true;
            any = true;
        }
        if (readColour(water,
                       kHashDeepWaterColourRed,
                       kHashDeepWaterColourGreen,
                       kHashDeepWaterColourBlue,
                       colour)) {
            std::copy(std::begin(colour), std::end(colour),
                      std::begin(theme.deep_colour));
            theme.has_deep_colour = true;
            any = true;
        }

        auto read_param = [&](uint32_t hash, bool& flag, float& dst) {
            float v = 0.0f;
            if (readFloat(water, hash, v)) {
                flag = true;
                dst = v;
                any = true;
            }
        };

        read_param(kHashEdgeBlendMin, theme.has_edge_blend_min,
                   theme.edge_blend_min);
        read_param(kHashEdgeBlendMax, theme.has_edge_blend_max,
                   theme.edge_blend_max);
        read_param(kHashEdgeBlendBias, theme.has_edge_blend_bias,
                   theme.edge_blend_bias);
        read_param(kHashMaxRefractionDistance,
                   theme.has_max_refraction_distance,
                   theme.max_refraction_distance);
        read_param(kHashFresnelBias, theme.has_fresnel_bias,
                   theme.fresnel_bias);
        read_param(kHashReflectionStrength,
                   theme.has_reflection_strength,
                   theme.reflection_strength);
        read_param(kHashRefractionScale, theme.has_refraction_scale,
                   theme.refraction_scale);
        read_param(kHashReflectionScale, theme.has_reflection_scale,
                   theme.reflection_scale);
        read_param(kHashReflectionBias, theme.has_reflection_bias,
                   theme.reflection_bias);
        read_param(kHashNormalScale, theme.has_normal_scale,
                   theme.normal_scale);

        return any;
    }

    bool applyThemeRecord(WaterThemeRecordRef theme_record,
                          WaterTheme& theme) const
    {
        if (!theme_record.valid) return false;
        WaterThemeRecordRef water =
            resolveRecordField(theme_record, kHashWater);
        if (water.valid && applyWaterRecord(water, theme)) {
            return true;
        }
        return applyWaterRecord(theme_record, theme);
    }

    bool applyThemeField(WaterThemeRecordRef owner,
                         uint32_t field_hash,
                         WaterTheme& theme) const
    {
        WaterThemeRecordRef target = resolveRecordField(owner, field_hash);
        return target.valid && applyThemeRecord(target, theme);
    }
