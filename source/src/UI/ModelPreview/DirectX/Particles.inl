void MP_BuildLevelFx(ID3D11Device* dev, ModelPreview& mp){
    mp.fx_system.clear();
    for (auto& kv : mp.fx_tex_srv) { if (kv.second) kv.second->Release(); }
    mp.fx_tex_srv.clear();
    if (g_pending_level_fx.empty() || !g_particle_bank_loaded) return;

    mp.fx_system.build(g_particle_bank, g_pending_level_fx);

    const std::string preferred =
        (S.selected_nested_index != -1 && !S.selected_nested_temp_path.empty())
            ? S.selected_nested_temp_path : S.selected_bnk;
    for (const auto& t : mp.fx_system.textures()) {
        if (t.empty() || mp.fx_tex_srv.count(t)) continue;
        std::vector<unsigned char> buf;
        if (!build_any_tex_buffer_for_name(t, buf, preferred)) {
            mp.fx_tex_srv[t] = nullptr; continue;
        }
        bool ha = false; ID3D11ShaderResourceView* srv = nullptr;
        srv_from_tex_blob_auto(dev, buf, &srv, &ha);
        mp.fx_tex_srv[t] = srv;
    }
    mp.fx_last_time = 0.0;

    {
        std::map<std::string, int> resolved, unresolved;
        for (const auto& p : g_pending_level_fx)
            (p.resolved ? resolved : unresolved)[p.effect_name]++;
        auto join = [](const std::map<std::string, int>& m) {
            std::string s; int shown = 0;
            for (const auto& kv : m) {
                if (shown++) s += ", ";
                s += kv.first;
                if (kv.second > 1) s += " x" + std::to_string(kv.second);
                if (shown >= 40) { s += ", ..."; break; }
            }
            return s;
        };
        OutputLog::success("fx: built " +
            std::to_string(mp.fx_system.instance_count()) +
            " instance(s); " + std::to_string(mp.fx_system.resolved_count()) +
            " resolved to bank effects, " +
            std::to_string(unresolved.size()) + " distinct unresolved");
        if (!resolved.empty())
            OutputLog::info("fx resolved: " + join(resolved));
        if (!unresolved.empty())
            OutputLog::warn("fx UNRESOLVED (name heuristic only): " +
                            join(unresolved));
    }
}
