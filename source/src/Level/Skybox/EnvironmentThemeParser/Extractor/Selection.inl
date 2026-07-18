    bool selectThemeFromDaySet(WaterThemeRecordRef day_set,
                               WaterThemeRecordRef& out_theme,
                               float& out_time) const
    {
        if (!day_set.valid) return false;
        const GdbView& v = view(day_set);
        size_t sch = 0;
        uint32_t n = 0;
        if (!v.schema(day_set.record, sch, n)) return false;
        const size_t descs = sch + 4 + size_t(n) * 4;

        bool found = false;
        float best_dist = std::numeric_limits<float>::max();
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
                time = read_time;
            }

            const float dist = std::fabs(time - 0.5f);
            if (!found || dist < best_dist) {
                best_dist = dist;
                out_theme = theme;
                out_time = time;
                found = true;
            }
        }
        return found;
    }

    bool applyFirstWaterLikeRecord(WaterTheme& out_theme) const
    {
        for (size_t db = 0; db < views_.size(); ++db) {
            const GdbView& v = views_[db];
            if (!v.ok) continue;
            for (size_t record : v.record_data_offsets) {
                WaterTheme scratch = out_theme;
                WaterThemeRecordRef ref{db, record, true};
                if (applyThemeRecord(ref, scratch)) {
                    out_theme = scratch;
                    return true;
                }
            }
        }
        return false;
    }

    bool applyFirstSkyLikeRecord(SkyTheme& out_theme) const
    {
        for (size_t db = 0; db < views_.size(); ++db) {
            const GdbView& v = views_[db];
            if (!v.ok) continue;
            for (size_t record : v.record_data_offsets) {
                SkyTheme scratch = out_theme;
                WaterThemeRecordRef ref{db, record, true};
                if (applySkyThemeRecord(ref, scratch)) {
                    out_theme = scratch;
                    return true;
                }
            }
        }
        return false;
    }

    void finaliseCloudTheme(CloudTheme& theme) const
    {
        int count = 0;
        for (int i = 0; i < 4; ++i) {
            if (theme.layers[i].enabled) {
                ++count;
            }
        }
        theme.layer_count = count;
    }
