bool write_terrain_glb(const TerrainMesh& mesh,
                       const std::filesystem::path& path,
                       std::string& err)
{
    if (!mesh.ok || mesh.positions.empty() || mesh.indices.empty()) {
        err = "terrain mesh is empty";
        return false;
    }

    std::vector<uint8_t> bin;
    std::ostringstream views;
    std::ostringstream accessors;
    int view_count = 0;
    int acc_count = 0;

    auto add_view = [&](const void* data,
                        size_t bytes,
                        int target) -> int {
        const size_t off = append_aligned(bin, data, bytes);
        if (view_count > 0) views << ",";
        views << "{\"buffer\":0,\"byteOffset\":" << off
              << ",\"byteLength\":" << bytes;
        if (target) views << ",\"target\":" << target;
        views << "}";
        return view_count++;
    };

    auto add_float_accessor = [&](const std::vector<float>& data,
                                  int comps,
                                  const char* type,
                                  bool bounds) -> int {
        const int view = add_view(data.data(), data.size() * sizeof(float),
                                  34962);
        if (acc_count > 0) accessors << ",";
        accessors << "{\"bufferView\":" << view
                  << ",\"componentType\":5126,\"count\":"
                  << (data.size() / size_t(comps))
                  << ",\"type\":\"" << type << "\"";
        if (bounds && data.size() >= size_t(comps)) {
            std::vector<float> mn(comps, std::numeric_limits<float>::max());
            std::vector<float> mx(comps, -std::numeric_limits<float>::max());
            for (size_t i = 0; i + comps <= data.size(); i += comps) {
                for (int c = 0; c < comps; ++c) {
                    mn[c] = std::min(mn[c], data[i + c]);
                    mx[c] = std::max(mx[c], data[i + c]);
                }
            }
            accessors << ",\"min\":[";
            for (int c = 0; c < comps; ++c) {
                if (c) accessors << ",";
                accessors << mn[c];
            }
            accessors << "],\"max\":[";
            for (int c = 0; c < comps; ++c) {
                if (c) accessors << ",";
                accessors << mx[c];
            }
            accessors << "]";
        }
        accessors << "}";
        return acc_count++;
    };

    std::vector<float> terrain_uv01;
    const size_t vertex_count = mesh.positions.size() / 3;
    if (mesh.width > 1 && mesh.height > 1 &&
        vertex_count == size_t(mesh.width) * size_t(mesh.height)) {
        terrain_uv01.resize(vertex_count * 2);
        for (uint32_t y = 0; y < mesh.height; ++y) {
            for (uint32_t x = 0; x < mesh.width; ++x) {
                const size_t i = size_t(y) * mesh.width + x;
                terrain_uv01[i * 2 + 0] =
                    float(x) / float(mesh.width - 1);
                terrain_uv01[i * 2 + 1] =
                    float(y) / float(mesh.height - 1);
            }
        }
    }

    const int pos_acc = add_float_accessor(mesh.positions, 3, "VEC3", true);
    int norm_acc = -1;
    int uv_acc = -1;
    int uv_weight_acc = -1;
    if (mesh.normals.size() / 3 == mesh.positions.size() / 3) {
        norm_acc = add_float_accessor(mesh.normals, 3, "VEC3", false);
    }
    if (mesh.uvs.size() / 2 == mesh.positions.size() / 3) {
        uv_acc = add_float_accessor(mesh.uvs, 2, "VEC2", false);
    }
    if (!terrain_uv01.empty()) {
        uv_weight_acc = add_float_accessor(terrain_uv01, 2, "VEC2", false);
    }

    const int idx_view = add_view(mesh.indices.data(),
                                  mesh.indices.size() * sizeof(uint32_t),
                                  34963);
    if (acc_count > 0) accessors << ",";
    accessors << "{\"bufferView\":" << idx_view
              << ",\"componentType\":5125,\"count\":" << mesh.indices.size()
              << ",\"type\":\"SCALAR\"}";
    const int idx_acc = acc_count++;

    std::ostringstream json;
    json << std::setprecision(9);
    json << "{\"asset\":{\"version\":\"2.0\",\"generator\":\"Fable2AssetBrowser\"},";
    json << "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],";
    json << "\"nodes\":[{\"name\":\"terrain\",\"mesh\":0}],";
    json << "\"materials\":[{\"name\":\"Fable terrain shader data in .fable\",";
    json << "\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,1,1,1],";
    json << "\"roughnessFactor\":1,\"metallicFactor\":0}}],";
    json << "\"meshes\":[{\"name\":\"terrain\",\"primitives\":[{\"attributes\":{";
    json << "\"POSITION\":" << pos_acc;
    if (norm_acc >= 0) json << ",\"NORMAL\":" << norm_acc;
    if (uv_acc >= 0) json << ",\"TEXCOORD_0\":" << uv_acc;
    if (uv_weight_acc >= 0) json << ",\"TEXCOORD_1\":" << uv_weight_acc;
    json << "},\"indices\":" << idx_acc << ",\"material\":0}]}],";
    json << "\"buffers\":[{\"byteLength\":" << bin.size() << "}],";
    json << "\"bufferViews\":[" << views.str() << "],";
    json << "\"accessors\":[" << accessors.str() << "]}";

    std::string json_str = json.str();
    while (json_str.size() & 3u) json_str.push_back(' ');
    while (bin.size() & 3u) bin.push_back(0);

    const uint32_t json_len = static_cast<uint32_t>(json_str.size());
    const uint32_t bin_len = static_cast<uint32_t>(bin.size());
    const uint32_t total_len = 12 + 8 + json_len + 8 + bin_len;

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        err = ec.message();
        return false;
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        err = "cannot open " + path.string();
        return false;
    }
    put_u32(out, 0x46546C67u);
    put_u32(out, 2u);
    put_u32(out, total_len);
    put_u32(out, json_len);
    put_u32(out, 0x4E4F534Au);
    out.write(json_str.data(), static_cast<std::streamsize>(json_str.size()));
    put_u32(out, bin_len);
    put_u32(out, 0x004E4942u);
    out.write(reinterpret_cast<const char*>(bin.data()),
              static_cast<std::streamsize>(bin.size()));
    if (!out.good()) {
        err = "terrain GLB write failed";
        return false;
    }
    return true;
}
