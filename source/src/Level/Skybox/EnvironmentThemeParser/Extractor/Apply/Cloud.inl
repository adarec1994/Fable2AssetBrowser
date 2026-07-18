    int readCloudLayerRecord(WaterThemeRecordRef layer,
                             CloudLayerTheme& out) const
    {
        if (!layer.valid) return 0;

        int fields = 0;
        auto read_param = [&](uint32_t hash, bool& flag, float& dst) {
            float v = 0.0f;
            if (readFloat(layer, hash, v)) {
                flag = true;
                dst = v;
                ++fields;
            }
        };

        WaterThemeFieldRef density;
        if (findField(layer, kHashDensityMap, 0xFF, density) &&
            density.raw != 0 && density.raw != kHashNull) {
            out.has_density_map = true;
            out.density_map_hash = density.raw;
            ++fields;
        }

        read_param(kHashPositionX, out.has_position, out.position_x);
        read_param(kHashPositionY, out.has_position, out.position_y);
        read_param(kHashSizeX, out.has_size, out.size_x);
        read_param(kHashSizeY, out.has_size, out.size_y);
        read_param(kHashTextureScaleX, out.has_texture_scale,
                   out.texture_scale_x);
        read_param(kHashTextureScaleY, out.has_texture_scale,
                   out.texture_scale_y);
        read_param(kHashVelocityX, out.has_velocity, out.velocity_x);
        read_param(kHashVelocityY, out.has_velocity, out.velocity_y);
        read_param(kHashHeight, out.has_height, out.height);
        read_param(kHashTransparency, out.has_transparency,
                   out.transparency);
        read_param(kHashBrightness, out.has_brightness, out.brightness);
        read_param(kHashAmbientLight, out.has_ambient,
                   out.ambient_light);
        read_param(kHashNormalStrength, out.has_normal_strength,
                   out.normal_strength);
        read_param(kHashTranslucencyStrength,
                   out.has_translucency_strength,
                   out.translucency_strength);

        if (fields > 0) {
            out.enabled = true;
        }
        return fields;
    }

    bool applyCloudLayerField(WaterThemeRecordRef theme_record,
                              uint32_t field_hash,
                              CloudLayerTheme& layer) const
    {
        WaterThemeRecordRef target = resolveRecordField(theme_record,
                                                        field_hash);
        return target.valid && readCloudLayerRecord(target, layer) > 0;
    }

    bool applyCloudThemeRecord(WaterThemeRecordRef theme_record,
                               CloudTheme& theme) const
    {
        if (!theme_record.valid) return false;

        WaterThemeRecordRef clouds =
            resolveRecordField(theme_record, kHashClouds);
        if (!clouds.valid) {
            clouds = theme_record;
        }

        bool any = false;
        constexpr uint32_t kLayerFields[4] = {
            kHashLayer1, kHashLayer2, kHashLayer3, kHashLayer4
        };
        for (int i = 0; i < 4; ++i) {
            CloudLayerTheme layer = theme.layers[i];
            if (applyCloudLayerField(clouds, kLayerFields[i], layer)) {
                theme.layers[i] = layer;
                any = true;
            }
        }

        if (!any) {
            CloudLayerTheme layer = theme.layers[0];
            const int field_count = readCloudLayerRecord(clouds, layer);
            if (field_count >= 2 || layer.has_density_map) {
                theme.layers[0] = layer;
                any = true;
            }
        }

        return any;
    }

    bool applyCloudThemeField(WaterThemeRecordRef owner,
                              uint32_t field_hash,
                              CloudTheme& theme) const
    {
        WaterThemeRecordRef target = resolveRecordField(owner, field_hash);
        return target.valid && applyCloudThemeRecord(target, theme);
    }
