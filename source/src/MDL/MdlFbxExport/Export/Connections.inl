    root.add_child(std::move(objects));

    Node conns("Connections");

    auto add_oo = [&](int64_t src, int64_t dst) {
        Node c("C");
        c.add_prop(Prop::S("OO"));
        c.add_prop(Prop::L(src));
        c.add_prop(Prop::L(dst));
        conns.add_child(std::move(c));
    };
    auto add_op = [&](int64_t src, int64_t dst, const std::string& prop) {
        Node c("C");
        c.add_prop(Prop::S("OP"));
        c.add_prop(Prop::L(src));
        c.add_prop(Prop::L(dst));
        c.add_prop(Prop::S(prop));
        conns.add_child(std::move(c));
    };

    for (const auto& b : bones) {
        if (b.parent_filt < 0) add_oo(b.model_id, 0);
        else                   add_oo(b.model_id, bones[b.parent_filt].model_id);
        add_oo(b.attr_id, b.model_id);
    }

    for (const auto& mo : meshes_out) {
        add_oo(mo.geom_id, mo.model_id);
        add_oo(mo.model_id, 0);
        add_oo(mo.mat_id, mo.model_id);

        for (int ch = 0; ch < CH_COUNT; ++ch) {
            const EmbeddedTex& et = mo.tex[ch];
            if (et.video_id == 0) continue;
            add_oo(et.video_id, et.tex_id);
            add_op(et.tex_id,   mo.mat_id, et.fbx_property);
        }
        if (!mo.cluster_for_bone.empty()) {
            add_oo(mo.skin_id, mo.geom_id);
            for (auto& kv : mo.cluster_for_bone) {
                int filt = kv.first;
                int64_t cid = kv.second;
                add_oo(cid, mo.skin_id);
                add_oo(bones[filt].model_id, cid);
            }
        }
    }

    for (const FbxAnimClip& ac : fbx_anims) {
        add_oo(ac.layer_id, ac.stack_id);
        for (const FbxAnimCurveNode& cn : ac.nodes) {
            if (cn.bone_filt < 0 || cn.bone_filt >= (int)bones.size()) {
                continue;
            }
            add_oo(cn.id, ac.layer_id);
            add_op(cn.id, bones[(size_t)cn.bone_filt].model_id, cn.property);
            for (const FbxAnimCurve& curve : cn.curves) {
                add_op(curve.id, cn.id, curve.axis);
            }
        }
    }

    root.add_child(std::move(conns));
    root.add_child(Node("Takes"));
