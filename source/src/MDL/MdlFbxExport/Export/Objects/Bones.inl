    Node objects("Objects");

    for (const auto& b : bones) {

        Node m("Model");
        m.add_prop(Prop::L(b.model_id));
        m.add_prop(Prop::S(fbx_object_name("Model::" + b.name, "Model")));
        m.add_prop(Prop::S("LimbNode"));
        m.add_child(Node("Version")).add_prop(Prop::I(232));
        Node p70("Properties70");
        p70.add_child(make_p("Lcl Translation", "Lcl Translation", "", "A",
                             { Prop::D(b.tx), Prop::D(b.ty), Prop::D(b.tz) }));

        p70.add_child(make_p("Lcl Scaling", "Lcl Scaling", "", "A",
                             { Prop::D(b.sx), Prop::D(b.sy), Prop::D(b.sz) }));

        {
            double sinr_cosp = 2.0 * (b.qw * b.qx + b.qy * b.qz);
            double cosr_cosp = 1.0 - 2.0 * (b.qx * b.qx + b.qy * b.qy);
            double rx = std::atan2(sinr_cosp, cosr_cosp);
            double sinp = 2.0 * (b.qw * b.qy - b.qz * b.qx);
            double ry = std::abs(sinp) >= 1
                ? std::copysign(3.14159265358979323846 / 2.0, sinp)
                : std::asin(sinp);
            double siny_cosp = 2.0 * (b.qw * b.qz + b.qx * b.qy);
            double cosy_cosp = 1.0 - 2.0 * (b.qy * b.qy + b.qz * b.qz);
            double rz = std::atan2(siny_cosp, cosy_cosp);
            const double k = 180.0 / 3.14159265358979323846;
            p70.add_child(make_p("Lcl Rotation", "Lcl Rotation", "", "A",
                                 { Prop::D(rx * k), Prop::D(ry * k), Prop::D(rz * k) }));
        }
        m.add_child(std::move(p70));
        objects.add_child(std::move(m));

        Node na("NodeAttribute");
        na.add_prop(Prop::L(b.attr_id));
        na.add_prop(Prop::S(fbx_object_name("NodeAttribute::",
                                            "NodeAttribute")));
        na.add_prop(Prop::S("LimbNode"));
        Node nap("Properties70");
        nap.add_child(make_p("Size", "double", "Number", "", { Prop::D(1.0) }));
        na.add_child(std::move(nap));
        na.add_child(Node("TypeFlags")).add_prop(Prop::S("Skeleton"));
        objects.add_child(std::move(na));
    }
