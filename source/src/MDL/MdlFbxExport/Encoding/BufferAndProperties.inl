struct Buf {
    std::vector<uint8_t> data;
    void put_u8 (uint8_t  v) { data.push_back(v); }
    void put_u16(uint16_t v) { put_u8(v & 0xFF); put_u8((v >> 8) & 0xFF); }
    void put_u32(uint32_t v) {
        put_u8(v & 0xFF); put_u8((v >> 8) & 0xFF);
        put_u8((v >> 16) & 0xFF); put_u8((v >> 24) & 0xFF);
    }
    void put_u64(uint64_t v) {
        put_u32((uint32_t)(v & 0xFFFFFFFFu));
        put_u32((uint32_t)((v >> 32) & 0xFFFFFFFFu));
    }
    void put_i32(int32_t v)  { put_u32((uint32_t)v); }
    void put_i64(int64_t v)  { put_u64((uint64_t)v); }
    void put_f32(float v)    { uint32_t u; std::memcpy(&u, &v, 4); put_u32(u); }
    void put_f64(double v)   { uint64_t u; std::memcpy(&u, &v, 8); put_u64(u); }
    void put_bytes(const void* p, size_t n) {
        const uint8_t* b = (const uint8_t*)p;
        data.insert(data.end(), b, b + n);
    }
};

struct Prop {
    enum Type { I32, I64, F32, F64, STRING, BYTES, ARR_I32, ARR_I64, ARR_F32, ARR_F64 } type;
    int32_t  i32  = 0;
    int64_t  i64  = 0;
    float    f32  = 0;
    double   f64  = 0;
    std::string str;
    bool     str_is_bytes = false;
    std::vector<int32_t> ai;
    std::vector<int64_t> al;
    std::vector<float>   af;
    std::vector<double>  ad;

    static Prop I (int32_t v)            { Prop p; p.type = I32; p.i32 = v; return p; }
    static Prop L (int64_t v)            { Prop p; p.type = I64; p.i64 = v; return p; }
    static Prop F (float v)              { Prop p; p.type = F32; p.f32 = v; return p; }
    static Prop D (double v)             { Prop p; p.type = F64; p.f64 = v; return p; }
    static Prop S (const std::string& s) { Prop p; p.type = STRING; p.str = s; return p; }
    static Prop R (const std::vector<uint8_t>& b) {
        Prop p; p.type = BYTES;
        p.str.assign((const char*)b.data(), b.size());
        p.str_is_bytes = true;
        return p;
    }
    static Prop AI(std::vector<int32_t> v) { Prop p; p.type = ARR_I32; p.ai = std::move(v); return p; }
    static Prop AL(std::vector<int64_t> v) { Prop p; p.type = ARR_I64; p.al = std::move(v); return p; }
    static Prop AF(std::vector<float>   v) { Prop p; p.type = ARR_F32; p.af = std::move(v); return p; }
    static Prop AD(std::vector<double>  v) { Prop p; p.type = ARR_F64; p.ad = std::move(v); return p; }

    void emit(Buf& out) const {
        switch (type) {
            case I32:    out.put_u8('I'); out.put_i32(i32); break;
            case I64:    out.put_u8('L'); out.put_i64(i64); break;
            case F32:    out.put_u8('F'); out.put_f32(f32); break;
            case F64:    out.put_u8('D'); out.put_f64(f64); break;
            case STRING: out.put_u8(str_is_bytes ? 'R' : 'S');
                         out.put_u32((uint32_t)str.size());
                         out.put_bytes(str.data(), str.size());
                         break;
            case BYTES:  out.put_u8('R');
                         out.put_u32((uint32_t)str.size());
                         out.put_bytes(str.data(), str.size());
                         break;

            case ARR_I32: {
                out.put_u8('i');
                out.put_u32((uint32_t)ai.size());
                out.put_u32(0);
                out.put_u32((uint32_t)(ai.size() * sizeof(int32_t)));
                for (auto v : ai) out.put_i32(v);
                break;
            }
            case ARR_I64: {
                out.put_u8('l');
                out.put_u32((uint32_t)al.size());
                out.put_u32(0);
                out.put_u32((uint32_t)(al.size() * sizeof(int64_t)));
                for (auto v : al) out.put_i64(v);
                break;
            }
            case ARR_F32: {
                out.put_u8('f');
                out.put_u32((uint32_t)af.size());
                out.put_u32(0);
                out.put_u32((uint32_t)(af.size() * sizeof(float)));
                for (auto v : af) out.put_f32(v);
                break;
            }
            case ARR_F64: {
                out.put_u8('d');
                out.put_u32((uint32_t)ad.size());
                out.put_u32(0);
                out.put_u32((uint32_t)(ad.size() * sizeof(double)));
                for (auto v : ad) out.put_f64(v);
                break;
            }
        }
    }

    size_t byte_size() const {
        switch (type) {
            case I32: return 1 + 4;
            case I64: return 1 + 8;
            case F32: return 1 + 4;
            case F64: return 1 + 8;
            case STRING:
            case BYTES: return 1 + 4 + str.size();
            case ARR_I32: return 1 + 12 + ai.size() * 4;
            case ARR_I64: return 1 + 12 + al.size() * 8;
            case ARR_F32: return 1 + 12 + af.size() * 4;
            case ARR_F64: return 1 + 12 + ad.size() * 8;
        }
        return 0;
    }
};
