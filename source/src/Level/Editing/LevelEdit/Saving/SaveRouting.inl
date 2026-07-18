bool append_save_entities(
    std::vector<uint8_t>& xml_bytes,
    const std::vector<std::pair<std::string, uint32_t>>& entities,
    bool route_bwsslums_childhood,
    std::string& err)
{
    if (entities.empty()) return true;
    std::string xml(reinterpret_cast<const char*>(xml_bytes.data()),
                    xml_bytes.size());

    auto find_named_always_on = [&](const char* name,
                                     size_t& open,
                                     size_t& close) -> bool {
        const std::string marker =
            std::string("name=\"") + name + "\"";
        size_t at = 0;
        while ((at = xml.find(marker, at)) != std::string::npos) {
            open = xml.rfind("<id_", at);
            const size_t tag_end = open == std::string::npos
                                       ? std::string::npos
                                       : xml.find('>', open);
            if (open == std::string::npos || tag_end == std::string::npos ||
                at > tag_end ||
                xml.find("load=\"AlwaysOn\"", open) > tag_end) {
                at += marker.size();
                continue;
            }
            const size_t id_end = xml.find_first_of(" \t>", open);
            if (id_end == std::string::npos || id_end > tag_end) {
                return false;
            }
            const std::string close_tag =
                "</" + xml.substr(open + 1, id_end - open - 1) + ">";
            close = xml.find(close_tag, tag_end + 1);
            return close != std::string::npos;
        }
        return false;
    };
    auto find_first_always_on = [&](size_t& open, size_t& close) -> bool {
        const size_t layer = xml.find("load=\"AlwaysOn\">");
        if (layer == std::string::npos) return false;
        open = xml.rfind("<id_", layer);
        if (open == std::string::npos) return false;
        const size_t id_end = xml.find_first_of(" \t>", open);
        if (id_end == std::string::npos) return false;
        const std::string close_tag =
            "</" + xml.substr(open + 1, id_end - open - 1) + ">";
        close = xml.find(close_tag, layer);
        return close != std::string::npos;
    };

    size_t open = 0, close = 0;
    const bool childhood_layer =
        route_bwsslums_childhood &&
        find_named_always_on("Chapter1DefaultLayer", open, close);

    std::vector<std::pair<std::string, uint32_t>> routed_entities;
    if (childhood_layer) {





        size_t at = 0;
        const std::string prefix = "<Entity name=\"F2AB_";
        while ((at = xml.find(prefix, at)) != std::string::npos) {
            const size_t name_begin = at + std::strlen("<Entity name=\"");
            const size_t name_end = xml.find('"', name_begin);
            const size_t value_begin = name_end == std::string::npos
                                           ? std::string::npos
                                           : xml.find('>', name_end);
            const size_t value_end = value_begin == std::string::npos
                                         ? std::string::npos
                                         : xml.find("</Entity>",
                                                    value_begin + 1);
            if (name_end == std::string::npos ||
                value_begin == std::string::npos ||
                value_end == std::string::npos) {
                err = "bad F2AB entity registration in .save";
                return false;
            }
            const std::string value =
                xml.substr(value_begin + 1, value_end - value_begin - 1);
            char* parse_end = nullptr;
            const unsigned long hash =
                std::strtoul(value.c_str(), &parse_end, 0);
            if (parse_end != value.c_str()) {
                routed_entities.emplace_back(
                    xml.substr(name_begin, name_end - name_begin),
                    uint32_t(hash));
            }

            size_t erase_begin = xml.rfind('\n', at);
            erase_begin = erase_begin == std::string::npos
                              ? at
                              : erase_begin + 1;
            size_t erase_end = value_end + std::strlen("</Entity>");
            if (erase_end < xml.size() && xml[erase_end] == '\r') {
                ++erase_end;
            }
            if (erase_end < xml.size() && xml[erase_end] == '\n') {
                ++erase_end;
            }
            xml.erase(erase_begin, erase_end - erase_begin);
            at = erase_begin;
        }
        if (!find_named_always_on("Chapter1DefaultLayer", open, close)) {
            err = "childhood layer disappeared during .save rewrite";
            return false;
        }
    } else if (!find_first_always_on(open, close)) {
        err = "no AlwaysOn layer in .save";
        return false;
    }

    routed_entities.insert(routed_entities.end(),
                           entities.begin(), entities.end());
    std::unordered_set<uint32_t> routed_hashes;

    while (close > 0 &&
           (xml[close - 1] == '\t' || xml[close - 1] == ' ')) {
        --close;
    }
    std::string ins;
    for (const auto& [name, hash] : routed_entities) {
        if (!routed_hashes.insert(hash).second) continue;
        char line[128];
        std::snprintf(line, sizeof(line),
                      "\t\t<Entity name=\"%s\">0x%08X</Entity>\r\n",
                      name.c_str(), hash);
        ins += line;
    }

    if (xml.find('\r') == std::string::npos) {
        std::string tmp;
        for (char c : ins) {
            if (c != '\r') tmp.push_back(c);
        }
        ins = tmp;
    }
    xml.insert(close, ins);
    xml_bytes.assign(xml.begin(), xml.end());
    return true;
}

struct SavePhysPatch {
    uint32_t hash = 0;
    float pos[3] = {0, 0, 0};
    float rot_deg[3] = {0, 0, 0};
    bool set_rot = false;
};

int find_level_save_index(const std::string& bnk_path, int lev_index) {
    if (bnk_path.empty() || lev_index < 0) return -1;
    try {
        const auto bc = BnkCache::get(bnk_path);
        std::string nm = bc.reader->list_files()[(size_t)lev_index].name;
        for (char& c : nm) c = (char)std::tolower((unsigned char)c);
        const std::string suffix = ".engine_level";
        const size_t sp = nm.rfind(suffix);
        if (sp == std::string::npos || sp + suffix.size() != nm.size()) {
            return -1;
        }
        std::string stem = nm.substr(0, sp);
        std::replace(stem.begin(), stem.end(), '\\', '/');
        return BnkCache::find_index(bnk_path, stem + ".save");
    } catch (...) {
        return -1;
    }
}

bool is_bwsslums_level(const std::string& bnk_path, int lev_index) {
    if (bnk_path.empty() || lev_index < 0) return false;
    try {
        const auto bc = BnkCache::get(bnk_path);
        const auto& files = bc.reader->list_files();
        if ((size_t)lev_index >= files.size()) return false;
        std::string nm = files[(size_t)lev_index].name;
        std::replace(nm.begin(), nm.end(), '\\', '/');
        for (char& c : nm) c = (char)std::tolower((unsigned char)c);
        return nm.find("worlds/albion/bwsslums/") !=
               std::string::npos;
    } catch (...) {
        return false;
    }
}

size_t apply_save_physics_patches(std::vector<uint8_t>& xml_bytes,
                                  const std::vector<SavePhysPatch>& patches)
{
    if (patches.empty()) return 0;
    std::unordered_map<uint32_t, const SavePhysPatch*> by_hash;
    by_hash.reserve(patches.size() * 2);
    for (const auto& p : patches) by_hash.emplace(p.hash, &p);

    std::string xml(reinterpret_cast<const char*>(xml_bytes.data()),
                    xml_bytes.size());

    auto replace_payload = [&](size_t region_start, size_t& region_end,
                               const char* tag, const std::string& text)
        -> bool {
        const std::string open = std::string("<") + tag;
        const std::string close = std::string("</") + tag + ">";
        size_t a = region_start;
        while (true) {
            a = xml.find(open, a);
            if (a == std::string::npos || a >= region_end) return false;
            const char nxt = a + open.size() < xml.size()
                                 ? xml[a + open.size()] : '\0';
            if (nxt == '>' || nxt == ' ' || nxt == '\t') break;
            a += open.size();
        }
        const size_t gt = xml.find('>', a);
        if (gt == std::string::npos || gt >= region_end) return false;
        const size_t vs = gt + 1;
        const size_t b = xml.find(close, vs);
        if (b == std::string::npos || b > region_end) return false;
        xml.replace(vs, b - vs, text);
        region_end += text.size() - (b - vs);
        return true;
    };
    auto fmt_f = [](float v) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%.6f", v);
        return std::string(buf);
    };
    auto find_open_tag = [&](size_t from, size_t limit,
                             const char* tag) -> size_t {
        const std::string open = std::string("<") + tag;
        size_t a = from;
        while (true) {
            a = xml.find(open, a);
            if (a == std::string::npos || a >= limit) {
                return std::string::npos;
            }
            const char nxt = a + open.size() < xml.size()
                                 ? xml[a + open.size()] : '\0';
            if (nxt == '>' || nxt == ' ' || nxt == '\t') return a;
            a += open.size();
        }
    };

    size_t patched = 0;
    const std::string tag_open = "<Entity name=\"";
    const std::string tag_close = "</Entity>";
    size_t pos = 0;
    while (true) {
        size_t a = xml.find(tag_open, pos);
        if (a == std::string::npos) break;
        const size_t name_end = xml.find('"', a + tag_open.size());
        if (name_end == std::string::npos) break;
        const size_t hash_start = xml.find("0x", name_end);
        if (hash_start == std::string::npos) break;
        size_t entity_close = xml.find(tag_close, hash_start);
        if (entity_close == std::string::npos) break;
        uint32_t h = 0;
        for (size_t i = hash_start + 2;
             i < xml.size() && std::isxdigit((unsigned char)xml[i]); ++i) {
            h <<= 4;
            const char c = xml[i];
            if (c >= '0' && c <= '9') h |= uint32_t(c - '0');
            else if (c >= 'A' && c <= 'F') h |= uint32_t(c - 'A' + 10);
            else h |= uint32_t(c - 'a' + 10);
        }
        auto it = by_hash.find(h);
        if (it == by_hash.end()) {
            pos = entity_close + tag_close.size();
            continue;
        }
        const SavePhysPatch& p = *it->second;

        const size_t phys = find_open_tag(name_end, entity_close,
                                          "PhysicsData");
        if (phys != std::string::npos) {
            size_t phys_end = xml.find("</PhysicsData>", phys);
            if (phys_end != std::string::npos && phys_end < entity_close) {
                const size_t pos_tag = find_open_tag(phys, phys_end,
                                                     "Position");
                if (pos_tag != std::string::npos) {
                    size_t pos_end = xml.find("</Position>", pos_tag);
                    if (pos_end != std::string::npos && pos_end < phys_end) {
                        const size_t before = pos_end;
                        replace_payload(pos_tag, pos_end, "X",
                                        fmt_f(p.pos[0]));
                        replace_payload(pos_tag, pos_end, "Y",
                                        fmt_f(p.pos[1]));
                        replace_payload(pos_tag, pos_end, "Z",
                                        fmt_f(p.pos[2]));
                        phys_end += pos_end - before;
                        entity_close = xml.find(tag_close, pos_tag);
                        ++patched;
                    }
                }
                if (p.set_rot) {
                    const size_t ori = find_open_tag(phys, phys_end,
                                                     "Orientation");
                    if (ori != std::string::npos) {
                        size_t ori_end = xml.find("</Orientation>", ori);
                        if (ori_end != std::string::npos &&
                            ori_end < phys_end) {
                            const float hx =
                                p.rot_deg[0] * kDegToRad * 0.5f;
                            const float hy =
                                p.rot_deg[1] * kDegToRad * 0.5f;
                            const float hz =
                                p.rot_deg[2] * kDegToRad * 0.5f;
                            const float qx4[4] = {std::sin(hx), 0, 0,
                                                  std::cos(hx)};
                            const float qy4[4] = {0, std::sin(hy), 0,
                                                  std::cos(hy)};
                            const float qz4[4] = {0, 0, std::sin(hz),
                                                  std::cos(hz)};
                            float t[4], q[4];
                            quat_mul(qy4, qx4, t);
                            quat_mul(qz4, t, q);
                            replace_payload(ori, ori_end, "X", fmt_f(q[0]));
                            replace_payload(ori, ori_end, "Y", fmt_f(q[1]));
                            replace_payload(ori, ori_end, "Z", fmt_f(q[2]));
                            replace_payload(ori, ori_end, "W", fmt_f(q[3]));
                            entity_close = xml.find(tag_close, ori);
                        }
                    }
                }
            }
        }
        if (entity_close == std::string::npos) break;
        pos = entity_close + tag_close.size();
    }
    if (patched) xml_bytes.assign(xml.begin(), xml.end());
    return patched;
}
