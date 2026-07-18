FrameState EvaluateFrame(ModelPreview& preview)
{
    FrameState frame{};
    static const auto sky_start_time =
        std::chrono::steady_clock::now();
    frame.elapsed_time = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - sky_start_time).count();

    std::copy(std::begin(preview.sky_top_colour),
              std::end(preview.sky_top_colour),
              std::begin(frame.sky_top));
    std::copy(std::begin(preview.sky_bottom_colour),
              std::end(preview.sky_bottom_colour),
              std::begin(frame.sky_bottom));
    std::copy(std::begin(preview.sky_sunset_colour),
              std::end(preview.sky_sunset_colour),
              std::begin(frame.sky_sunset));
    std::copy(std::begin(preview.sky_params),
              std::end(preview.sky_params),
              std::begin(frame.sky_params));
    std::copy(std::begin(preview.main_light_colour),
              std::end(preview.main_light_colour),
              std::begin(frame.main_light_colour));
    std::copy(std::begin(preview.sky_element_params),
              std::end(preview.sky_element_params),
              std::begin(frame.element_params));
    for (int layer = 0; layer < 4; ++layer) {
        for (int field = 0; field < 4; ++field) {
            frame.cloud_layer[layer][field] =
                preview.cloud_layer[layer][field];
            frame.cloud_shape[layer][field] =
                preview.cloud_shape[layer][field];
            frame.cloud_motion[layer][field] =
                preview.cloud_motion[layer][field];
            frame.cloud_light[layer][field] =
                preview.cloud_light[layer][field];
        }
        frame.cloud_density_token[layer] =
            preview.cloud_density_token[layer];
    }
    frame.has_cloud_theme = preview.has_cloud_theme;
    frame.mist = preview.weather_mist_strength;
    frame.has_fog = preview.has_fog_theme;
    std::copy(std::begin(preview.weather_precip),
              std::end(preview.weather_precip),
              std::begin(frame.weather));
    std::copy(std::begin(preview.fog_range),
              std::end(preview.fog_range),
              std::begin(frame.fog_range));
    std::copy(std::begin(preview.fog_density),
              std::end(preview.fog_density),
              std::begin(frame.fog_density));

    frame.time_of_day = std::isfinite(preview.sky_time_of_day)
        ? preview.sky_time_of_day : 0.5f;
    frame.time_of_day -= std::floor(frame.time_of_day);
    const bool has_time_override =
        preview.time_of_day_override &&
        std::isfinite(preview.time_of_day_override_value);
    if (has_time_override) {
        frame.time_of_day = preview.time_of_day_override_value;
        frame.time_of_day -= std::floor(frame.time_of_day);
        if (frame.time_of_day < 0.0f) frame.time_of_day += 1.0f;
    }

    if (preview.has_day_night_cycle &&
        preview.day_night_keyframes.size() >= 2) {
        const float cycle_seconds =
            std::max(preview.day_night_cycle_seconds, 1.0f);
        if (!has_time_override) {
            frame.time_of_day = static_cast<float>(
                frame.elapsed_time / static_cast<double>(cycle_seconds));
            frame.time_of_day -= std::floor(frame.time_of_day);
        }

        const auto& keys = preview.day_night_keyframes;
        std::size_t a = keys.size() - 1;
        std::size_t b = 0;
        for (std::size_t i = 0; i < keys.size(); ++i) {
            if (frame.time_of_day < keys[i].time_of_day) {
                b = i;
                a = (i == 0) ? keys.size() - 1 : i - 1;
                break;
            }
        }

        float span = keys[b].time_of_day - keys[a].time_of_day;
        if (span <= 0.0f) span += 1.0f;
        float position = frame.time_of_day - keys[a].time_of_day;
        if (position < 0.0f) position += 1.0f;
        const float key_t = span > 0.0001f
            ? std::clamp(position / span, 0.0f, 1.0f)
            : 0.0f;
        auto lerp = [key_t](float value_a, float value_b) {
            return value_a + (value_b - value_a) * key_t;
        };

        for (int i = 0; i < 3; ++i) {
            frame.sky_top[i] = lerp(
                keys[a].sky_top_colour[i], keys[b].sky_top_colour[i]);
            frame.sky_bottom[i] = lerp(
                keys[a].sky_bottom_colour[i], keys[b].sky_bottom_colour[i]);
            frame.sky_sunset[i] = lerp(
                keys[a].sky_sunset_colour[i], keys[b].sky_sunset_colour[i]);
            frame.main_light_colour[i] = lerp(
                keys[a].main_light_colour[i],
                keys[b].main_light_colour[i]);
        }
        for (int i = 0; i < 4; ++i) {
            frame.sky_params[i] = lerp(
                keys[a].sky_params[i], keys[b].sky_params[i]);
            frame.weather[i] = lerp(
                keys[a].weather_precip[i], keys[b].weather_precip[i]);
            frame.fog_range[i] = lerp(
                keys[a].fog_range[i], keys[b].fog_range[i]);
        }
        for (int i = 0; i < 16; ++i) {
            frame.element_params[i] = lerp(
                keys[a].element_params[i], keys[b].element_params[i]);
        }
        frame.has_cloud_theme =
            keys[a].has_cloud_theme || keys[b].has_cloud_theme;
        frame.mist = lerp(keys[a].weather_mist_strength,
                          keys[b].weather_mist_strength);
        frame.fog_density[0] = lerp(
            keys[a].fog_density[0], keys[b].fog_density[0]);
        frame.fog_density[1] = lerp(
            keys[a].fog_density[1], keys[b].fog_density[1]);
        frame.has_fog =
            keys[a].has_fog_theme || keys[b].has_fog_theme;

        for (int layer = 0; layer < 4; ++layer) {
            for (int field = 0; field < 4; ++field) {
                frame.cloud_layer[layer][field] = lerp(
                    keys[a].cloud_layer[layer][field],
                    keys[b].cloud_layer[layer][field]);
                frame.cloud_shape[layer][field] = lerp(
                    keys[a].cloud_shape[layer][field],
                    keys[b].cloud_shape[layer][field]);
                frame.cloud_motion[layer][field] = lerp(
                    keys[a].cloud_motion[layer][field],
                    keys[b].cloud_motion[layer][field]);
                frame.cloud_light[layer][field] = lerp(
                    keys[a].cloud_light[layer][field],
                    keys[b].cloud_light[layer][field]);
            }

            const std::uint32_t token_a =
                keys[a].cloud_density_token[layer];
            const std::uint32_t token_b =
                keys[b].cloud_density_token[layer];
            const std::uint32_t selected_token =
                CloudRuntime::SelectDensityToken({
                    {1.0f - key_t, token_a}, {key_t, token_b}});
            const bool select_b = selected_token == token_b &&
                                  selected_token != token_a;
            const MPSkyCloudKeyframe& resource_key =
                select_b ? keys[b] : keys[a];
            const std::string& selected_name =
                resource_key.cloud_density_tex_name[layer];
            frame.cloud_density_token[layer] = selected_token;
            if (preview.cloud_density_tex_name[layer] != selected_name) {
                if (preview.cloud_density_srv[layer]) {
                    preview.cloud_density_srv[layer]->Release();
                    preview.cloud_density_srv[layer] = nullptr;
                }
                preview.cloud_density_tex_name[layer] = selected_name;
                preview.cloud_density_tried[layer] = false;
            }
            preview.cloud_density_token[layer] = selected_token;
        }
    }
    preview.current_time_of_day = frame.time_of_day;

    const float degrees_to_radians = XM_PI / 180.0f;
    const float time_factor =
        (preview.has_sun_axis && std::isfinite(preview.sun_axis[3]) &&
         preview.sun_axis[3] > 0.0f)
        ? preview.sun_axis[3] : 1.0f;
    const float theta =
        (frame.time_of_day * time_factor - 0.5f) * XM_2PI;
    const float elevation =
        (preview.has_sun_axis ? preview.sun_axis[0] : 26.0f) *
        degrees_to_radians;
    const float phi =
        (preview.has_sun_axis
            ? preview.sun_axis[1] + preview.sun_axis[2]
            : 0.0f) * degrees_to_radians;
    const float cos_theta = std::cos(theta);
    const float sin_theta = std::sin(theta);
    const float ground_x = cos_theta * std::cos(elevation);
    const float up = cos_theta * std::sin(elevation);
    const float ground_z = sin_theta;
    const float cos_phi = std::cos(phi);
    const float sin_phi = std::sin(phi);
    const XMVECTOR sun = XMVector3Normalize(XMVectorSet(
        ground_x * cos_phi - ground_z * sin_phi,
        up,
        ground_x * sin_phi + ground_z * cos_phi,
        0.0f));
    XMFLOAT3 sun_f{};
    XMStoreFloat3(&sun_f, sun);
    frame.sun_direction[0] = sun_f.x;
    frame.sun_direction[1] = sun_f.y;
    frame.sun_direction[2] = sun_f.z;

    if (preview.has_moon_axis) {
        const float moon_elevation =
            preview.moon_axis[0] * degrees_to_radians;
        const float moon_phi =
            (preview.moon_axis[1] + preview.moon_axis[2]) *
            degrees_to_radians;
        const float moon_ground_x = cos_theta * std::cos(moon_elevation);
        const float moon_up = cos_theta * std::sin(moon_elevation);
        const float cos_moon_phi = std::cos(moon_phi);
        const float sin_moon_phi = std::sin(moon_phi);
        const XMVECTOR moon = XMVector3Normalize(XMVectorSet(
            -(moon_ground_x * cos_moon_phi - ground_z * sin_moon_phi),
            -moon_up,
            -(moon_ground_x * sin_moon_phi + ground_z * cos_moon_phi),
            0.0f));
        XMFLOAT3 moon_f{};
        XMStoreFloat3(&moon_f, moon);
        frame.moon_direction[0] = moon_f.x;
        frame.moon_direction[1] = moon_f.y;
        frame.moon_direction[2] = moon_f.z;
    } else {
        frame.moon_direction[0] = -sun_f.x;
        frame.moon_direction[1] = -sun_f.y;
        frame.moon_direction[2] = -sun_f.z;
    }
    return frame;
}
