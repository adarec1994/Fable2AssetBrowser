bool is_gdb_authored_level_shell_model(
    const std::string& model_path,
    const std::unordered_set<std::string>& authored_level_model_paths)
{
    const std::string p = lower_slash(model_path);
    if (authored_level_model_paths.find(p) ==
        authored_level_model_paths.end())
    {
        return false;
    }

    return p.find("/buildings/") != std::string::npos ||
           p.find("/structures/") != std::string::npos;
}

bool is_gdb_shell_audit_model(const std::string& model_path)
{
    const std::string p = lower_slash(model_path);
    return p.find("/buildings/") != std::string::npos ||
           p.find("/structures/") != std::string::npos;
}

bool is_gdb_static_prop_reject_model(const std::string& model_path)
{
    const std::string p = lower_slash(model_path);
    return p.find("art/characters/") == 0 ||
           p.find("/art/characters/") != std::string::npos ||
           p.find("/characters/heros/") != std::string::npos;
}

bool is_gdb_unique_entity_shell_model(const std::string& model_path)
{
    const std::string p = lower_slash(model_path);
    if (p.find("/buildings/") == std::string::npos &&
        p.find("/structures/") == std::string::npos)
    {
        return false;
    }

    return p.find("/exterior.mdl") != std::string::npos ||
           p.find("/interior.mdl") != std::string::npos ||
           p.find("bs_market_gatehouse") != std::string::npos ||
           p.find("bs_market_clocktower") != std::string::npos ||
           p.find("bs_market_platform") != std::string::npos ||
           p.find("bs_market_largeshop") != std::string::npos ||
           p.find("bs_market_smallshop") != std::string::npos ||
           p.find("bs_market_generalshop") != std::string::npos ||
           p.find("bs_market_tavern") != std::string::npos ||
           p.find("bs_market_tarotstall") != std::string::npos ||
           p.find("bs_townhouse") != std::string::npos;
}

bool is_implausible_container_shell_model(const std::string& model_path)
{
    const std::string p = lower_slash(model_path);
    return p.find("bridge") != std::string::npos ||
           p.find("facade") != std::string::npos ||
           p.find("clocktower") != std::string::npos ||
           p.find("/buildings/") != std::string::npos ||
           p.find("/exterior.mdl") != std::string::npos ||
           p.find("/interior.mdl") != std::string::npos;
}

bool compact_key_is_or_numbered(const std::string& key, const char* base)
{
    const size_t n = std::strlen(base);
    if (key == base) return true;
    if (key.size() <= n || key.compare(0, n, base) != 0) {
        return false;
    }
    for (size_t i = n; i < key.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(key[i]))) {
            return false;
        }
    }
    return true;
}

bool compact_key_is_or_variant(const std::string& key, const char* base)
{
    if (compact_key_is_or_numbered(key, base)) return true;
    const size_t n = std::strlen(base);
    if (key.size() <= n + 1 || key.compare(0, n, base) != 0 ||
        key[n] != 'v')
    {
        return false;
    }
    for (size_t i = n + 1; i < key.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(key[i]))) {
            return false;
        }
    }
    return true;
}

bool is_bad_market_helper_substitution(const std::string& entity_key,
                                       const std::string& raw_key,
                                       const std::string& model_path)
{
    const std::string model_key = compact_match_key(model_path);

    const bool sign_entity =
        entity_key.find("sign") != std::string::npos ||
        raw_key.find("sign") != std::string::npos;
    const bool door_entity =
        entity_key.find("door") != std::string::npos ||
        raw_key.find("door") != std::string::npos;

    const bool general_store_building =
        compact_key_is_or_numbered(entity_key, "generalstore") ||
        compact_key_is_or_numbered(raw_key, "objectbuildinggeneralstore") ||
        compact_key_is_or_numbered(raw_key, "newobjectbuildinggeneralstore");
    if (model_key.find("bsmarketgeneralshop") != std::string::npos &&
        (model_key.find("exterior") != std::string::npos ||
         model_key.find("interior") != std::string::npos) &&
        !general_store_building)
    {
        return true;
    }
    if (general_store_building && !sign_entity &&
        model_key.find("signgeneralstore") != std::string::npos)
    {
        return true;
    }

    const bool tavern_building =
        entity_key.find("bsmarkettavern") != std::string::npos ||
        entity_key.find("markettavern") != std::string::npos ||
        raw_key.find("objectbuildingbsmarkettavern") != std::string::npos ||
        raw_key.find("newobjectbuildingbsmarkettavern") != std::string::npos;
    if (model_key.find("bsmarkettavern") != std::string::npos &&
        !tavern_building)
    {
        return true;
    }
    if (tavern_building && !sign_entity &&
        (model_key.find("botavernsign") != std::string::npos ||
         model_key.find("tavernsign") != std::string::npos))
    {
        return true;
    }

    const bool tarot_stall_building =
        entity_key.find("tarotstall") != std::string::npos ||
        raw_key.find("tarotstall") != std::string::npos;
    if (tarot_stall_building && !door_entity &&
        model_key.find("tarotstalldoors") != std::string::npos)
    {
        return true;
    }

    const bool market_stall_building =
        compact_key_is_or_variant(entity_key, "bsopenstall") ||
        compact_key_is_or_variant(entity_key, "bsmarketopenstall") ||
        compact_key_is_or_variant(entity_key, "bsmarketstall") ||
        raw_key.find("objectbuildingbsopenstall") != std::string::npos ||
        raw_key.find("objectbuildingbsmarketstall") != std::string::npos ||
        raw_key.find("newobjectbuildingbsmarketstall") != std::string::npos;
    const bool bs_market_stall_shell =
        (model_key.find("bsmarketmarketstall") != std::string::npos ||
         model_key.find("bsmarketopenstall") != std::string::npos ||
         model_key.find("bsopenstall") != std::string::npos) &&
        model_key.find("esashopmarketstall") == std::string::npos;
    if (bs_market_stall_shell && !market_stall_building) {
        return true;
    }
    if ((general_store_building || tavern_building ||
         market_stall_building || tarot_stall_building) &&
        (model_key.find("bstownhouse") != std::string::npos ||
         model_key.find("townhouse") != std::string::npos))
    {
        return true;
    }

    return false;
}

bool is_unindexed_shell_fallback_entity(const std::string& entity_key,
                                        const std::string& raw_key)
{
    const std::string text = entity_key + " " + raw_key;
    auto has = [&](const char* needle) {
        return text.find(needle) != std::string::npos;
    };

    if (has("canopy") || has("counter") || has("stairsfloor") ||
        has("door") || has("sign"))
    {
        return false;
    }

    return has("objectbuilding") ||
           has("newobjectbuilding") ||
           has("bsmarkettavern") ||
           has("generalstore") ||
           has("generalshop") ||
           has("largeshop") ||
           has("smallshop") ||
           has("townhouse") ||
           has("slumstreethouse") ||
           has("gatehouse") ||
           has("clocktower");
}
