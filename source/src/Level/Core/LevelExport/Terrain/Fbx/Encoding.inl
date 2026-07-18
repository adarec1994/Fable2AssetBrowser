template <class Fn>
void write_csv_numbers(std::ostream& os, size_t count, Fn fn)
{
    os << "a: ";
    for (size_t i = 0; i < count; ++i) {
        if (i) os << ",";
        os << fn(i);
    }
}

struct FbxBuf {
    std::vector<uint8_t> data;
    void u8(uint8_t v) { data.push_back(v); }
    void u32(uint32_t v)
    {
        u8(uint8_t(v & 0xffu));
        u8(uint8_t((v >> 8) & 0xffu));
        u8(uint8_t((v >> 16) & 0xffu));
        u8(uint8_t((v >> 24) & 0xffu));
    }
    void u64(uint64_t v)
    {
        u32(uint32_t(v & 0xffffffffu));
        u32(uint32_t((v >> 32) & 0xffffffffu));
    }
    void i32(int32_t v) { u32(uint32_t(v)); }
    void i64(int64_t v) { u64(uint64_t(v)); }
    void f64(double v)
    {
        uint64_t u = 0;
        std::memcpy(&u, &v, 8);
        u64(u);
    }
    void bytes(const void* ptr, size_t size)
    {
        const auto* p = static_cast<const uint8_t*>(ptr);
        data.insert(data.end(), p, p + size);
    }
};

struct FbxProp {
    enum Type { I32, I64, F64, STRING, ARR_I32, ARR_F64 } type;
    int32_t i32 = 0;
    int64_t i64 = 0;
    double f64 = 0.0;
    std::string str;
    std::vector<int32_t> ai;
    std::vector<double> ad;

    static FbxProp I(int32_t v)
    {
        FbxProp p;
        p.type = I32;
        p.i32 = v;
        return p;
    }
    static FbxProp L(int64_t v)
    {
        FbxProp p;
        p.type = I64;
        p.i64 = v;
        return p;
    }
    static FbxProp D(double v)
    {
        FbxProp p;
        p.type = F64;
        p.f64 = v;
        return p;
    }
    static FbxProp S(std::string v)
    {
        FbxProp p;
        p.type = STRING;
        p.str = std::move(v);
        return p;
    }
    static FbxProp AI(std::vector<int32_t> v)
    {
        FbxProp p;
        p.type = ARR_I32;
        p.ai = std::move(v);
        return p;
    }
    static FbxProp AD(std::vector<double> v)
    {
        FbxProp p;
        p.type = ARR_F64;
        p.ad = std::move(v);
        return p;
    }

    void emit(FbxBuf& out) const
    {
        switch (type) {
            case I32:
                out.u8('I');
                out.i32(i32);
                break;
            case I64:
                out.u8('L');
                out.i64(i64);
                break;
            case F64:
                out.u8('D');
                out.f64(f64);
                break;
            case STRING:
                out.u8('S');
                out.u32(uint32_t(str.size()));
                out.bytes(str.data(), str.size());
                break;
            case ARR_I32:
                out.u8('i');
                out.u32(uint32_t(ai.size()));
                out.u32(0);
                out.u32(uint32_t(ai.size() * sizeof(int32_t)));
                for (int32_t v : ai) out.i32(v);
                break;
            case ARR_F64:
                out.u8('d');
                out.u32(uint32_t(ad.size()));
                out.u32(0);
                out.u32(uint32_t(ad.size() * sizeof(double)));
                for (double v : ad) out.f64(v);
                break;
        }
    }
};

struct FbxNode {
    std::string name;
    std::vector<FbxProp> props;
    std::vector<FbxNode> children;

    explicit FbxNode(std::string n = {}) : name(std::move(n)) {}
    FbxNode& prop(FbxProp p)
    {
        props.push_back(std::move(p));
        return *this;
    }
    FbxNode& child(FbxNode n)
    {
        children.push_back(std::move(n));
        return children.back();
    }
};

void fbx_write_node(FbxBuf& out, const FbxNode& n)
{
    const size_t header = out.data.size();
    out.u32(0);
    out.u32(uint32_t(n.props.size()));
    const size_t plen_pos = out.data.size();
    out.u32(0);
    out.u8(uint8_t(n.name.size()));
    out.bytes(n.name.data(), n.name.size());
    const size_t props_start = out.data.size();
    for (const auto& p : n.props) p.emit(out);
    const uint32_t plen = uint32_t(out.data.size() - props_start);
    out.data[plen_pos + 0] = uint8_t(plen & 0xffu);
    out.data[plen_pos + 1] = uint8_t((plen >> 8) & 0xffu);
    out.data[plen_pos + 2] = uint8_t((plen >> 16) & 0xffu);
    out.data[plen_pos + 3] = uint8_t((plen >> 24) & 0xffu);
    for (const auto& c : n.children) fbx_write_node(out, c);
    if (!n.children.empty()) {
        for (int i = 0; i < 13; ++i) out.u8(0);
    }
    const uint32_t end = uint32_t(out.data.size());
    out.data[header + 0] = uint8_t(end & 0xffu);
    out.data[header + 1] = uint8_t((end >> 8) & 0xffu);
    out.data[header + 2] = uint8_t((end >> 16) & 0xffu);
    out.data[header + 3] = uint8_t((end >> 24) & 0xffu);
}

FbxNode fbx_p(const char* name,
              const char* type1,
              const char* type2,
              const char* flags,
              std::vector<FbxProp> values)
{
    FbxNode p("P");
    p.prop(FbxProp::S(name));
    p.prop(FbxProp::S(type1));
    p.prop(FbxProp::S(type2));
    p.prop(FbxProp::S(flags));
    for (auto& v : values) p.prop(std::move(v));
    return p;
}

std::string fbx_obj_name(const std::string& name, const char* klass)
{
    std::string out = name;
    out.push_back('\0');
    out.push_back('\1');
    out += klass;
    return out;
}
