    js << "  \"terrain\": {\n";
    js << "    \"mesh\": \"" << json_escape(terrain_file) << "\",\n";
    js << "    \"exported\": " << (terrain_ok ? "true" : "false") << ",\n";
    const std::string terrain_source =
        terrain_entry.full_path.empty() ? g_pending_terrain_label
                                        : terrain_entry.full_path;
    js << "    \"sourceEhf\": \"" << json_escape(to_slash(terrain_source)) << "\",\n";
    js << "    \"width\": " << ghf_w << ",\n";
    js << "    \"height\": " << ghf_h << ",\n";
    js << "    \"tileSize\": " << ghf_tile << ",\n";
    js << "    \"minHeight\": " << terrain_mesh.min_height << ",\n";
    js << "    \"maxHeight\": " << terrain_mesh.max_height << ",\n";
    js << "    \"materialsParsed\": " << (ehf_ok ? "true" : "false");
    if (ehf_ok) {
        js << ",\n    \"splat\": {\"file\":\"" << json_escape(splat_rel)
           << "\",\"width\":" << ehf.splat_w
           << ",\"height\":" << ehf.splat_h << "},\n";
        js << "    \"weightMaps\": {\"width\":" << weight_w
           << ",\"height\":" << weight_h << ",\"files\":[";
        for (size_t wi = 0; wi < weight_files.size(); ++wi) {
            if (wi) js << ",";
            js << "\"" << json_escape(weight_files[wi]) << "\"";
        }
        js << "]},\n";
        js << "    \"uvSets\": {\"tiled\":0,\"weights\":1},\n";
        js << "    \"paintResources\": [";
        for (size_t i = 0; i < ehf.paint_resources.size(); ++i) {
            if (i) js << ",";
            const auto& pr = ehf.paint_resources[i];
            js << "{\"width\":" << pr.width << ",\"height\":" << pr.height
               << ",\"pixelFormat\":" << pr.pixel_format << "}";
        }
        js << "],\n";
        js << "    \"lods\": [\n";
        for (size_t li = 0; li < ehf.lods.size(); ++li) {
            if (li) js << ",\n";
            const auto& lod = ehf.lods[li];
            js << "      {\"index\":" << li
               << ",\"materialFlags\":" << lod.material_flags
               << ",\"strings\":[";
            for (int si = 0; si < 6; ++si) {
                if (si) js << ",";
                js << "\"" << json_escape(to_slash(lod.strs[si])) << "\"";
            }
            js << "],\"files\":[";
            for (int si = 0; si < 6; ++si) {
                if (si) js << ",";
                js << "\"" << json_escape(lod_texture_files[li][si]) << "\"";
            }
            js << "],\"weight\":\"";
            if (li < weight_files.size()) {
                js << json_escape(weight_files[li]);
            }
            js << "\",\"params\":[["
               << lod.params[0][0] << "," << lod.params[0][1] << ","
               << lod.params[0][2] << "],["
               << lod.params[1][0] << "," << lod.params[1][1] << ","
               << lod.params[1][2] << "]]}";
        }
        js << "\n    ],\n";
        js << "    \"chunks\": [";
        for (size_t ci = 0; ci < ehf.chunks.size(); ++ci) {
            if (ci) js << ",";
            const auto& ch = ehf.chunks[ci];
            js << "{\"origin\":[" << ch.origin[0] << "," << ch.origin[1]
               << "," << ch.origin[2] << "],\"extent\":["
               << ch.extent[0] << "," << ch.extent[1] << ","
               << ch.extent[2] << "],\"layers\":[";
            for (size_t li = 0; li < ch.layers.size(); ++li) {
                if (li) js << ",";
                const auto& layer = ch.layers[li];
                js << "{\"material\":" << layer.material_idx
                   << ",\"name\":" << layer.name_idx
                   << ",\"tileUv\":[" << layer.tile_uv[0] << ","
                   << layer.tile_uv[1] << "],\"maskScale\":["
                   << layer.mask_scale[0] << "," << layer.mask_scale[1]
                   << "],\"textureIdx\":["
                   << int(layer.texture_idx[0]) << ","
                   << int(layer.texture_idx[1]) << ","
                   << int(layer.texture_idx[2]) << ","
                   << int(layer.texture_idx[3]) << "],\"blend\":["
                   << int(layer.blend[0]) << "," << int(layer.blend[1])
                   << "," << int(layer.blend[2]) << ","
                   << int(layer.blend[3]) << "]}";
            }
            js << "]}";
        }
        js << "]\n";
    } else {
        js << "\n";
    }
    js << "  }\n";
    js << "}\n";
