    for (size_t gi = 0; gi < geoms.size(); ++gi) {
        const auto& g = geoms[gi];
        const auto& mo = meshes_out[gi];

        Node gn("Geometry");
        gn.add_prop(Prop::L(mo.geom_id));
        gn.add_prop(Prop::S(fbx_object_name("Geometry::" + mo.name,
                                            "Geometry")));
        gn.add_prop(Prop::S("Mesh"));

        std::vector<double> verts(g.positions.begin(), g.positions.end());
        for (size_t i = 0; i + 2 < verts.size(); i += 3) {
            double y = verts[i + 1], z = verts[i + 2];
            verts[i + 1] = -z;
            verts[i + 2] =  y;
        }
        gn.add_child(Node("Vertices")).add_prop(Prop::AD(std::move(verts)));

        std::vector<int32_t> pvi;
        pvi.reserve(g.indices.size());
        for (size_t i = 0; i + 2 < g.indices.size(); i += 3) {
            pvi.push_back((int32_t)g.indices[i + 0]);
            pvi.push_back((int32_t)g.indices[i + 1]);
            pvi.push_back((int32_t)~g.indices[i + 2]);
        }
        gn.add_child(Node("PolygonVertexIndex")).add_prop(Prop::AI(std::move(pvi)));

        gn.add_child(Node("GeometryVersion")).add_prop(Prop::I(124));

        if (g.normals.size() == g.positions.size()) {
            Node n("LayerElementNormal");
            n.add_prop(Prop::I(0));
            n.add_child(Node("Version")).add_prop(Prop::I(101));
            n.add_child(Node("Name")).add_prop(Prop::S(""));
            n.add_child(Node("MappingInformationType")).add_prop(Prop::S("ByVertice"));
            n.add_child(Node("ReferenceInformationType")).add_prop(Prop::S("Direct"));
            std::vector<double> nv(g.normals.begin(), g.normals.end());

            for (size_t i = 0; i + 2 < nv.size(); i += 3) {
                double y = nv[i + 1], z = nv[i + 2];
                nv[i + 1] = -z;
                nv[i + 2] =  y;
            }
            n.add_child(Node("Normals")).add_prop(Prop::AD(std::move(nv)));
            gn.add_child(std::move(n));
        }

        if (!g.uvs.empty()) {
            Node uv("LayerElementUV");
            uv.add_prop(Prop::I(0));
            uv.add_child(Node("Version")).add_prop(Prop::I(101));
            uv.add_child(Node("Name")).add_prop(Prop::S("UVMap"));
            uv.add_child(Node("MappingInformationType")).add_prop(Prop::S("ByVertice"));
            uv.add_child(Node("ReferenceInformationType")).add_prop(Prop::S("Direct"));
            std::vector<double> uvv(g.uvs.begin(), g.uvs.end());

            for (size_t i = 1; i < uvv.size(); i += 2) uvv[i] = 1.0 - uvv[i];
            uv.add_child(Node("UV")).add_prop(Prop::AD(std::move(uvv)));
            gn.add_child(std::move(uv));
        }

        {
            Node lm("LayerElementMaterial");
            lm.add_prop(Prop::I(0));
            lm.add_child(Node("Version")).add_prop(Prop::I(101));
            lm.add_child(Node("Name")).add_prop(Prop::S(""));
            lm.add_child(Node("MappingInformationType")).add_prop(Prop::S("AllSame"));
            lm.add_child(Node("ReferenceInformationType")).add_prop(Prop::S("IndexToDirect"));
            lm.add_child(Node("Materials")).add_prop(Prop::AI({ 0 }));
            gn.add_child(std::move(lm));
        }

        {
            Node l("Layer");
            l.add_prop(Prop::I(0));
            l.add_child(Node("Version")).add_prop(Prop::I(100));
            auto add_le = [&](const char* type) {
                Node le("LayerElement");
                le.add_child(Node("Type")).add_prop(Prop::S(type));
                le.add_child(Node("TypedIndex")).add_prop(Prop::I(0));
                l.add_child(std::move(le));
            };
            if (g.normals.size() == g.positions.size()) add_le("LayerElementNormal");
            if (!g.uvs.empty()) add_le("LayerElementUV");
            add_le("LayerElementMaterial");
            gn.add_child(std::move(l));
        }
        objects.add_child(std::move(gn));

        Node mn("Model");
        mn.add_prop(Prop::L(mo.model_id));
        mn.add_prop(Prop::S(fbx_object_name("Model::" + mo.name, "Model")));
        mn.add_prop(Prop::S("Mesh"));
        mn.add_child(Node("Version")).add_prop(Prop::I(232));
        Node mp("Properties70");
        mp.add_child(make_p("Lcl Translation", "Lcl Translation", "", "A",
                            { Prop::D(0.0), Prop::D(0.0), Prop::D(0.0) }));
        mn.add_child(std::move(mp));
        objects.add_child(std::move(mn));

        Node mat("Material");
        mat.add_prop(Prop::L(mo.mat_id));
        mat.add_prop(Prop::S(fbx_object_name("Material::" + mo.name,
                                             "Material")));
        mat.add_prop(Prop::S(""));
        mat.add_child(Node("Version")).add_prop(Prop::I(102));
        mat.add_child(Node("ShadingModel")).add_prop(Prop::S("phong"));
        mat.add_child(Node("MultiLayer")).add_prop(Prop::I(0));
        Node matp("Properties70");
        matp.add_child(make_p("DiffuseColor", "Color", "", "A",
                              { Prop::D(0.8), Prop::D(0.8), Prop::D(0.8) }));
        matp.add_child(make_p("SpecularColor", "Color", "", "A",
                              { Prop::D(0.2), Prop::D(0.2), Prop::D(0.2) }));
        matp.add_child(make_p("Shininess", "double", "Number", "", { Prop::D(20.0) }));
        mat.add_child(std::move(matp));
        objects.add_child(std::move(mat));

        for (int ch = 0; ch < CH_COUNT; ++ch) {
            const EmbeddedTex& et = mo.tex[ch];
            if (et.video_id == 0) continue;

            Node vn("Video");
            vn.add_prop(Prop::L(et.video_id));
            vn.add_prop(Prop::S(fbx_object_name("Video::" + et.name,
                                                "Video")));
            vn.add_prop(Prop::S("Clip"));
            vn.add_child(Node("Type")).add_prop(Prop::S("Clip"));
            Node vp("Properties70");
            vp.add_child(make_p("Path", "KString", "XRefUrl", "",
                                { Prop::S(et.filename) }));
            vn.add_child(std::move(vp));
            vn.add_child(Node("UseMipMap")).add_prop(Prop::I(0));
            vn.add_child(Node("Filename")).add_prop(Prop::S(et.filename));
            vn.add_child(Node("RelativeFilename")).add_prop(Prop::S(et.filename));

            vn.add_child(Node("Content")).add_prop(Prop::R(et.bytes));
            objects.add_child(std::move(vn));

            Node tn("Texture");
            tn.add_prop(Prop::L(et.tex_id));
            tn.add_prop(Prop::S(fbx_object_name("Texture::" + et.name,
                                                "Texture")));
            tn.add_prop(Prop::S(""));
            tn.add_child(Node("Type")).add_prop(Prop::S("TextureVideoClip"));
            tn.add_child(Node("Version")).add_prop(Prop::I(202));
            tn.add_child(Node("TextureName")).add_prop(Prop::S("Texture::" + et.name));
            Node tp("Properties70");
            tp.add_child(make_p("UVSet", "KString", "", "", { Prop::S("UVMap") }));
            tn.add_child(std::move(tp));
            tn.add_child(Node("FileName")).add_prop(Prop::S(et.filename));
            tn.add_child(Node("RelativeFilename")).add_prop(Prop::S(et.filename));
            objects.add_child(std::move(tn));
        }

        if (!mo.cluster_for_bone.empty()) {
            Node skin("Deformer");
            skin.add_prop(Prop::L(mo.skin_id));
            skin.add_prop(Prop::S(fbx_object_name(
                "Deformer::" + mo.name + "_skin", "Deformer")));
            skin.add_prop(Prop::S("Skin"));
            skin.add_child(Node("Version")).add_prop(Prop::I(101));
            skin.add_child(Node("Link_DeformAcuracy")).add_prop(Prop::D(50.0));
            objects.add_child(std::move(skin));

            for (auto& kv : mo.cluster_for_bone) {
                int filt = kv.first;
                int64_t cid = kv.second;
                const BoneOut& b = bones[filt];

                Node c("Deformer");
                c.add_prop(Prop::L(cid));
                c.add_prop(Prop::S(fbx_object_name(
                    "SubDeformer::Cluster_" + b.name, "Deformer")));
                c.add_prop(Prop::S("Cluster"));
                c.add_child(Node("Version")).add_prop(Prop::I(100));
                c.add_child(Node("UserData")).add_prop(Prop::S("")).add_prop(Prop::S(""));

                std::vector<int32_t> idxs(mo.cluster_indexes.at(filt).begin(),
                                          mo.cluster_indexes.at(filt).end());
                std::vector<double> wts = mo.cluster_weights.at(filt);
                c.add_child(Node("Indexes")).add_prop(Prop::AI(std::move(idxs)));
                c.add_child(Node("Weights")).add_prop(Prop::AD(std::move(wts)));

                Mat4 link = b.world;
                Mat4 inv  = link.inverse_or_identity();
                c.add_child(Node("Transform")).add_prop(Prop::AD(inv.as_column_major()));
                c.add_child(Node("TransformLink")).add_prop(Prop::AD(link.as_column_major()));

                objects.add_child(std::move(c));
            }
        }
    }
