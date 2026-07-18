bool write_terrain_fbx(const TerrainMesh& mesh,
                       const std::filesystem::path& path,
                       std::string& err)
{
    if (!mesh.ok || mesh.positions.empty() || mesh.indices.empty()) {
        err = "terrain mesh is empty";
        return false;
    }
    const int64_t geom_id = 100001;
    const int64_t model_id = 100002;
    const int64_t mat_id = 100003;
    const size_t vcount = mesh.positions.size() / 3;
    const size_t icount = mesh.indices.size();
    std::vector<float> terrain_uv01;
    if (mesh.width > 1 && mesh.height > 1 &&
        vcount == size_t(mesh.width) * size_t(mesh.height)) {
        terrain_uv01.resize(vcount * 2);
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

    FbxNode root;
    {
        FbxNode hdr("FBXHeaderExtension");
        hdr.child(FbxNode("FBXHeaderVersion")).prop(FbxProp::I(1003));
        hdr.child(FbxNode("FBXVersion")).prop(FbxProp::I(7400));
        hdr.child(FbxNode("EncryptionType")).prop(FbxProp::I(0));
        hdr.child(FbxNode("Creator")).prop(FbxProp::S("Fable2AssetBrowser"));
        root.child(std::move(hdr));
    }
    root.child(FbxNode("CreationTime")).prop(FbxProp::S("1970-01-01 00:00:00:000"));
    root.child(FbxNode("Creator")).prop(FbxProp::S("Fable2AssetBrowser"));
    {
        FbxNode gs("GlobalSettings");
        gs.child(FbxNode("Version")).prop(FbxProp::I(1000));
        FbxNode p70("Properties70");
        p70.child(fbx_p("UpAxis", "int", "Integer", "", { FbxProp::I(2) }));
        p70.child(fbx_p("UpAxisSign", "int", "Integer", "", { FbxProp::I(1) }));
        p70.child(fbx_p("FrontAxis", "int", "Integer", "", { FbxProp::I(1) }));
        p70.child(fbx_p("FrontAxisSign", "int", "Integer", "", { FbxProp::I(-1) }));
        p70.child(fbx_p("CoordAxis", "int", "Integer", "", { FbxProp::I(0) }));
        p70.child(fbx_p("CoordAxisSign", "int", "Integer", "", { FbxProp::I(1) }));
        p70.child(fbx_p("UnitScaleFactor", "double", "Number", "", { FbxProp::D(100.0) }));
        p70.child(fbx_p("OriginalUnitScaleFactor", "double", "Number", "", { FbxProp::D(100.0) }));
        gs.child(std::move(p70));
        root.child(std::move(gs));
    }
    {
        FbxNode defs("Definitions");
        defs.child(FbxNode("Version")).prop(FbxProp::I(100));
        defs.child(FbxNode("Count")).prop(FbxProp::I(4));
        auto ot = [&](const char* name) {
            FbxNode n("ObjectType");
            n.prop(FbxProp::S(name));
            n.child(FbxNode("Count")).prop(FbxProp::I(1));
            defs.child(std::move(n));
        };
        ot("GlobalSettings");
        ot("Geometry");
        ot("Model");
        ot("Material");
        root.child(std::move(defs));
    }

    FbxNode objects("Objects");
    FbxNode geom("Geometry");
    geom.prop(FbxProp::L(geom_id));
    geom.prop(FbxProp::S(fbx_obj_name("Geometry::terrain", "Geometry")));
    geom.prop(FbxProp::S("Mesh"));
    geom.child(FbxNode("Vertices")).prop(FbxProp::AD(
        std::vector<double>(mesh.positions.begin(), mesh.positions.end())));

    std::vector<int32_t> pvi;
    pvi.reserve(icount);
    for (size_t i = 0; i + 2 < icount; i += 3) {
        pvi.push_back(int32_t(mesh.indices[i + 0]));
        pvi.push_back(int32_t(mesh.indices[i + 1]));
        pvi.push_back(~int32_t(mesh.indices[i + 2]));
    }
    geom.child(FbxNode("PolygonVertexIndex")).prop(FbxProp::AI(std::move(pvi)));
    geom.child(FbxNode("GeometryVersion")).prop(FbxProp::I(124));

    if (mesh.normals.size() / 3 == vcount) {
        FbxNode n("LayerElementNormal");
        n.prop(FbxProp::I(0));
        n.child(FbxNode("Version")).prop(FbxProp::I(101));
        n.child(FbxNode("Name")).prop(FbxProp::S(""));
        n.child(FbxNode("MappingInformationType")).prop(FbxProp::S("ByVertice"));
        n.child(FbxNode("ReferenceInformationType")).prop(FbxProp::S("Direct"));
        n.child(FbxNode("Normals")).prop(FbxProp::AD(
            std::vector<double>(mesh.normals.begin(), mesh.normals.end())));
        geom.child(std::move(n));
    }
    if (mesh.uvs.size() / 2 == vcount) {
        FbxNode uv("LayerElementUV");
        uv.prop(FbxProp::I(0));
        uv.child(FbxNode("Version")).prop(FbxProp::I(101));
        uv.child(FbxNode("Name")).prop(FbxProp::S("UVMap"));
        uv.child(FbxNode("MappingInformationType")).prop(FbxProp::S("ByVertice"));
        uv.child(FbxNode("ReferenceInformationType")).prop(FbxProp::S("Direct"));
        std::vector<double> vals(mesh.uvs.begin(), mesh.uvs.end());
        for (size_t i = 1; i < vals.size(); i += 2) {
            vals[i] = 1.0 - vals[i];
        }
        uv.child(FbxNode("UV")).prop(FbxProp::AD(std::move(vals)));
        geom.child(std::move(uv));
    }
    if (!terrain_uv01.empty()) {
        FbxNode uv("LayerElementUV");
        uv.prop(FbxProp::I(1));
        uv.child(FbxNode("Version")).prop(FbxProp::I(101));
        uv.child(FbxNode("Name")).prop(FbxProp::S("TerrainWeights"));
        uv.child(FbxNode("MappingInformationType")).prop(FbxProp::S("ByVertice"));
        uv.child(FbxNode("ReferenceInformationType")).prop(FbxProp::S("Direct"));
        std::vector<double> vals(terrain_uv01.begin(), terrain_uv01.end());
        for (size_t i = 1; i < vals.size(); i += 2) {
            vals[i] = 1.0 - vals[i];
        }
        uv.child(FbxNode("UV")).prop(FbxProp::AD(std::move(vals)));
        geom.child(std::move(uv));
    }
    {
        FbxNode lm("LayerElementMaterial");
        lm.prop(FbxProp::I(0));
        lm.child(FbxNode("Version")).prop(FbxProp::I(101));
        lm.child(FbxNode("Name")).prop(FbxProp::S(""));
        lm.child(FbxNode("MappingInformationType")).prop(FbxProp::S("AllSame"));
        lm.child(FbxNode("ReferenceInformationType")).prop(FbxProp::S("IndexToDirect"));
        lm.child(FbxNode("Materials")).prop(FbxProp::AI({ 0 }));
        geom.child(std::move(lm));
    }
    FbxNode layer("Layer");
    layer.prop(FbxProp::I(0));
    layer.child(FbxNode("Version")).prop(FbxProp::I(100));
    auto le = [&](const char* type, int idx) {
        FbxNode e("LayerElement");
        e.child(FbxNode("Type")).prop(FbxProp::S(type));
        e.child(FbxNode("TypedIndex")).prop(FbxProp::I(idx));
        layer.child(std::move(e));
    };
    if (mesh.normals.size() / 3 == vcount) {
        le("LayerElementNormal", 0);
    }
    if (mesh.uvs.size() / 2 == vcount) {
        le("LayerElementUV", 0);
    }
    if (!terrain_uv01.empty()) {
        le("LayerElementUV", 1);
    }
    le("LayerElementMaterial", 0);
    geom.child(std::move(layer));
    objects.child(std::move(geom));

    FbxNode model("Model");
    model.prop(FbxProp::L(model_id));
    model.prop(FbxProp::S(fbx_obj_name("Model::terrain", "Model")));
    model.prop(FbxProp::S("Mesh"));
    model.child(FbxNode("Version")).prop(FbxProp::I(232));
    FbxNode mp("Properties70");
    mp.child(fbx_p("Lcl Translation", "Lcl Translation", "", "A",
                   { FbxProp::D(0.0), FbxProp::D(0.0), FbxProp::D(0.0) }));
    mp.child(fbx_p("Lcl Rotation", "Lcl Rotation", "", "A",
                   { FbxProp::D(0.0), FbxProp::D(0.0), FbxProp::D(0.0) }));
    mp.child(fbx_p("Lcl Scaling", "Lcl Scaling", "", "A",
                   { FbxProp::D(1.0), FbxProp::D(1.0), FbxProp::D(1.0) }));
    model.child(std::move(mp));
    objects.child(std::move(model));

    FbxNode mat("Material");
    mat.prop(FbxProp::L(mat_id));
    mat.prop(FbxProp::S(fbx_obj_name(
        "Material::Fable terrain shader data in .fable", "Material")));
    mat.prop(FbxProp::S(""));
    mat.child(FbxNode("Version")).prop(FbxProp::I(102));
    mat.child(FbxNode("ShadingModel")).prop(FbxProp::S("phong"));
    mat.child(FbxNode("MultiLayer")).prop(FbxProp::I(0));
    objects.child(std::move(mat));
    root.child(std::move(objects));

    FbxNode conns("Connections");
    auto conn = [&](std::vector<FbxProp> props) {
        FbxNode c("C");
        for (auto& p : props) c.prop(std::move(p));
        conns.child(std::move(c));
    };
    conn({ FbxProp::S("OO"), FbxProp::L(geom_id), FbxProp::L(model_id) });
    conn({ FbxProp::S("OO"), FbxProp::L(model_id), FbxProp::L(0) });
    conn({ FbxProp::S("OO"), FbxProp::L(mat_id), FbxProp::L(model_id) });
    root.child(std::move(conns));
    root.child(FbxNode("Takes"));

    FbxBuf out;
    static const uint8_t magic[] = {
        'K','a','y','d','a','r','a',' ','F','B','X',' ','B','i','n','a',
        'r','y',' ',' ', 0x00, 0x1a, 0x00
    };
    out.bytes(magic, sizeof(magic));
    out.u32(7400);
    for (const auto& child : root.children) fbx_write_node(out, child);
    for (int i = 0; i < 13; ++i) out.u8(0);
    for (int i = 0; i < 16; ++i) out.u8(0);
    while (out.data.size() & 0xfu) out.u8(0);
    out.u32(0);
    out.u32(7400);
    for (int i = 0; i < 120; ++i) out.u8(0);
    static const uint8_t footer[] = {
        0xf8, 0x5a, 0x8c, 0x6a, 0xde, 0xf5, 0xd9, 0x7e,
        0xec, 0xe9, 0x0c, 0xe3, 0x75, 0x8f, 0x29, 0x0b
    };
    out.bytes(footer, sizeof(footer));

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        err = ec.message();
        return false;
    }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        err = "cannot open " + path.string();
        return false;
    }
    f.write(reinterpret_cast<const char*>(out.data.data()),
            static_cast<std::streamsize>(out.data.size()));
    if (!f.good()) {
        err = "terrain FBX write failed";
        return false;
    }
    return true;
}
