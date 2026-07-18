struct Node {
    std::string name;
    std::vector<Prop> props;
    std::vector<Node> children;

    Node() = default;
    Node(std::string n) : name(std::move(n)) {}

    Node& add_prop(Prop p) { props.push_back(std::move(p)); return *this; }
    Node& add_child(Node c) {
        children.push_back(std::move(c));
        return children.back();
    }
};

void write_node(Buf& out, const Node& n) {
    size_t header_off = out.data.size();
    out.put_u32(0);
    out.put_u32((uint32_t)n.props.size());

    size_t prop_list_len_off = out.data.size();
    out.put_u32(0);
    out.put_u8((uint8_t)n.name.size());
    out.put_bytes(n.name.data(), n.name.size());

    size_t props_start = out.data.size();
    for (const auto& p : n.props) p.emit(out);
    size_t props_end = out.data.size();

    uint32_t plen = (uint32_t)(props_end - props_start);
    out.data[prop_list_len_off + 0] = (uint8_t)(plen & 0xFF);
    out.data[prop_list_len_off + 1] = (uint8_t)((plen >> 8) & 0xFF);
    out.data[prop_list_len_off + 2] = (uint8_t)((plen >> 16) & 0xFF);
    out.data[prop_list_len_off + 3] = (uint8_t)((plen >> 24) & 0xFF);

    for (const auto& c : n.children) write_node(out, c);
    if (!n.children.empty()) {

        for (int i = 0; i < 13; ++i) out.put_u8(0);
    }

    uint32_t end_off = (uint32_t)out.data.size();
    out.data[header_off + 0] = (uint8_t)(end_off & 0xFF);
    out.data[header_off + 1] = (uint8_t)((end_off >> 8) & 0xFF);
    out.data[header_off + 2] = (uint8_t)((end_off >> 16) & 0xFF);
    out.data[header_off + 3] = (uint8_t)((end_off >> 24) & 0xFF);
}

Node make_p(const char* name, const char* type1, const char* type2,
            const char* flags, std::vector<Prop> values) {
    Node p("P");
    p.add_prop(Prop::S(name));
    p.add_prop(Prop::S(type1));
    p.add_prop(Prop::S(type2));
    p.add_prop(Prop::S(flags));
    for (auto& v : values) p.add_prop(std::move(v));
    return p;
}

std::string fbx_object_name(const std::string& name,
                            const char* object_class)
{
    std::string out = name;
    out.push_back('\0');
    out.push_back('\1');
    out += object_class;
    return out;
}
