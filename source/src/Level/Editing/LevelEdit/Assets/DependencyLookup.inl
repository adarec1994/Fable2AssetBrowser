std::string clean_name(const std::string& s) {
    std::string n = s;
    while (!n.empty() && n.back() == '\0') n.pop_back();
    return n;
}

std::string norm_key(const std::string& s) {
    std::string k = clean_name(s);
    for (char& c : k) {
        if (c == '\\') c = '/';
        else c = (char)std::tolower((unsigned char)c);
    }
    return k;
}

bool nested_bank_has(BNKReader& nested, const std::string& want_key) {
    for (const auto& fe : nested.list_files()) {
        if (norm_key(fe.name) == want_key) return true;
    }
    return false;
}

bool find_in_nested_banks(const std::string& container_path,
                          const std::string& nested_suffix,
                          const std::string& want_key,
                          std::string& out_name,
                          std::vector<uint8_t>& out_payload) {
    const auto bc = BnkCache::get(container_path);
    const auto& files = bc.reader->list_files();
    for (size_t i = 0; i < files.size(); ++i) {
        const std::string key = norm_key(files[i].name);
        if (key.size() < nested_suffix.size() ||
            key.compare(key.size() - nested_suffix.size(),
                        nested_suffix.size(), nested_suffix) != 0) {
            continue;
        }
        std::vector<uint8_t> blob;
        try {
            blob = BnkCache::extract_bytes(container_path, (int)i);
        } catch (...) {
            continue;
        }
        try {
            BNKReader nested(std::move(blob));
            const auto& nf = nested.list_files();
            for (size_t j = 0; j < nf.size(); ++j) {
                if (norm_key(nf[j].name) == want_key) {
                    out_payload = nested.extract_index_bytes((int)j);
                    out_name = clean_name(nf[j].name);
                    return true;
                }
            }
        } catch (...) {
            continue;
        }
    }
    return false;
}

size_t collect_folder_from_nested_banks(
    const std::string& container_path,
    const std::string& nested_suffix,
    const std::string& folder_key,
    std::vector<BnkWriter::EntryAddition>& out) {
    const auto bc = BnkCache::get(container_path);
    const auto& files = bc.reader->list_files();
    for (size_t i = 0; i < files.size(); ++i) {
        const std::string key = norm_key(files[i].name);
        if (key.size() < nested_suffix.size() ||
            key.compare(key.size() - nested_suffix.size(),
                        nested_suffix.size(), nested_suffix) != 0) {
            continue;
        }
        std::vector<uint8_t> blob;
        try {
            blob = BnkCache::extract_bytes(container_path, (int)i);
        } catch (...) {
            continue;
        }
        try {
            BNKReader nested(std::move(blob));
            const auto& nf = nested.list_files();
            size_t found = 0;
            std::vector<BnkWriter::EntryAddition> local;
            for (size_t j = 0; j < nf.size(); ++j) {
                const std::string k = norm_key(nf[j].name);
                if (k.compare(0, folder_key.size(), folder_key) != 0) {
                    continue;
                }
                BnkWriter::EntryAddition a;
                a.name = clean_name(nf[j].name);
                a.payload = nested.extract_index_bytes((int)j);
                local.push_back(std::move(a));
                ++found;
            }
            if (found) {
                for (auto& a : local) out.push_back(std::move(a));
                return found;
            }
        } catch (...) {
            continue;
        }
    }
    return 0;
}

void collect_tex_refs(const std::vector<uint8_t>& mdl,
                      std::vector<std::string>& out) {
    size_t i = 0;
    const size_t n = mdl.size();
    while (i < n) {
        if (mdl[i] < 32 || mdl[i] > 126) {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < n && mdl[j] >= 32 && mdl[j] <= 126) ++j;
        if (j - i >= 8) {
            std::string l = norm_key(
                std::string((const char*)mdl.data() + i, j - i));
            if (l.size() > 4 &&
                l.compare(l.size() - 4, 4, ".tex") == 0) {
                out.push_back(std::move(l));
            }
        }
        i = j + 1;
    }
}

bool patch_lmp_probes(std::vector<uint8_t>& gz,
                      const std::vector<Addition>& adds,
                      const std::vector<uint8_t>& lev_bytes,
                      bool& changed,
                      std::string& err) {
    changed = false;
    std::vector<uint8_t> raw;
    if (!gzip_inflate(gz, raw)) {
        err = "lmp gunzip failed";
        return false;
    }
    static const char kMagic[] = "LightmapFile";
    const size_t magic_len = sizeof(kMagic) - 1;
    if (raw.size() < magic_len + 64 ||
        std::memcmp(raw.data(), kMagic, magic_len) != 0) {
        err = "lmp magic mismatch";
        return false;
    }
    const size_t total = raw.size();
    size_t n = 0;
    for (size_t c = (total - 4) / 56; c >= 1; --c) {
        const size_t pos = total - 56 * c - 4;
        if (pos < magic_len + 4) continue;
        if (get_u32_be2(raw.data() + pos) == (uint32_t)c) {
            n = c;
            break;
        }
    }
    if (!n) {
        err = "lmp probe section not found";
        return false;
    }
    const size_t sec = total - 56 * n;

    std::unordered_map<uint64_t, size_t> probe_off;
    probe_off.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        uint64_t h = 0;
        for (int k = 0; k < 8; ++k) {
            h = (h << 8) | raw[sec + i * 56 + (size_t)k];
        }
        probe_off[h] = sec + i * 56;
    }

    struct Donor {
        float p[3];
        size_t off;
    };
    std::vector<Donor> donors;
    Level::EngineLevelInfo info;
    if (Level::ParseEngineLevel(lev_bytes, info)) {
        for (const auto& b : info.prop_blocks) {
            if (b.type != 2) continue;
            for (const auto& inst : b.instances) {
                auto it = probe_off.find(inst.hash);
                if (it == probe_off.end()) continue;
                Donor d;
                d.p[0] = inst.values[0];
                d.p[1] = inst.values[1];
                d.p[2] = inst.values[2];
                d.off = it->second;
                donors.push_back(d);
            }
        }
    }

    std::vector<uint8_t> add;
    for (size_t ai = 0; ai < adds.size(); ++ai) {
        const Addition& a = adds[ai];
        if (a.removed) continue;
        const uint64_t h =
            addition_instance_hash(lower_model_path(a.model_path), ai);
        if (probe_off.count(h)) continue;
        size_t donor_off = sec + (n - 1) * 56;
        float best = 3.4e38f;
        for (const auto& d : donors) {
            const float dx = d.p[0] - a.pos[0];
            const float dy = d.p[1] - a.pos[1];
            const float dz = d.p[2] - a.pos[2];
            const float dist = dx * dx + dy * dy + dz * dz;
            if (dist < best) {
                best = dist;
                donor_off = d.off;
            }
        }
        put_u64_be(add, h);
        add.insert(add.end(), raw.begin() + donor_off + 8,
                   raw.begin() + donor_off + 56);
    }
    if (add.empty()) return true;

    raw.insert(raw.end(), add.begin(), add.end());
    const uint32_t new_n = (uint32_t)(n + add.size() / 56);
    raw[sec - 4] = uint8_t(new_n >> 24);
    raw[sec - 3] = uint8_t(new_n >> 16);
    raw[sec - 2] = uint8_t(new_n >> 8);
    raw[sec - 1] = uint8_t(new_n);

    std::vector<uint8_t> gz_out;
    if (!gzip_deflate(raw, gz_out)) {
        err = "lmp gzip failed";
        return false;
    }
    gz.swap(gz_out);
    changed = true;
    return true;
}
