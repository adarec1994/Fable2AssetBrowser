    std::string bake_note;
    std::vector<std::string> gen_asset_models;
    for (const auto& addition : s.additions) {
        if (addition.removed) continue;
        gen_asset_models.insert(gen_asset_models.end(),
                                addition.asset_models.begin(),
                                addition.asset_models.end());
    }
    for (const auto& ga : s.generators) {
        if (ga.removed) continue;
        for (const auto& mp : ga.asset_models) {
            gen_asset_models.push_back(mp);
        }
    }
