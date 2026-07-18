EntityGameplayOptions CollectEntityGameplayOptions(
    const std::vector<const std::vector<uint8_t>*>& gdbs)
{
    EntityGameplayOptions out;
    std::unordered_map<uint32_t, std::string> names;
    for (const auto* bytes : gdbs) {
        if (!bytes || bytes->empty()) continue;
        const GdbView view(*bytes);
        if (!view.ok) continue;
        const auto local = LoadEmbeddedDict(*bytes);
        names.insert(local.begin(), local.end());
    }
    auto starts = [](const std::string& value, const char* prefix) {
        const std::size_t length = std::strlen(prefix);
        return value.size() > length &&
               value.compare(0, length, prefix) == 0;
    };
    auto humanize = [](std::string value, const char* prefix) {
        value.erase(0, std::strlen(prefix));
        std::string label;
        label.reserve(value.size() + 8);
        for (std::size_t i = 0; i < value.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(value[i]);
            if (i != 0 && std::isupper(c) &&
                !std::isupper(static_cast<unsigned char>(value[i - 1]))) {
                label.push_back(' ');
            }
            label.push_back(value[i]);
        }
        return label;
    };
    for (const auto& [hash, name] : names) {




        if (starts(name, "CombatBalanceParams") &&
            name != "CombatBalanceParams") {
            out.combat_profiles.push_back(
                {hash, humanize(name, "CombatBalanceParams")});
        } else if (starts(name, "Faction") &&
                   name != "Faction" &&
                   name != "FactionComponent" &&
                   name != "FactionsEnabled" &&
                   name != "FactionsDisabled") {
            out.factions.push_back({hash, humanize(name, "Faction")});
        }
    }
    auto finish = [](std::vector<EntityGameplayOption>& options) {
        std::sort(options.begin(), options.end(),
                  [](const EntityGameplayOption& a,
                     const EntityGameplayOption& b) {
                      if (a.label != b.label) return a.label < b.label;
                      return a.record_hash < b.record_hash;
                  });
        options.erase(std::unique(options.begin(), options.end(),
                                  [](const EntityGameplayOption& a,
                                     const EntityGameplayOption& b) {
                                      return a.record_hash == b.record_hash;
                                  }),
                      options.end());
    };
    finish(out.factions);
    finish(out.combat_profiles);
    return out;
}
