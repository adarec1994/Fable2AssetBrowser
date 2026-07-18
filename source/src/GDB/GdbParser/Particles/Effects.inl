std::unordered_map<uint32_t, std::vector<ParticleFxBinding>>
ExtractParticleFxBindings(
    const std::vector<const std::vector<uint8_t>*>& gdbs)
{
    std::unordered_map<uint32_t, std::vector<ParticleFxBinding>> out;

    std::unordered_map<uint32_t, std::string> name_by_lower;
    for (const auto* g : gdbs) {
        if (g && !g->empty()) CollectFxNameStrings(*g, name_by_lower);
    }

    std::unordered_map<uint32_t, uint32_t> parent_of;

    for (const auto* gp : gdbs) {
        if (!gp || gp->empty()) continue;
        const std::vector<uint8_t>& bytes = *gp;
        GdbView view(bytes);
        if (!view.ok) continue;

        auto for_each_field = [&](size_t record, auto&& fn) {
            size_t sch = 0; uint32_t fc = 0;
            if (!view.schema(record, sch, fc)) return;
            const size_t hashes = sch + 4;
            const size_t descs = hashes + size_t(fc) * 4;
            for (uint32_t i = 0; i < fc; ++i) {
                const uint32_t fh = ReadBeU32(bytes.data() + hashes + size_t(i) * 4);
                const uint32_t desc = ReadBeU32(bytes.data() + descs + size_t(i) * 4);
                const uint8_t type = uint8_t(desc >> 24);
                const size_t slot = record + 4 + size_t(i) * 4;
                if (slot + 4 > view.body_end) break;
                const uint32_t val = ReadBeU32(bytes.data() + slot);
                fn(fh, type, val);
            }
        };

        for (uint32_t i = 0; i < view.count; ++i) {
            if (i >= view.record_data_offsets.size()) break;
            const uint32_t rh = ReadBeU32(bytes.data() + view.hash_base + size_t(i) * 4);
            const size_t rec = view.record_data_offsets[i];
            for_each_field(rec, [&](uint32_t fh, uint8_t type, uint32_t val) {
                if (fh == kHashParent && (type == 6 || type == 7) && val != 0)
                    parent_of[rh] = val;
            });
        }

        for (uint32_t i = 0; i < view.count; ++i) {
            if (i >= view.record_data_offsets.size()) break;
            const uint32_t rh = ReadBeU32(bytes.data() + view.hash_base + size_t(i) * 4);
            const size_t rec = view.record_data_offsets[i];

            uint32_t chain_root = 0;
            for_each_field(rec, [&](uint32_t fh, uint8_t type, uint32_t val) {
                if (fh == kHashParticleEffect && (type == 6 || type == 7) && val != 0)
                    chain_root = val;
            });
            if (chain_root == 0) continue;

            std::vector<uint32_t> hashes_found;
            std::unordered_set<uint32_t> seen;
            std::vector<uint32_t> stack{ chain_root };
            int budget = 4096;
            while (!stack.empty() && budget-- > 0) {
                uint32_t rref = stack.back(); stack.pop_back();
                if (rref == 0 || seen.count(rref)) continue;
                seen.insert(rref);
                size_t rr = 0;
                if (!view.lookup(rref, rr)) continue;
                for_each_field(rr, [&](uint32_t fh, uint8_t type, uint32_t val) {
                    if (type == 4 && val != kHashNull) {
                        hashes_found.push_back(val);
                    } else if ((type == 6 || type == 7) && val != 0) {
                        stack.push_back(val);
                    }
                });
            }
            if (hashes_found.empty()) continue;

            std::vector<ParticleFxBinding>& binds = out[rh];
            for (uint32_t h : hashes_found) {
                bool dup = false;
                for (const auto& b : binds) if (b.fx_hash_lower == h) { dup = true; break; }
                if (dup) continue;
                ParticleFxBinding b;
                b.fx_hash_lower = h;
                auto it = name_by_lower.find(h);
                if (it != name_by_lower.end()) b.fx_name = it->second;
                binds.push_back(std::move(b));
            }
        }
    }

    if (!out.empty() && !parent_of.empty()) {
        std::unordered_map<uint32_t, std::vector<ParticleFxBinding>> resolved = out;
        for (const auto& kv : parent_of) {
            const uint32_t start = kv.first;
            if (resolved.count(start)) continue;
            uint32_t cur = start;
            int guard = 64;
            const std::vector<ParticleFxBinding>* hit = nullptr;
            std::unordered_set<uint32_t> path;
            while (guard-- > 0 && cur != 0 && !path.count(cur)) {
                path.insert(cur);
                auto pit = parent_of.find(cur);
                if (pit == parent_of.end()) break;
                cur = pit->second;
                auto rit = out.find(cur);
                if (rit != out.end()) { hit = &rit->second; break; }
            }
            if (hit) resolved[start] = *hit;
        }
        out.swap(resolved);
    }

    return out;
}
