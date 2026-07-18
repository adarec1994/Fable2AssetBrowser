int streaming_model_score(const std::string& entity_name,
                          const StreamingModelCandidate& c)
{
    const std::string entity_key = gdb_entity_key(entity_name);
    if (entity_key.empty() || c.key.empty()) return INT_MIN;

    auto has = [&](const char* needle) {
        return entity_key.find(needle) != std::string::npos;
    };
    auto cand_has = [&](const char* needle) {
        return c.key.find(needle) != std::string::npos;
    };
    const std::string& path_key = c.path_key;
    auto cand_path_has = [&](const char* needle) {
        return path_key.find(needle) != std::string::npos;
    };
    auto key_is_or_numbered = [&](const char* base) {
        const size_t n = std::strlen(base);
        if (entity_key == base) return true;
        if (entity_key.size() <= n ||
            entity_key.compare(0, n, base) != 0)
        {
            return false;
        }
        for (size_t i = n; i < entity_key.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(entity_key[i]))) {
                return false;
            }
        }
        return true;
    };
    auto key_is_or_variant = [&](const char* base) {
        if (key_is_or_numbered(base)) return true;
        const size_t n = std::strlen(base);
        if (entity_key.size() <= n + 1 ||
            entity_key.compare(0, n, base) != 0 ||
            entity_key[n] != 'v')
        {
            return false;
        }
        for (size_t i = n + 1; i < entity_key.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(entity_key[i]))) {
                return false;
            }
        }
        return true;
    };

    if (key_is_or_numbered("generalstore") &&
        (cand_has("signgeneralstore") || cand_path_has("signgeneralstore")))
    {
        return INT_MIN;
    }

    int score = INT_MIN;

    const bool bare_general_store = key_is_or_numbered("generalstore");
    const bool market_tavern_shell =
        key_is_or_numbered("bsmarkettavern") ||
        key_is_or_numbered("markettavern");
    const bool market_openstall_shell =
        key_is_or_variant("bsopenstall") ||
        key_is_or_variant("bsmarketopenstall");
    const bool market_stall_shell =
        key_is_or_variant("bsmarketstall") ||
        key_is_or_variant("marketstall") ||
        key_is_or_numbered("bstarotstall") ||
        key_is_or_numbered("tarotstall");

    auto path_has_any = [&](std::initializer_list<const char*> needles) {
        for (const char* needle : needles) {
            if (cand_path_has(needle)) return true;
        }
        return false;
    };
    if ((bare_general_store || market_tavern_shell ||
         market_openstall_shell || market_stall_shell) &&
        path_has_any({"bstownhouse", "townhouse"}))
    {
        return INT_MIN;
    }
    auto same_variant_bonus = [&](int base_score) {
        int adjusted = base_score;
        for (const char* v :
             {"v1", "v2", "v3", "v4", "v5", "v6"})
        {
            if (entity_key.find(v) != std::string::npos &&
                path_key.find(v) != std::string::npos)
            {
                adjusted += 500;
            }
        }
        return adjusted;
    };

    if (bare_general_store) {
        return INT_MIN;
    }
    if (market_tavern_shell) {
        return INT_MIN;
    }
    if (market_openstall_shell) {
        if (cand_path_has("esashopmarketstall") ||
            cand_path_has("signstall"))
        {
            return INT_MIN;
        }
        if (cand_path_has("bsopenstall") ||
            cand_path_has("bsmarketopenstall") ||
            cand_path_has("openstall"))
        {
            score = std::max(score, same_variant_bonus(18500));
        }
    }
    if (market_stall_shell) {
        if (cand_path_has("esashopmarketstall") ||
            cand_path_has("signstall"))
        {
            return INT_MIN;
        }
        if (entity_key.find("tarotstall") != std::string::npos &&
            cand_path_has("tarotstall"))
        {
            score = std::max(score, same_variant_bonus(19000));
        } else if ((cand_path_has("bsmarketstall") ||
                    cand_path_has("marketstall")) &&
                   !cand_path_has("esashopmarketstall"))
        {
            score = std::max(score, same_variant_bonus(18500));
        }
    }

    if (entity_key == c.key) {
        score = 12000;
    } else if (c.key.find(entity_key) != std::string::npos) {
        score = 9000 + int(entity_key.size());
    } else if (entity_key.find(c.key) != std::string::npos) {
        score = 7000 + int(c.key.size());
    }

    struct Alias { const char* entity; const char* model; int score; };
    static const Alias aliases[] = {
        { "smallwallpost",         "stonewallmediumpostspiked",     15000 },
        { "wallpost",              "stonewallmediumpostspiked",     14500 },
        { "smallwallstraight",     "stonewallmediumstraightspiked", 15000 },
        { "smallwallcurved",       "stonewallmediumcurvedspiked",   15000 },
        { "smallwallcorner",       "stonewallmediumcurvedspiked",   14500 },
        { "smallwallbroken",       "stonewallmediumbrokenspiked",   15000 },
        { "shelflong",             "esashelflong",                  15000 },
        { "woodenbucket",          "esabucketwooden",               15000 },
        { "lightsceiling",         "bslightceiling",                15000 },
        { "lightfixingceiling",    "bslightceiling",                15000 },
        { "candleholder",          "bscandleholder",                14000 },
        { "grainsack",             "esasackgrain",                  14500 },
        { "shippingcrate",         "esashippingcrate",              14500 },
        { "weaponrackwallmulti",   "esashopweaponswallrackmulti",   15000 },
        { "weaponrackwallsingle",  "esashopweaponswallracksingle",  15000 },
        { "weaponrack",            "esashopweaponrack",             13500 },
        { "booksgroup",            "esabooksblock",                 13000 },
        { "pubtable",              "esatabletavern",                14500 },
        { "largesquareultradecorative","esaftableultradecorative",  13800 },
        { "largesquareupgradeable","esaftabledecorative",           12500 },
        { "standardultradecorative","esaftableultradecorative",     13600 },
        { "standardupgradeable",   "esaftabledecorative",           12300 },
        { "bookcaseultradecorative","esafbookcaseultradecorative",  15000 },
        { "bookcaseworn",          "esafbookcaseworn",              14500 },
        { "dresserupgradeable",    "esafdresserultradecorative",    13000 },
        { "kitchensinkupgradeable", "esakitchensink",               12000 },
        { "buildingsalesign",      "buildingsalesign",              14000 },
        { "bsmarketbridge",        "bsmarketbridge",                16000 },
        { "marketbridge",          "bsmarketbridge",                15800 },
        { "bridge",                "bsmarketbridge",                12000 },
        { "bsmarketclocktower",    "bsmarketclocktower",            16000 },
        { "marketclocktower",      "bsmarketclocktower",            15800 },
        { "clocktower",            "bsmarketclocktower",            14500 },
        { "grandfatherclock",      "bsgrandfatherclock",            15500 },
        { "wallclock",             "bswallclock",                   15500 },
        { "bsmarketdockarch",      "bsmarketdocksarch",             15000 },
        { "dockarch",              "bsmarketdocksarch",             14500 },
        { "bsmarketarchway",       "bsmarketarchway",               15000 },
        { "archway",               "bsmarketarchway",               13000 },
        { "bsmarketgatehouse",     "bsmarketgatehouse",             15000 },
        { "bsgatehouse",           "bsmarketgatehouse",             14500 },
        { "bsmarketlockgate",      "bsmarketlockgates",             15000 },
        { "lockgate",              "bsmarketlockgates",             14000 },
        { "bsmarketwalltower",     "bsmarketwalltower",             15000 },
        { "walltower",             "bsmarketwalltower",             13500 },
        { "bsmarketwallgate",      "bsmarketwallgate",              15000 },
        { "closedgate",            "bsmarketwallgate",              13000 },
        { "guardpost",             "bsmarketguardpost",             14500 },
        { "marketstairs",          "bsmarketstairs",                14500 },
        { "generalstorestairsfloor","bsmarketgeneralshopstairsfloor",14500 },
        { "generalshopstairsfloor", "bsmarketgeneralshopstairsfloor",14500 },
        { "bsopenstall",           "openstall",                     14500 },
        { "openstall",             "openstall",                     14000 },
        { "bsmarketstall",         "bsmarketstall",                 14500 },
        { "marketstall",           "marketstall",                   14000 },
        { "tarotstall",            "tarotstall",                    15000 },
        { "scaffoldingstairs",     "bsmarketscaffoldingstairs",     14500 },
        { "scaffoldstairs",        "bsmarketscaffoldingstairs",     14500 },
        { "scaffoldstraight",      "bsmarketscaffoldingstraight",   14000 },
        { "marketwalljoiner",      "bsmarketwallbuffer",            13500 },
        { "walljoiner",            "bsmarketwallbuffer",            13000 },
        { "castlearch",            "bsmarketcastlearch",            14500 },
        { "dockswall",             "bsmarketdockswall",             14500 },
        { "dockwall",              "bsmarketdockswall",             14500 },
        { "bsdockwall",            "bsmarketdockswall",             14500 },
        { "slumswall",             "bsslumsthinwallv1",             14000 },
        { "slumsthinwall",         "bsslumsthinwallv1",             14500 },
        { "windowsmallarched",     "esasmarchedwin",                14000 },
        { "smallarchedwin",        "esasmarchedwin",                14000 },
        { "smarchedwin",           "esasmarchedwin",                14000 },
        { "marketdocksjetty",      "bsmarketdocksjetty",            14000 },
        { "docksjetty",            "bsmarketdocksjetty",            13500 },
        { "docksplatform",         "bsmarketdocksplatform",         13500 },
        { "dockscrane",            "bsmarketdockscrane",            13500 },
        { "oillanternsingle",      "bscemetaryoillampsingle",       13000 },
        { "oillampsingle",         "bscemetaryoillampsingle",       13000 },
        { "statue",                "okstatuedolphinv1",             12000 },
        { "cellarlargeroom",       "cellarlargeroom",               15000 },
        { "cellarsmallroom",       "cellarsmallroom",               15000 },
        { "bsmarkettownhousesmall", "bstownhousebasicfacademid",    13500 },
        { "bwsmarkettownhousesmall","bstownhousebasicfacademid",    13500 },
        { "townhousev1",           "bstownhousev1facademid",        14000 },
        { "townhousev2",           "bstownhousev2facademid",        14000 },
        { "townhousev3",           "bstownhousev3exterior",         14500 },
    };
    for (const auto& a : aliases) {
        if (has(a.entity) && (cand_has(a.model) || cand_path_has(a.model))) {
            score = std::max(score, a.score);
        }
    }

    if (score == INT_MIN) return score;
    if (c.entry) score += 500;
    if (c.from_gmd) score += 150;
    if (has("facademid") && path_key.find("facademid") != std::string::npos) {
        score += 500;
    }
    if (has("facade") && path_key.find("facade") != std::string::npos) {
        score += 150;
    }
    return score - int(std::min<size_t>(c.hint_path.size(), 200));
}

const StreamingModelCandidate*
choose_streaming_model_for_gdb(const std::string& entity_name,
                               const std::vector<StreamingModelCandidate>& candidates,
                               int* out_score,
                               uint32_t parent_hash)
{
    auto path_suffix_matches = [](const std::string& path,
                                  const std::string& target) {
        if (path.empty() || target.empty()) return false;
        if (path == target) return true;
        return path.size() > target.size() &&
               path.compare(path.size() - target.size(),
                            target.size(), target) == 0 &&
               (path[path.size() - target.size() - 1] == '/' ||
                path[path.size() - target.size() - 1] == '\\');
    };

    auto choose_curated_override =
        [&](const char* target_model_path, int* score_out) {
            if (!target_model_path || !*target_model_path) {
                return static_cast<const StreamingModelCandidate*>(nullptr);
            }
            const std::string target_lower = lower_slash(target_model_path);
            const std::string target_key =
                compact_match_key(model_name_from_path(target_model_path));
            const bool generic_shell_target =
                target_key == "exterior" || target_key == "interior";
            const StreamingModelCandidate* best = nullptr;
            int best_score = INT_MIN;
            for (const auto& c : candidates) {
                int score = INT_MIN;
                const std::string& resolved_lower = c.resolved_lower;
                const std::string& hint_lower = c.hint_lower;
                if (resolved_lower == target_lower) {
                    score = std::max(score, 50000);
                } else if (path_suffix_matches(resolved_lower, target_lower)) {
                    score = std::max(score, 49250);
                }
                if (hint_lower == target_lower) {
                    score = std::max(score, 49000);
                } else if (path_suffix_matches(hint_lower, target_lower)) {
                    score = std::max(score, 48250);
                }
                if (!target_key.empty() && !generic_shell_target) {
                    if (c.key == target_key) {
                        score = std::max(score, 46000);
                    }
                }
                if (score == INT_MIN) continue;
                if (c.entry) score += 500;
                if (c.from_gmd) score += 150;
                score -= int(std::min<size_t>(c.hint_path.size(), 200));
                if (!best || score > best_score) {
                    best = &c;
                    best_score = score;
                }
            }
            if (score_out) *score_out = best_score;
            return best;
        };

    const std::string entity_key = gdb_entity_key(entity_name);
    const char* curated_model =
        GdbModelHashlist::LookupParentHash(parent_hash);
    if (!curated_model) {
        curated_model = GdbModelHashlist::LookupEntityKey(entity_key);
    }
    if (curated_model && *curated_model) {
        if (const StreamingModelCandidate* curated =
                choose_curated_override(curated_model, out_score)) {
            return curated;
        }
        log_curated_hashlist_miss(entity_name, parent_hash, curated_model);
        if (out_score) *out_score = INT_MIN;
        return nullptr;
    }

    const StreamingModelCandidate* best = nullptr;
    int best_score = INT_MIN;
    for (const auto& c : candidates) {
        const int score = streaming_model_score(entity_name, c);
        if (!best || score > best_score) {
            best = &c;
            best_score = score;
        }
    }
    if (out_score) *out_score = best_score;
    return (best_score >= 6500) ? best : nullptr;
}
