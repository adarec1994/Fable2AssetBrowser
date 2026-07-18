    if (!bones.empty()) {
        Node pose("Pose");
        pose.add_prop(Prop::L(ids.make()));
        pose.add_prop(Prop::S(fbx_object_name("Pose::Bind", "Pose")));
        pose.add_prop(Prop::S("BindPose"));
        pose.add_child(Node("Type")).add_prop(Prop::S("BindPose"));
        pose.add_child(Node("Version")).add_prop(Prop::I(100));
        pose.add_child(Node("NbPoseNodes")).add_prop(Prop::I((int32_t)bones.size()));
        for (const auto& b : bones) {
            Node pn("PoseNode");
            pn.add_child(Node("Node")).add_prop(Prop::L(b.model_id));
            pn.add_child(Node("Matrix")).add_prop(Prop::AD(b.world.as_column_major()));
            pose.add_child(std::move(pn));
        }
        objects.add_child(std::move(pose));
    }

    for (const FbxAnimClip& ac : fbx_anims) {
        Node stack("AnimationStack");
        stack.add_prop(Prop::L(ac.stack_id));
        stack.add_prop(Prop::S(fbx_object_name("AnimStack::" + ac.name,
                                               "AnimStack")));
        stack.add_prop(Prop::S(""));
        Node sp("Properties70");
        sp.add_child(make_p("LocalStart", "KTime", "Time", "",
                            { Prop::L(ac.start) }));
        sp.add_child(make_p("LocalStop", "KTime", "Time", "",
                            { Prop::L(ac.stop) }));
        stack.add_child(std::move(sp));
        objects.add_child(std::move(stack));

        Node layer("AnimationLayer");
        layer.add_prop(Prop::L(ac.layer_id));
        layer.add_prop(Prop::S(fbx_object_name("AnimLayer::BaseLayer",
                                               "AnimLayer")));
        layer.add_prop(Prop::S(""));
        objects.add_child(std::move(layer));

        for (const FbxAnimCurveNode& cn : ac.nodes) {
            Node n("AnimationCurveNode");
            n.add_prop(Prop::L(cn.id));
            n.add_prop(Prop::S(fbx_object_name(
                "AnimCurveNode::" + ac.name + "_" + std::to_string(cn.id),
                "AnimationCurveNode")));
            n.add_prop(Prop::S(""));
            Node p70("Properties70");
            p70.add_child(make_p("d|X", "Number", "", "A",
                                 { Prop::D(0.0) }));
            p70.add_child(make_p("d|Y", "Number", "", "A",
                                 { Prop::D(0.0) }));
            p70.add_child(make_p("d|Z", "Number", "", "A",
                                 { Prop::D(0.0) }));
            n.add_child(std::move(p70));
            objects.add_child(std::move(n));

            for (const FbxAnimCurve& curve : cn.curves) {
                Node c("AnimationCurve");
                c.add_prop(Prop::L(curve.id));
                c.add_prop(Prop::S(fbx_object_name(
                    "AnimCurve::" + ac.name + "_" + std::to_string(curve.id),
                    "AnimationCurve")));
                c.add_prop(Prop::S(""));
                c.add_child(Node("Default")).add_prop(Prop::F(0.0f));
                c.add_child(Node("KeyVer")).add_prop(Prop::I(4008));
                c.add_child(Node("KeyTime")).add_prop(Prop::AL(curve.times));
                c.add_child(Node("KeyValueFloat")).add_prop(Prop::AF(curve.values));
                c.add_child(Node("KeyAttrFlags")).add_prop(Prop::AI({ 24836 }));
                c.add_child(Node("KeyAttrDataFloat")).add_prop(
                    Prop::AF({ 0.0f, 0.0f, 0.0f, 0.0f }));
                c.add_child(Node("KeyAttrRefCount")).add_prop(
                    Prop::AI({ (int32_t)curve.times.size() }));
                objects.add_child(std::move(c));
            }
        }
    }
