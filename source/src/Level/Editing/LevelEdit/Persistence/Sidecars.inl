void register_entry(EditEntry& e, const InstInfo& info) {
    if (e.registered) return;
    e.registered = true;
    e.lev_off = info.lev_off;
    e.lev_kind = info.lev_kind;
    if (info.gdb_off) {
        e.gdb_off[0] = info.gdb_off[0];
        e.gdb_off[1] = info.gdb_off[1];
        e.gdb_off[2] = info.gdb_off[2];
    }
    if (info.gdb_rot_off) {
        e.gdb_rot_off[0] = info.gdb_rot_off[0];
        e.gdb_rot_off[1] = info.gdb_rot_off[1];
        e.gdb_rot_off[2] = info.gdb_rot_off[2];
    }
    e.gdb_entity_hash = info.gdb_entity_hash;
    if (info.orig_pos) {
        e.orig[0] = info.orig_pos[0];
        e.orig[1] = info.orig_pos[1];
        e.orig[2] = info.orig_pos[2];
    }
    e.orig_rot[0] = info.orig_rot_deg[0];
    e.orig_rot[1] = info.orig_rot_deg[1];
    e.orig_rot[2] = info.orig_rot_deg[2];
}

std::filesystem::path additions_path() {
    std::string leaf = std::filesystem::path(st().entry.full_path)
        .filename().string();
    if (leaf.empty()) leaf = "level";
    return edited_levels_dir() / (leaf + ".additions.txt");
}

void load_additions(ModuleState& s) {
    s.additions.clear();
    std::ifstream f(additions_path());
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        Addition a;
        size_t p0 = line.find('\t');
        if (p0 == std::string::npos) continue;
        a.model_path = line.substr(0, p0);
        if (a.model_path == "-") a.model_path.clear();
        if (std::sscanf(line.c_str() + p0 + 1, "%f\t%f\t%f\t%f",
                        &a.pos[0], &a.pos[1], &a.pos[2],
                        &a.yaw_deg) < 3) continue;
        const size_t chest_tag = line.find("\tCHEST");
        const size_t key_tag = line.find("\tKEY");
        if (chest_tag != std::string::npos) {
            a.entity_kind = AdditionEntityKind::Chest;
            const size_t ctpl = line.find("\tCTPL", chest_tag);
            const size_t loot = line.find("\tLOOT", chest_tag);
            const size_t keys = line.find("\tKEYS", chest_tag);
            size_t stop = std::min(
                ctpl == std::string::npos ? line.size() : ctpl,
                loot == std::string::npos ? line.size() : loot);
            stop = std::min(
                stop, keys == std::string::npos ? line.size() : keys);
            size_t p = chest_tag + 6;
            while (p < line.size() && line[p] == '\t') {
                if (p >= stop) break;
                unsigned int h = 0;
                if (std::sscanf(line.c_str() + p + 1, "%x", &h) == 1 && h) {
                    a.chest_items.push_back(h);
                }
                p = line.find('\t', p + 1);
                if (p == std::string::npos) break;
            }
            if (ctpl != std::string::npos) {
                unsigned int tpl = 0, cf = 0, ct = 0, pf = 0;
                if (std::sscanf(line.c_str() + ctpl + 5,
                                "\t%x\t%x\t%x\t%x",
                                &tpl, &cf, &ct, &pf) >= 2 && tpl && cf) {
                    a.entity_template = tpl;
                    a.entity_comp_field = cf;
                    a.entity_comp_template = ct;
                    a.physics_file_hash = pf;
                }
            }
            if (loot != std::string::npos) {
                unsigned int lt = 0;
                if (std::sscanf(line.c_str() + loot + 5, "\t%x",
                                &lt) == 1) {
                    a.loot_table_record = lt;
                }
            }
            if (keys != std::string::npos) {
                int required = 0;
                if (std::sscanf(line.c_str() + keys + 5, "\t%d",
                                &required) == 1 && required > 0) {
                    a.silver_keys_needed = required;
                }
            }
            a.is_dig_spot = line.find("\tDIGSPOT") != std::string::npos;
        } else if (key_tag != std::string::npos) {
            a.entity_kind = AdditionEntityKind::SilverKey;
        } else {
            const size_t prop_tag = line.find("\tPROP");
            if (prop_tag != std::string::npos) {
                unsigned int tpl = 0, cf = 0, ct = 0, pf = 0, ht = 0;
                if (std::sscanf(line.c_str() + prop_tag + 5,
                                "\t%x\t%x\t%x\t%x\t%u",
                                &tpl, &cf, &ct, &pf, &ht) >= 3 &&
                    tpl && cf) {
                    a.entity_kind = AdditionEntityKind::GenericProp;
                    a.entity_template = tpl;
                    a.entity_comp_field = cf;
                    a.entity_comp_template = ct;
                    a.physics_file_hash = pf;
                    a.entity_has_text = ht != 0;
                    const size_t tt = line.find("\tTEXT\t", prop_tag);
                    if (tt != std::string::npos) {
                        a.entity_has_text = true;
                        const char* h = line.c_str() + tt + 6;
                        auto nib = [](char c) -> int {
                            if (c >= '0' && c <= '9') return c - '0';
                            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                            return -1;
                        };
                        for (size_t k = 0; h[k] && h[k + 1]; k += 2) {
                            const int hi = nib(h[k]);
                            const int lo = nib(h[k + 1]);
                            if (hi < 0 || lo < 0) break;
                            a.readable_text.push_back(
                                char((hi << 4) | lo));
                        }
                    }
                }
            }
        }
        if (a.entity_kind == AdditionEntityKind::Chest &&
            a.silver_keys_needed <= 0) {
            a.silver_keys_needed =
                SilverKeyChestRequirement(a.model_path);
        }
        s.additions.push_back(std::move(a));
    }
    if (!s.additions.empty()) {
        OutputLog::info("level edit: loaded " +
                        std::to_string(s.additions.size()) +
                        " placed model(s) from " +
                        additions_path().string());
    }
}

bool write_additions(const ModuleState& s, std::string& msg) {
    const auto path = additions_path();
    std::error_code ec;
    size_t alive = 0;
    for (const auto& a : s.additions) {
        if (!a.removed) ++alive;
    }
    if (alive == 0) {
        std::filesystem::remove(path, ec);
        return true;
    }
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        msg = "could not write " + path.string();
        return false;
    }
    for (const auto& a : s.additions) {
        if (a.removed) continue;
        f << (a.model_path.empty() ? "-" : a.model_path)
          << '\t' << a.pos[0] << '\t' << a.pos[1] << '\t'
          << a.pos[2] << '\t' << a.yaw_deg;
        if (a.entity_kind == AdditionEntityKind::Chest) {
            f << "\tCHEST";
            char buf[80];
            for (uint32_t h : a.chest_items) {
                std::snprintf(buf, sizeof(buf), "%08X", h);
                f << '\t' << buf;
            }
            if (a.silver_keys_needed > 0) {
                f << "\tKEYS\t" << a.silver_keys_needed;
            }
            if (a.entity_template) {
                std::snprintf(buf, sizeof(buf),
                              "\tCTPL\t%08X\t%08X\t%08X\t%08X",
                              a.entity_template, a.entity_comp_field,
                              a.entity_comp_template,
                              a.physics_file_hash);
                f << buf;
            }
            if (a.loot_table_record) {
                std::snprintf(buf, sizeof(buf), "\tLOOT\t%08X",
                              a.loot_table_record);
                f << buf;
            }
            if (a.is_dig_spot) f << "\tDIGSPOT";
        } else if (a.entity_kind == AdditionEntityKind::SilverKey) {
            f << "\tKEY";
        } else if (a.entity_kind == AdditionEntityKind::GenericProp) {
            char buf[80];
            std::snprintf(buf, sizeof(buf),
                          "\tPROP\t%08X\t%08X\t%08X\t%08X\t%d",
                          a.entity_template, a.entity_comp_field,
                          a.entity_comp_template, a.physics_file_hash,
                          a.entity_has_text ? 1 : 0);
            f << buf;
            if (a.entity_has_text && !a.readable_text.empty()) {
                f << "\tTEXT\t";
                static const char* hexd = "0123456789ABCDEF";
                for (unsigned char c : a.readable_text) {
                    f << hexd[c >> 4] << hexd[c & 15];
                }
            }
        }
        f << '\n';
    }
    return true;
}



std::filesystem::path spawns_path() {
    std::string leaf = std::filesystem::path(st().entry.full_path)
        .filename().string();
    if (leaf.empty()) leaf = "level";
    return edited_levels_dir() / (leaf + ".spawns.txt");
}

bool write_spawns(const ModuleState& s, std::string& msg) {
    const auto path = spawns_path();
    std::error_code ec;
    size_t alive_gens = 0;
    for (const auto& g : s.generators) {
        if (!g.removed) ++alive_gens;
    }
    if (alive_gens == 0 && s.spawn_point_adds.empty()) {
        std::filesystem::remove(path, ec);
        return true;
    }
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        msg = "could not write " + path.string();
        return false;
    }
    char hex[16];
    for (const auto& g : s.generators) {
        if (g.removed) continue;
        std::snprintf(hex, sizeof(hex), "%08X", g.creature_entity);
        f << "GEN\t" << g.pos[0] << '\t' << g.pos[1] << '\t' << g.pos[2]
          << '\t' << hex << '\t' << g.creature_name;
        for (const auto& m : g.asset_models) f << '\t' << m;
        f << '\n';
        for (const auto& sp : g.spawn_points) {
            f << "GSP\t" << sp[0] << '\t' << sp[1] << '\t' << sp[2]
              << '\n';
        }
    }
    for (const auto& a : s.spawn_point_adds) {
        char hex2[16];
        std::snprintf(hex, sizeof(hex), "%08X", a.generator_entity);
        std::snprintf(hex2, sizeof(hex2), "%08X", a.spawn_points_record);
        f << "SPA\t" << a.pos[0] << '\t' << a.pos[1] << '\t' << a.pos[2]
          << '\t' << hex << '\t' << hex2 << '\n';
    }
    return true;
}

void load_spawns(ModuleState& s) {
    s.generators.clear();
    s.spawn_point_adds.clear();
    std::ifstream f(spawns_path());
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        while (!line.empty() &&
               (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        if (line.empty()) continue;
        std::vector<std::string> tok;
        size_t start = 0;
        for (;;) {
            const size_t tab = line.find('\t', start);
            tok.push_back(line.substr(
                start, tab == std::string::npos ? std::string::npos
                                                : tab - start));
            if (tab == std::string::npos) break;
            start = tab + 1;
        }
        if (tok[0] == "GEN" && tok.size() >= 6) {
            GeneratorAddition g;
            g.pos[0] = std::strtof(tok[1].c_str(), nullptr);
            g.pos[1] = std::strtof(tok[2].c_str(), nullptr);
            g.pos[2] = std::strtof(tok[3].c_str(), nullptr);
            g.creature_entity =
                (uint32_t)std::strtoul(tok[4].c_str(), nullptr, 16);
            g.creature_name = tok[5];
            for (size_t i = 6; i < tok.size(); ++i) {
                if (!tok[i].empty()) g.asset_models.push_back(tok[i]);
            }
            s.generators.push_back(std::move(g));
        } else if (tok[0] == "GSP" && tok.size() >= 4 &&
                   !s.generators.empty()) {
            s.generators.back().spawn_points.push_back(
                {std::strtof(tok[1].c_str(), nullptr),
                 std::strtof(tok[2].c_str(), nullptr),
                 std::strtof(tok[3].c_str(), nullptr)});
        } else if (tok[0] == "SPA" && tok.size() >= 6) {
            ModuleState::SpawnPointAdd a;
            a.pos[0] = std::strtof(tok[1].c_str(), nullptr);
            a.pos[1] = std::strtof(tok[2].c_str(), nullptr);
            a.pos[2] = std::strtof(tok[3].c_str(), nullptr);
            a.generator_entity =
                (uint32_t)std::strtoul(tok[4].c_str(), nullptr, 16);
            a.spawn_points_record =
                (uint32_t)std::strtoul(tok[5].c_str(), nullptr, 16);
            s.spawn_point_adds.push_back(a);
        }
    }
    if (!s.generators.empty() || !s.spawn_point_adds.empty()) {
        OutputLog::info("level edit: loaded " +
                        std::to_string(s.generators.size()) +
                        " generator(s) and " +
                        std::to_string(s.spawn_point_adds.size()) +
                        " pending spawn point(s) from " +
                        spawns_path().string());
    }
}
