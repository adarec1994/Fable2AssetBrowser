    std::vector<GdbView> views_;

    static float normalizeTimeOfDay(float time)
    {
        if (!std::isfinite(time)) return 0.5f;
        if (time > 1.0f && time <= 24.0f) {
            time *= (1.0f / 24.0f);
        }
        time -= std::floor(time);
        if (time < 0.0f) time += 1.0f;
        return time;
    }

    const GdbView& view(const WaterThemeRecordRef& ref) const
    {
        return views_[ref.db];
    }

    std::vector<WaterThemeRecordRef> lookupAll(uint32_t hash) const
    {
        std::vector<WaterThemeRecordRef> out;
        if (hash == 0 || hash == kHashNull) return out;
        for (size_t db = 0; db < views_.size(); ++db) {
            const GdbView& v = views_[db];
            if (!v.ok) continue;
            size_t record = 0;
            if (v.lookup(hash, record)) {
                out.push_back(WaterThemeRecordRef{db, record, true});
            }
        }
        return out;
    }

    std::vector<WaterThemeRecordRef> lookupAllInDb(uint32_t hash,
                                                   size_t db) const
    {
        std::vector<WaterThemeRecordRef> out;
        if (hash == 0 || hash == kHashNull || db >= views_.size()) {
            return out;
        }
        const GdbView& v = views_[db];
        if (!v.ok) return out;
        size_t record = 0;
        if (v.lookup(hash, record)) {
            out.push_back(WaterThemeRecordRef{db, record, true});
        }
        return out;
    }

    WaterThemeRecordRef lookupFirst(uint32_t hash) const
    {
        const std::vector<WaterThemeRecordRef> all = lookupAll(hash);
        if (all.empty()) return {};
        return all.front();
    }

    WaterThemeRecordRef firstRecordWithField(uint32_t field_hash) const
    {
        for (size_t db = 0; db < views_.size(); ++db) {
            const GdbView& v = views_[db];
            if (!v.ok) continue;
            for (size_t record : v.record_data_offsets) {
                size_t slot = 0;
                if (v.findLocal(record, field_hash, 0xFF,
                                slot, nullptr)) {
                    return WaterThemeRecordRef{db, record, true};
                }
            }
        }
        return {};
    }

    WaterThemeRecordRef firstRecordWithFieldInDb(uint32_t field_hash,
                                                 size_t db) const
    {
        if (db >= views_.size()) return {};
        const GdbView& v = views_[db];
        if (!v.ok) return {};
        for (size_t record : v.record_data_offsets) {
            size_t slot = 0;
            if (v.findLocal(record, field_hash, 0xFF, slot, nullptr)) {
                return WaterThemeRecordRef{db, record, true};
            }
        }
        return {};
    }

    WaterThemeRecordRef firstCloudContainer() const
    {
        constexpr uint32_t kLayerFields[4] = {
            kHashLayer1, kHashLayer2, kHashLayer3, kHashLayer4
        };
        for (size_t db = 0; db < views_.size(); ++db) {
            const GdbView& v = views_[db];
            if (!v.ok) continue;
            for (size_t record : v.record_data_offsets) {
                bool has_all_layers = true;
                for (uint32_t hash : kLayerFields) {
                    size_t slot = 0;
                    if (!v.findLocal(record, hash, 0xFF, slot, nullptr)) {
                        has_all_layers = false;
                        break;
                    }
                }
                if (has_all_layers) {
                    return WaterThemeRecordRef{db, record, true};
                }
            }
        }
        return {};
    }

    WaterThemeRecordRef findDaySet(bool level_only) const
    {
        const size_t db_count = level_only
            ? std::min<size_t>(views_.size(), 1)
            : views_.size();
        for (size_t db = 0; db < db_count; ++db) {
            for (const WaterThemeRecordRef& level :
                 lookupAllInDb(kHashLevelData, db)) {
                WaterThemeRecordRef day_set =
                    resolveRecordField(level, kHashEnvironmentThemeDaySet);
                if (day_set.valid) return day_set;
            }
        }

        for (size_t db = 0; db < db_count; ++db) {
            if (db >= views_.size()) continue;
            const GdbView& v = views_[db];
            if (!v.ok) continue;
            for (size_t record : v.record_data_offsets) {
                size_t slot = 0;
                if (!v.findLocal(record, kHashEnvironmentThemeDaySet, 0xFF,
                                 slot, nullptr)) {
                    continue;
                }
                WaterThemeRecordRef owner{db, record, true};
                WaterThemeRecordRef day_set = resolveRecordField(
                    owner, kHashEnvironmentThemeDaySet);
                if (day_set.valid) return day_set;
            }
        }

        return {};
    }

    bool findLocalField(WaterThemeRecordRef record,
                        uint32_t field_hash,
                        uint8_t expected_type,
                        WaterThemeFieldRef& out) const
    {
        if (!record.valid || record.db >= views_.size()) return false;
        const GdbView& v = view(record);
        size_t slot = 0;
        uint8_t type = 0;
        if (!v.findLocal(record.record, field_hash, expected_type,
                         slot, &type)) {
            return false;
        }
        out.owner = record;
        out.slot = slot;
        out.type = type;
        out.raw = ReadBeU32(v.bytes.data() + slot);
        out.f32 = ReadBeF32(v.bytes.data() + slot);
        return true;
    }

    bool findField(WaterThemeRecordRef record,
                   uint32_t field_hash,
                   uint8_t expected_type,
                   WaterThemeFieldRef& out) const
    {
        if (!record.valid || record.db >= views_.size()) return false;

        WaterThemeRecordRef cur = record;
        std::unordered_set<uint64_t> seen;
        for (int depth = 0; depth < 64; ++depth) {
            const uint64_t key =
                (uint64_t(cur.db) << 48) ^ uint64_t(cur.record);
            if (!seen.insert(key).second) return false;

            if (findLocalField(cur, field_hash, expected_type, out)) {
                return true;
            }

            WaterThemeFieldRef parent_field;
            if (!findLocalField(cur, kHashParent, 0xFF, parent_field)) {
                return false;
            }
            cur = fieldToRecord(parent_field);
            if (!cur.valid) return false;
        }
        return false;
    }

    WaterThemeRecordRef fieldToRecord(const WaterThemeFieldRef& field) const
    {
        if (field.raw == 0 || field.raw == kHashNull) return {};
        if (field.type != 4 && field.type != 6 && field.type != 7) return {};
        return lookupFirst(field.raw);
    }

    WaterThemeRecordRef resolveRecordField(WaterThemeRecordRef record,
                                           uint32_t field_hash) const
    {
        WaterThemeFieldRef field;
        if (!findField(record, field_hash, 0xFF, field)) return {};
        return fieldToRecord(field);
    }

    bool readFloat(WaterThemeRecordRef record,
                   uint32_t field_hash,
                   float& out) const
    {
        WaterThemeFieldRef field;
        if (!findField(record, field_hash, 3, field)) return false;
        if (!std::isfinite(field.f32)) return false;
        out = field.f32;
        return true;
    }

    bool readColour(WaterThemeRecordRef record,
                    uint32_t red_hash,
                    uint32_t green_hash,
                    uint32_t blue_hash,
                    float (&out)[3]) const
    {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        if (!readFloat(record, red_hash, r) ||
            !readFloat(record, green_hash, g) ||
            !readFloat(record, blue_hash, b)) {
            return false;
        }
        out[0] = EnvColourComponentToLinearInput(r);
        out[1] = EnvColourComponentToLinearInput(g);
        out[2] = EnvColourComponentToLinearInput(b);
        return true;
    }

    bool readFlatColour(WaterThemeRecordRef record,
                        uint32_t red_hash,
                        uint32_t green_hash,
                        uint32_t blue_hash,
                        uint32_t factor_hash,
                        float (&out)[3]) const
    {
        if (!readColour(record, red_hash, green_hash, blue_hash, out)) {
            return false;
        }
        float factor = 1.0f;
        if (factor_hash != 0 &&
            readFloat(record, factor_hash, factor) &&
            std::isfinite(factor) && factor > 0.0f) {
            out[0] *= factor;
            out[1] *= factor;
            out[2] *= factor;
        }
        return true;
    }

    bool readColourRecordField(WaterThemeRecordRef record,
                               uint32_t field_hash,
                               float (&out)[3]) const
    {
        WaterThemeRecordRef colour = resolveRecordField(record, field_hash);
        if (!colour.valid) return false;
        if (!readColour(colour, kHashRed, kHashGreen, kHashBlue, out)) {
            return false;
        }
        float factor = 1.0f;
        if (readFloat(colour, kHashFactor, factor) &&
            std::isfinite(factor) && factor > 0.0f) {
            out[0] *= factor;
            out[1] *= factor;
            out[2] *= factor;
        }
        return true;
    }
