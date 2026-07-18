    {
        extern std::atomic<bool>    g_pending_heightmap_view_load;
        extern std::vector<uint8_t> g_pending_heightmap_view_rgba;
        extern int                  g_pending_heightmap_view_w;
        extern int                  g_pending_heightmap_view_h;
        extern std::string          g_pending_heightmap_view_name;
        extern std::string          g_pending_heightmap_view_kind;

        extern ID3D11ShaderResourceView* g_heightmap_popout_srv;
        extern std::string               g_heightmap_popout_name;
        extern std::string               g_heightmap_popout_kind;
        extern int                       g_heightmap_popout_w;
        extern int                       g_heightmap_popout_h;
        extern bool                      g_heightmap_popout_open;
        extern std::vector<uint8_t>      g_heightmap_popout_rgba;

        if (g_pending_heightmap_view_load.exchange(false)) {
            const int w = g_pending_heightmap_view_w;
            const int h = g_pending_heightmap_view_h;
            if (w > 0 && h > 0 &&
                g_pending_heightmap_view_rgba.size() == size_t(w) * size_t(h) * 4) {

                if (g_heightmap_popout_srv) {
                    g_heightmap_popout_srv->Release();
                    g_heightmap_popout_srv = nullptr;
                }
                g_heightmap_popout_srv =
                    create_srv_from_rgba(device, w, h,
                                         g_pending_heightmap_view_rgba);
                if (g_heightmap_popout_srv) {
                    g_heightmap_popout_w    = w;
                    g_heightmap_popout_h    = h;
                    g_heightmap_popout_name = g_pending_heightmap_view_name;
                    g_heightmap_popout_kind = g_pending_heightmap_view_kind;
                    g_heightmap_popout_rgba = std::move(g_pending_heightmap_view_rgba);
                    g_heightmap_popout_open = true;
                    OutputLog::success(g_heightmap_popout_kind + " opened: " +
                                       g_heightmap_popout_name + "  (" +
                                       std::to_string(w) + "x" +
                                       std::to_string(h) + ")");
                } else {
                    OutputLog::error(g_pending_heightmap_view_kind +
                                     ": failed to create SRV");
                }
            } else {
                OutputLog::error(g_pending_heightmap_view_kind +
                                 ": invalid RGBA payload (" +
                                 std::to_string(w) + "x" +
                                 std::to_string(h) + ")");
            }
            g_pending_heightmap_view_rgba.clear();
            g_pending_heightmap_view_name.clear();
            g_pending_heightmap_view_kind = "Heightmap";
            g_pending_heightmap_view_w = 0;
            g_pending_heightmap_view_h = 0;
        }
    }

