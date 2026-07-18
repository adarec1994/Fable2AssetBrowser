std::unordered_map<uint32_t, std::vector<ParticleAttachmentBinding>>
ExtractParticleAttachmentBindings(
    const std::vector<const std::vector<uint8_t>*>& gdbs)
{
    std::unordered_map<uint32_t, std::vector<ParticleAttachmentBinding>> out;
    const std::unordered_map<uint32_t, std::vector<ParticleFxBinding>>
        effects_by_record = ExtractParticleFxBindings(gdbs);

    std::unordered_map<uint32_t, std::string> name_by_hash;
    std::unordered_map<uint32_t, std::string> name_by_lower;
    for (const auto* g : gdbs) {
        if (g && !g->empty())
            CollectGdbNameStrings(*g, name_by_hash, name_by_lower);
    }

    std::unordered_map<uint32_t, uint32_t> parent_of;
    std::unordered_map<uint32_t, std::vector<uint32_t>> referrers;

    auto add_unique = [](std::vector<ParticleAttachmentBinding>& dst,
                         const ParticleAttachmentBinding& b) {
        for (const auto& e : dst) {
            if (e.fx_hash_lower == b.fx_hash_lower &&
                e.component_hash == b.component_hash &&
                e.dummy_hash == b.dummy_hash &&
                std::fabs(e.offset[0] - b.offset[0]) < 1e-6f &&
                std::fabs(e.offset[1] - b.offset[1]) < 1e-6f &&
                std::fabs(e.offset[2] - b.offset[2]) < 1e-6f) {
                return;
            }
        }
        dst.push_back(b);
    };

    for (const auto* gp : gdbs) {
        if (!gp || gp->empty()) continue;
        const std::vector<uint8_t>& bytes = *gp;
        GdbView view(bytes);
        if (!view.ok) continue;

        auto for_each_field = [&](size_t record, auto&& fn) {
            size_t sch = 0;
            uint32_t fc = 0;
            if (!view.schema(record, sch, fc)) return;
            const size_t hashes = sch + 4;
            const size_t descs = hashes + size_t(fc) * 4;
            for (uint32_t i = 0; i < fc; ++i) {
                const uint32_t fh =
                    ReadBeU32(bytes.data() + hashes + size_t(i) * 4);
                const uint32_t desc =
                    ReadBeU32(bytes.data() + descs + size_t(i) * 4);
                const uint8_t type = uint8_t(desc >> 24);
                const size_t slot = record + 4 + size_t(i) * 4;
                if (slot + 4 > view.body_end) break;
                const uint32_t val = ReadBeU32(bytes.data() + slot);
                fn(fh, type, val);
            }
        };

        for (uint32_t i = 0; i < view.count; ++i) {
            if (i >= view.record_data_offsets.size()) break;
            const uint32_t rh =
                ReadBeU32(bytes.data() + view.hash_base + size_t(i) * 4);
            const size_t rec = view.record_data_offsets[i];
            for_each_field(rec, [&](uint32_t fh, uint8_t type, uint32_t val) {
                if ((type == 6 || type == 7) && val != 0) {
                    referrers[val].push_back(rh);
                    if (fh == kHashParent) parent_of[rh] = val;
                }
            });
        }
    }

    for (const auto* gp : gdbs) {
        if (!gp || gp->empty()) continue;
        const std::vector<uint8_t>& bytes = *gp;
        GdbView view(bytes);
        if (!view.ok) continue;

        auto read_bool = [&](size_t record, uint32_t hash, bool& out_bool) {
            size_t slot = 0;
            if (!view.findField(record, hash, 0, slot, nullptr)) return false;
            out_bool = ReadBeU32(bytes.data() + slot) != 0;
            return true;
        };
        auto read_float = [&](size_t record, uint32_t hash, float& out_float) {
            size_t slot = 0;
            if (!view.findField(record, hash, 3, slot, nullptr)) return false;
            out_float = ReadBeF32(bytes.data() + slot);
            return std::isfinite(out_float);
        };

        auto resolve_effects = [&](uint32_t emitter_hash) {
            std::vector<ParticleFxBinding> found;
            auto eit = effects_by_record.find(emitter_hash);
            if (eit != effects_by_record.end()) {
                found = eit->second;
            }
            return found;
        };

        for (uint32_t i = 0; i < view.count; ++i) {
            if (i >= view.record_data_offsets.size()) break;
            const uint32_t component_hash =
                ReadBeU32(bytes.data() + view.hash_base + size_t(i) * 4);
            const size_t rec = view.record_data_offsets[i];

            size_t emitter_slot = 0;
            uint8_t emitter_type = 0;
            if (!view.findField(rec, kHashParticleEmitter, 0xFF,
                                emitter_slot, &emitter_type) ||
                (emitter_type != 6 && emitter_type != 7)) {
                continue;
            }

            size_t dummy_slot = 0;
            uint8_t dummy_type = 0;
            const bool has_dummy =
                view.findField(rec, kHashDummyObject, 4, dummy_slot,
                               &dummy_type);
            size_t offset_slot = 0;
            uint8_t offset_type = 0;
            const bool has_offset =
                view.findField(rec, kHashOffsetFromDummy, 0xFF,
                               offset_slot, &offset_type) &&
                (offset_type == 6 || offset_type == 7);
            size_t orient_slot = 0;
            const bool has_orient =
                view.findField(rec, kHashOrientParticleToAttachmentPoint, 0,
                               orient_slot, nullptr);
            if (!has_dummy && !has_offset && !has_orient) {
                continue;
            }

            const uint32_t emitter_hash = ReadBeU32(bytes.data() + emitter_slot);
            std::vector<ParticleFxBinding> effects = resolve_effects(emitter_hash);
            if (effects.empty()) continue;

            ParticleAttachmentBinding base;
            base.component_hash = component_hash;
            base.emitter_record_hash = emitter_hash;

            if (has_dummy) {
                base.dummy_hash = ReadBeU32(bytes.data() + dummy_slot);
                auto it = name_by_hash.find(base.dummy_hash);
                if (it != name_by_hash.end()) base.dummy_name = it->second;
            }
            if (has_offset) {
                const uint32_t off_hash = ReadBeU32(bytes.data() + offset_slot);
                float x = 0.0f, y = 0.0f, z = 0.0f;
                if (view.readVec3Ref(off_hash, x, y, z)) {
                    base.offset[0] = x;
                    base.offset[1] = y;
                    base.offset[2] = z;
                }
            }
            read_float(rec, kHashMaxVisibilityDistance,
                       base.max_visibility_distance);
            read_bool(rec, kHashOverrideMaxVisibilityDistance,
                      base.override_max_visibility_distance);
            read_bool(rec, kHashDisableWhenParentIsInvisible,
                      base.disable_when_parent_invisible);
            read_bool(rec, kHashOrientParticleToAttachmentPoint,
                      base.orient_to_attachment_point);

            std::vector<uint32_t> owners;
            owners.push_back(component_hash);
            std::vector<uint32_t> queue;
            if (auto it = referrers.find(component_hash);
                it != referrers.end()) {
                queue = it->second;
            }
            std::unordered_set<uint32_t> seen_owners;
            seen_owners.insert(component_hash);
            for (size_t qi = 0; qi < queue.size() && qi < 4096; ++qi) {
                const uint32_t owner = queue[qi];
                if (!seen_owners.insert(owner).second) continue;
                owners.push_back(owner);
                if (auto it = referrers.find(owner);
                    it != referrers.end() && qi < 1024) {
                    for (uint32_t p : it->second) queue.push_back(p);
                }
            }

            for (const ParticleFxBinding& fx : effects) {
                ParticleAttachmentBinding b = base;
                b.fx_hash_lower = fx.fx_hash_lower;
                b.fx_name = fx.fx_name;
                for (uint32_t owner : owners) {
                    add_unique(out[owner], b);
                }
            }
        }
    }

    if (!out.empty() && !parent_of.empty()) {
        std::unordered_map<uint32_t, std::vector<ParticleAttachmentBinding>>
            resolved = out;
        for (const auto& kv : parent_of) {
            const uint32_t start = kv.first;
            if (resolved.count(start)) continue;
            uint32_t cur = start;
            int guard = 64;
            const std::vector<ParticleAttachmentBinding>* hit = nullptr;
            std::unordered_set<uint32_t> path;
            while (guard-- > 0 && cur != 0 && !path.count(cur)) {
                path.insert(cur);
                auto pit = parent_of.find(cur);
                if (pit == parent_of.end()) break;
                cur = pit->second;
                auto rit = out.find(cur);
                if (rit != out.end()) {
                    hit = &rit->second;
                    break;
                }
            }
            if (hit) resolved[start] = *hit;
        }
        out.swap(resolved);
    }

    return out;
}
