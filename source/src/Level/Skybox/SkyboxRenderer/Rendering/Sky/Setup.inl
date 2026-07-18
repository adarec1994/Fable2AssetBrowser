void DrawSky(ID3D11Device* device,
             ID3D11DeviceContext* context,
             ModelPreview& preview,
             const CameraFrame& camera,
             const FrameState& frame)
{
    auto load_cloud_density = [&](int index) {
        if (index < 0 || index >= 4) return;
        if (preview.cloud_density_srv[index] ||
            preview.cloud_density_tried[index]) {
            return;
        }
        preview.cloud_density_tried[index] = true;
        const std::string& texture_name =
            preview.cloud_density_tex_name[index];
        if (texture_name.empty()) {
            SkyDomeDebug("texload skip(empty name) cloud_density " +
                         std::to_string(index));
            return;
        }

        std::vector<unsigned char> texture_data;
        if (!build_any_tex_buffer_for_name(
                texture_name, texture_data, PreferredTextureBank())) {
            OutputLog::warn(
                "cloud density texture not found: " + texture_name);
            SkyDomeDebug("texload FAIL(not found) cloud_density name=" +
                         texture_name + " bank=" + PreferredTextureBank());
            return;
        }
        ID3D11ShaderResourceView* view = nullptr;
        CreateCloudSrv(device, texture_data, &view);
        if (!view) {
            OutputLog::error(
                "cloud density texture failed to decode: " + texture_name);
            return;
        }
        preview.cloud_density_srv[index] = view;
        OutputLog::info(
            "cloud density texture bound: " + texture_name);
    };

    if (frame.has_cloud_theme && preview.show_sky &&
        preview.vs_cloud && preview.ps_cloud) {
        for (int i = 0; i < 4; ++i) load_cloud_density(i);
    }

    const bool exact_dome_ready =
        preview.vs_sky_dome && preview.ps_sky_dome &&
        preview.cbuffer_sky_dome && preview.sky_lut_tex &&
        preview.sky_lut_srv;
    static int debug_frame = 0;
    const bool debug_now = (debug_frame < 5) || (debug_frame % 300 == 0);
    ++debug_frame;
    if (debug_now) {
        std::ostringstream log;
        log << "frame " << debug_frame
            << ": has_sky_theme=" << preview.has_sky_theme
            << " show_sky=" << preview.show_sky
            << " exact_ready=" << exact_dome_ready
            << " old_ready="
            << (preview.vs_sky && preview.ps_sky && preview.cbuffer_sky);
        SkyDomeDebug(log.str());
    }
    if (!preview.has_sky_theme || !preview.show_sky) {
        return;
    }
    if (!exact_dome_ready &&
        (!preview.vs_sky || !preview.ps_sky || !preview.cbuffer_sky)) {
        return;
    }

    auto load_sky_texture =
        [&](const std::string& texture_name,
            bool& tried,
            ID3D11ShaderResourceView*& target,
            const char* label,
            int* out_width = nullptr,
            int* out_height = nullptr) {
            if (target || tried) return;
            tried = true;
            if (texture_name.empty()) {
                SkyDomeDebug(std::string("texload skip(empty name) ") +
                             label);
                return;
            }

            std::vector<unsigned char> texture_data;
            if (!build_any_tex_buffer_for_name(
                    texture_name, texture_data, PreferredTextureBank())) {
                OutputLog::warn(
                    std::string(label) + " texture not found: " +
                    texture_name);
                SkyDomeDebug(std::string("texload FAIL(not found) ") +
                             label + " name=" + texture_name + " bank=" +
                             PreferredTextureBank());
                return;
            }
            ID3D11ShaderResourceView* view = nullptr;
            CreateOrdinarySrv(device, texture_data, &view,
                              out_width, out_height);
            if (!view) {
                OutputLog::error(
                    std::string(label) + " texture failed to decode: " +
                    texture_name);
                SkyDomeDebug(std::string("texload FAIL(decode) ") + label +
                             " name=" + texture_name);
                return;
            }
            target = view;
            OutputLog::info(
                std::string(label) + " texture bound: " + texture_name);
        };

    {
        int moon_width = 0;
        int moon_height = 0;
        const bool was_loaded = preview.sky_moon_srv != nullptr;
        load_sky_texture(preview.sky_moon_tex_name,
                         preview.sky_moon_tried,
                         preview.sky_moon_srv,
                         "sky moon", &moon_width, &moon_height);
        if (!was_loaded && preview.sky_moon_srv &&
            moon_width > 0 && moon_height > 0) {
            const float texture_aspect =
                static_cast<float>(moon_width) /
                static_cast<float>(moon_height);
            float tiles_x = 1.0f;
            float tiles_y = 1.0f;
            if (texture_aspect >= 7.0f) {
                tiles_x = 8.0f;
                tiles_y = 1.0f;
            } else if (texture_aspect >= 3.5f) {
                tiles_x = 4.0f;
                tiles_y = 1.0f;
            } else if (texture_aspect >= 1.8f) {
                tiles_x = 4.0f;
                tiles_y = 2.0f;
            } else if (texture_aspect <= 0.15f) {
                tiles_x = 1.0f;
                tiles_y = 8.0f;
            } else if (texture_aspect <= 0.6f) {
                tiles_x = 2.0f;
                tiles_y = 4.0f;
            }
            preview.sky_moon_tiles[0] = tiles_x;
            preview.sky_moon_tiles[1] = tiles_y;
        }
    }
    load_sky_texture(preview.sky_moon_glare_tex_name,
                     preview.sky_moon_glare_tried,
                     preview.sky_moon_glare_srv,
                     "sky moon glare");
    load_sky_texture(preview.sky_sun_disc_tex_name,
                     preview.sky_sun_disc_tried,
                     preview.sky_sun_disc_srv,
                     "sky sun disc");
    load_sky_texture(preview.sky_overlay_tex_name,
                     preview.sky_overlay_tried,
                     preview.sky_overlay_srv,
                     "sky overlay");
    load_sky_texture(preview.sky_sun_beams_tex_name,
                     preview.sky_sun_beams_tried,
                     preview.sky_sun_beams_srv,
                     "sky sun beams");
    load_sky_texture(preview.sky_sun_glare_tex_name,
                     preview.sky_sun_glare_tried,
                     preview.sky_sun_glare_srv,
                     "sky sun glare");

