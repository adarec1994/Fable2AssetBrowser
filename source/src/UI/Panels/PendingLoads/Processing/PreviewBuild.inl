    if (S.pending_preview_build) {
        S.pending_preview_build = false;
        const bool capture_model_tab =
            S.pending_model_tab_capture.exchange(false);
        const bool preserve_hero_camera =
            ContentTabs::ActiveKind() == ContentTabs::Kind::Hero &&
            g_mp.has_model;
#ifdef _WIN32
        try {
            if (!g_mp_initialized) {
                g_mp_initialized = MP_Init(device, g_mp, 800, 600);
            }
            if (g_mp_initialized) {
                const bool built =
                    MP_Build(device, S.mdl_meshes, S.mdl_info, g_mp);
                if (built && capture_model_tab) {
                    ContentTabs::CaptureCurrentModel();
                }
                S.show_model_preview = false;
                S.model_preview_open = false;
                S.model_materials_open = false;
                S.terrain_mode = false;
                g_mp.no_tilt = false;
                if (!preserve_hero_camera) {
                    S.cam_yaw = 3.14159265f;
                    S.cam_pitch = 0.2f;
                    S.cam_dist = 3.0f;
                }
            }
        } catch (const std::exception& e) {
            OutputLog::error(std::string("MDL preview build failed: ") +
                             e.what());
        } catch (...) {
            OutputLog::error("MDL preview build failed: unknown exception");
        }
#else
        MP_Release(g_mp);
        g_mp_initialized = MP_Init(g_mp, 800, 600);
        if (g_mp_initialized) {
            const bool built = MP_Build(S.mdl_meshes, S.mdl_info, g_mp);
            if (built && capture_model_tab) {
                ContentTabs::CaptureCurrentModel();
            }
            S.show_model_preview = false;
            S.model_preview_open = false;
            S.model_materials_open = false;
            S.terrain_mode = false;
            g_mp.no_tilt = false;
            if (!preserve_hero_camera) {
                S.cam_yaw = 3.14159265f;
                S.cam_pitch = 0.2f;
                S.cam_dist = 3.0f;
            }
        }
#endif
    }
