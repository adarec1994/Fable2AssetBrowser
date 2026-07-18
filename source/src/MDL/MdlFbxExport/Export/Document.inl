    Node root("");

    {
        Node hdr("FBXHeaderExtension");
        hdr.add_child(Node("FBXHeaderVersion")).add_prop(Prop::I(1003));
        hdr.add_child(Node("FBXVersion")).add_prop(Prop::I(7400));
        hdr.add_child(Node("EncryptionType")).add_prop(Prop::I(0));
        Node ts("CreationTimeStamp");
        std::time_t tt = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &tt);
#else
        localtime_r(&tt, &tm);
#endif
        ts.add_child(Node("Version")).add_prop(Prop::I(1000));
        ts.add_child(Node("Year")).add_prop(Prop::I(tm.tm_year + 1900));
        ts.add_child(Node("Month")).add_prop(Prop::I(tm.tm_mon + 1));
        ts.add_child(Node("Day")).add_prop(Prop::I(tm.tm_mday));
        ts.add_child(Node("Hour")).add_prop(Prop::I(tm.tm_hour));
        ts.add_child(Node("Minute")).add_prop(Prop::I(tm.tm_min));
        ts.add_child(Node("Second")).add_prop(Prop::I(tm.tm_sec));
        ts.add_child(Node("Millisecond")).add_prop(Prop::I(0));
        hdr.add_child(std::move(ts));
        hdr.add_child(Node("Creator")).add_prop(Prop::S("Fable 2 Asset Browser"));
        root.add_child(std::move(hdr));
    }
    root.add_child(Node("CreationTime")).add_prop(Prop::S("1970-01-01 00:00:00:000"));
    root.add_child(Node("Creator")).add_prop(Prop::S("Fable 2 Asset Browser"));

    {
        Node gs("GlobalSettings");
        gs.add_child(Node("Version")).add_prop(Prop::I(1000));
        Node props70("Properties70");
        props70.add_child(make_p("UpAxis",         "int", "Integer", "", { Prop::I(2)  }));
        props70.add_child(make_p("UpAxisSign",     "int", "Integer", "", { Prop::I(1)  }));
        props70.add_child(make_p("FrontAxis",      "int", "Integer", "", { Prop::I(1)  }));
        props70.add_child(make_p("FrontAxisSign",  "int", "Integer", "", { Prop::I(-1) }));
        props70.add_child(make_p("CoordAxis",      "int", "Integer", "", { Prop::I(0)  }));
        props70.add_child(make_p("CoordAxisSign",  "int", "Integer", "", { Prop::I(1)  }));
        props70.add_child(make_p("OriginalUpAxis",     "int", "Integer", "", { Prop::I(2) }));
        props70.add_child(make_p("OriginalUpAxisSign", "int", "Integer", "", { Prop::I(1) }));
        props70.add_child(make_p("UnitScaleFactor",         "double", "Number", "", { Prop::D(100.0) }));
        props70.add_child(make_p("OriginalUnitScaleFactor", "double", "Number", "", { Prop::D(100.0) }));
        gs.add_child(std::move(props70));
        root.add_child(std::move(gs));
    }

    {
        Node docs("Documents");
        docs.add_child(Node("Count")).add_prop(Prop::I(1));
        Node doc("Document");
        int64_t doc_id = ids.make();
        doc.add_prop(Prop::L(doc_id));
        doc.add_prop(Prop::S(""));
        doc.add_prop(Prop::S("Scene"));
        doc.add_child(Node("RootNode")).add_prop(Prop::L(0));
        docs.add_child(std::move(doc));
        root.add_child(std::move(docs));
    }
    root.add_child(Node("References"));

    {
        Node defs("Definitions");
        defs.add_child(Node("Version")).add_prop(Prop::I(100));

        int total = 1 + (int)meshes_out.size() * 3;
        for (auto& m : meshes_out) {
            for (int ch = 0; ch < CH_COUNT; ++ch) {
                if (m.tex[ch].video_id) total += 2;
            }
            total += (int)m.cluster_for_bone.size() + 1;
        }
        total += (int)bones.size() * 2;
        int anim_curve_nodes = 0;
        int anim_curves = 0;
        for (const FbxAnimClip& ac : fbx_anims) {
            anim_curve_nodes += (int)ac.nodes.size();
            for (const FbxAnimCurveNode& cn : ac.nodes) {
                anim_curves += (int)cn.curves.size();
            }
        }
        total += (int)fbx_anims.size() * 2 + anim_curve_nodes + anim_curves;
        defs.add_child(Node("Count")).add_prop(Prop::I(total));
        auto add_object_type = [&](const char* t, int count) {
            Node ot("ObjectType");
            ot.add_prop(Prop::S(t));
            ot.add_child(Node("Count")).add_prop(Prop::I(count));
            defs.add_child(std::move(ot));
        };
        add_object_type("GlobalSettings", 1);
        add_object_type("Geometry", (int)meshes_out.size());
        add_object_type("Model", (int)meshes_out.size() + (int)bones.size());
        add_object_type("Material", (int)meshes_out.size());
        add_object_type("Texture", 1);
        add_object_type("Video", 1);
        add_object_type("Pose", bones.empty() ? 0 : 1);
        add_object_type("NodeAttribute", (int)bones.size());
        add_object_type("Deformer", 1);
        if (!fbx_anims.empty()) {
            add_object_type("AnimationStack", (int)fbx_anims.size());
            add_object_type("AnimationLayer", (int)fbx_anims.size());
            add_object_type("AnimationCurveNode", anim_curve_nodes);
            add_object_type("AnimationCurve", anim_curves);
        }
        root.add_child(std::move(defs));
    }
