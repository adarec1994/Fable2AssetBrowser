bool SaveWorkingCopy(std::string& msg) {
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (!s.available) {
        msg = "no level loaded";
        return false;
    }
    std::string werr;
    if (!write_additions(s, werr) || !write_spawns(s, werr)) {
        msg = werr;
        return false;
    }
    size_t models = 0;
    for (const auto& a : s.additions) {
        if (!a.removed) ++models;
    }
    size_t gens = 0;
    for (const auto& g : s.generators) {
        if (!g.removed) ++gens;
    }
    s.dirty = false;
    msg = "level saved: " + std::to_string(models) +
          " placed model(s), " + std::to_string(gens) + " generator(s)";
    return true;
}

void ClearEdits() {
    std::lock_guard<std::mutex> lk(mtx());
    st().edits.clear();
    st().undo_stack.clear();
    st().contents_edits.clear();
    st().contents_loot_edits.clear();
    st().dirty = false;
    ++st().revision;
}
