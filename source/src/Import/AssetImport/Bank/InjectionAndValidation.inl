std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

std::string normalize_folder(std::string f) {
    std::replace(f.begin(), f.end(), '/', '\\');
    while (!f.empty() && (f.back() == '\\')) f.pop_back();
    while (!f.empty() && (f.front() == '\\')) f.erase(f.begin());
    return to_lower(f);
}

struct PendingEntry {
    std::string bnk_filename;   
    std::string virtual_path;   
    std::vector<uint8_t> payload;
};



bool inject_entries(const std::vector<PendingEntry>& entries,
                    std::vector<std::pair<std::string, std::string>>& injected,
                    std::string& err)
{
    struct Target {
        std::string path;
        std::vector<BnkWriter::EntryReplacement> repls;
        std::vector<BnkWriter::EntryAddition> adds;
    };
    std::map<std::string, Target> targets;

    for (const auto& e : entries) {
        auto p = find_bnk_by_filename(e.bnk_filename);
        if (!p) {
            err = e.bnk_filename + " was not found in the loaded game data";
            return false;
        }
        Target& t = targets[*p];
        t.path = *p;
        const std::string key = to_lower(e.virtual_path);
        int existing = BnkCache::find_index(t.path, [&]{
            std::string k = key;
            std::replace(k.begin(), k.end(), '\\', '/');
            return k;
        }());
        if (existing >= 0) {
            BnkWriter::EntryReplacement r;
            r.file_index = existing;
            r.payload = e.payload;
            t.repls.push_back(std::move(r));
        } else {
            BnkWriter::EntryAddition a;
            a.name = e.virtual_path;
            a.payload = e.payload;
            t.adds.push_back(std::move(a));
        }
        injected.emplace_back(t.path, e.virtual_path);
    }

    
    struct Snap { std::string path; std::vector<uint8_t> bytes; };
    std::vector<Snap> snaps;
    for (auto& [path, t] : targets) {
        std::ifstream f(path, std::ios::binary);
        if (!f) { err = "could not read " + path; return false; }
        Snap s;
        s.path = path;
        s.bytes.assign((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
        snaps.push_back(std::move(s));
    }

    size_t done = 0;
    for (auto& [path, t] : targets) {
        BnkCache::invalidate(path);
        if (!BnkWriter::RebuildWithChanges(path, t.repls, t.adds, err)) {
            err = std::filesystem::path(path).filename().string() + ": " + err;
            break;
        }
        ++done;
    }

    if (done != targets.size()) {
        for (const Snap& s : snaps) {
            std::ofstream f(s.path, std::ios::binary | std::ios::trunc);
            if (f) f.write((const char*)s.bytes.data(),
                           (std::streamsize)s.bytes.size());
            BnkCache::invalidate(s.path);
        }
        return false;
    }

    for (auto& [path, t] : targets) BnkCache::invalidate(path);
    return true;
}

void register_injected(const std::vector<std::pair<std::string, std::string>>& injected)
{
    for (const auto& [bnk, vpath] : injected)
        tree_register_injected_file(bnk, vpath);
}



bool verify_mdl(const MdlWriter::BuiltMdl& built, std::string& err)
{
    std::vector<unsigned char> whole;
    whole.reserve(built.header.size() + built.body.size());
    whole.insert(whole.end(), built.header.begin(), built.header.end());
    whole.insert(whole.end(), built.body.begin(), built.body.end());
    std::vector<MDLMeshGeom> geoms;
    if (!build_mdl_engine_geometry(whole, geoms) || geoms.empty()) {
        err = "internal: generated .mdl failed the engine-walker round-trip";
        return false;
    }
    return true;
}

bool verify_tex(const TexWriter::BuiltTex& built, std::string& err)
{
    std::vector<unsigned char> whole;
    whole.insert(whole.end(), built.header.begin(), built.header.end());
    whole.insert(whole.end(), built.mip0.begin(), built.mip0.end());
    whole.insert(whole.end(), built.body.begin(), built.body.end());
    TexInfo ti;
    if (!parse_tex_info(whole, ti) || ti.Mips.empty()) {
        err = "internal: generated .tex failed parse_tex_info";
        return false;
    }
    if (ti.TextureWidth != built.width || ti.TextureHeight != built.height ||
        ti.Mips.size() != built.mip_count) {
        err = "internal: generated .tex round-trip mismatch";
        return false;
    }
    return true;
}

void queue_tex(const std::string& vpath, const TexWriter::BuiltTex& built,
               std::vector<PendingEntry>& pending)
{
    pending.push_back({"globals_texture_headers.bnk", vpath, built.header});
    if (!built.mip0.empty())
        pending.push_back({"1024mip0_textures.bnk", vpath, built.mip0});
    pending.push_back({"globals_textures.bnk", vpath, built.body});
}
