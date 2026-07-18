std::vector<FxEntityPlacement> ExtractFxEntityPlacements(
    const std::vector<uint8_t>& level_bytes,
    const std::vector<const std::vector<uint8_t>*>& gdbs,
    const std::vector<std::pair<uint32_t, std::string>>& hash_to_name)
{
    std::vector<FxEntityPlacement> out;
    if (level_bytes.empty()) return out;
    GdbView level(level_bytes);
    if (!level.ok) return out;

    std::vector<std::unique_ptr<GdbView>> views;
    for (const auto* g : gdbs) {
        if (!g || g->empty()) continue;
        auto v = std::make_unique<GdbView>(*g);
        if (v->ok) views.push_back(std::move(v));
    }

    auto for_each_field = [](const GdbView& view, size_t record, auto&& fn) {
        size_t sch = 0; uint32_t fc = 0;
        if (!view.schema(record, sch, fc)) return;
        const size_t hashes = sch + 4;
        const size_t descs = hashes + size_t(fc) * 4;
        for (uint32_t i = 0; i < fc; ++i) {
            const uint32_t fh =
                ReadBeU32(view.bytes.data() + hashes + size_t(i) * 4);
            const uint32_t desc =
                ReadBeU32(view.bytes.data() + descs + size_t(i) * 4);
            const uint8_t type = uint8_t(desc >> 24);
            const size_t slot = record + 4 + size_t(i) * 4;
            if (slot + 4 > view.body_end) break;
            fn(fh, type, ReadBeU32(view.bytes.data() + slot));
        }
    };

    auto hunt_fx_hash = [&](uint32_t root) -> uint32_t {
        uint32_t found = 0;
        std::unordered_set<uint32_t> seen;
        std::vector<std::pair<uint32_t, int>> stack{ { root, 0 } };
        int budget = 2048;
        while (!stack.empty() && budget-- > 0 && !found) {
            auto [h, depth] = stack.back();
            stack.pop_back();
            if (!h || depth > 4 || seen.count(h)) continue;
            seen.insert(h);
            for (const auto& v : views) {
                size_t rec = 0;
                if (!v->lookup(h, rec)) continue;
                for_each_field(*v, rec,
                               [&](uint32_t fh, uint8_t type, uint32_t val) {
                    if (fh == kHashParticleSystemNameHash && val != 0 &&
                        val != kHashNull) {
                        found = val;
                    } else if ((type == 6 || type == 7) && val != 0) {
                        stack.emplace_back(val, depth + 1);
                    }
                });
                break;
            }
        }
        return found;
    };

    for (const auto& [rec_hash, name] : hash_to_name) {
        size_t rec = 0;
        if (!level.lookup(rec_hash, rec)) continue;
        FxEntityPlacement p;

        if (!TryComponentTransformField(level, rec,
                                        kHashSimpleTransformComponent,
                                        p.x, p.y, p.z,
                                        p.rot_x, p.rot_y, p.rot_z,
                                        p.has_rotation) &&
            !TryComponentTransformRecord(level, rec, p.x, p.y, p.z,
                                         p.rot_x, p.rot_y, p.rot_z,
                                         p.has_rotation)) {
            continue;
        }

        p.fx_hash = hunt_fx_hash(rec_hash);
        p.record_hash = rec_hash;
        p.name = name;
        out.push_back(std::move(p));
    }
    return out;
}
