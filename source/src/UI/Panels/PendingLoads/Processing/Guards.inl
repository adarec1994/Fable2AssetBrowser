    const bool level_tab_active =
        ContentTabs::ActiveKind() == ContentTabs::Kind::Level;
    if (g_pending_mdl_load && level_tab_active &&
        level_edit_click_guard("Model loading")) {
        g_pending_mdl_load = false;
        g_pending_mdl_is_item = false;
        g_pending_mdl_index = -1;
        g_pending_mdl_full_path.clear();
    }
    if (g_pending_tex_load && level_tab_active &&
        level_edit_click_guard("Texture preview")) {
        g_pending_tex_load = false;
        g_pending_tex_index = -1;
    }
