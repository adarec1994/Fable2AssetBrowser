std::string unique_name(std::set<std::string>& used, std::string base)
{
    if (base.empty()) base = "texture";
    std::string name = base;
    int n = 2;
    while (!used.insert(name).second)
        name = base + "_" + std::to_string(n++);
    return name;
}

std::string derive_entity_id(const std::string& asset)
{
    std::string id = "PROP_" + asset;
    for (char& c : id) {
        if (!std::isalnum((unsigned char)c) && c != '_') c = '_';
    }
    return id;
}

void create_spawn_template(const std::string& mdl_vpath,
                           const Options& opt,
                           const std::string& asset,
                           Result& res)
{
    const bool explicit_id = !opt.entity_id.empty();
    std::string id = explicit_id ? opt.entity_id : derive_entity_id(asset);

    std::vector<StaticPropAuthoring::CatalogEntry> catalog;
    std::string cat_err;
    StaticPropAuthoring::LoadCatalog(S.root_dir, catalog, cat_err);

    auto find_entry = [&](const std::string& name)
        -> const StaticPropAuthoring::CatalogEntry* {
        for (const auto& e : catalog)
            if (e.internal_name == name) return &e;
        return nullptr;
    };

    if (const auto* existing = find_entry(id)) {
        if (existing->model_path == mdl_vpath) {
            res.entity_id = id;
            res.gdb_template_created = true;
            res.notes.push_back("entity " + id +
                                " already exists in globals.gdb and points "
                                "at this model - ready to place");
            return;
        }
        if (explicit_id) {
            res.notes.push_back(
                "GDB TEMPLATE SKIPPED: entity ID '" + id +
                "' already uses a different model (" +
                existing->model_path + "). Pick another ID.");
            return;
        }
        int n = 2;
        std::string candidate;
        do {
            candidate = id + "_" + std::to_string(n++);
        } while (find_entry(candidate));
        id = candidate;
    }

    StaticPropAuthoring::Definition def;
    def.internal_name = id;
    def.model_path = mdl_vpath;
    StaticPropAuthoring::CatalogEntry saved;
    std::string result, error;
    if (StaticPropAuthoring::Save(S.root_dir, def, saved, result, error)) {
        res.entity_id = id;
        res.gdb_template_created = true;
        res.notes.push_back("spawnable entity " + id +
                            " created in globals.gdb");
    } else {
        res.notes.push_back("GDB TEMPLATE FAILED: " + error);
    }
}
