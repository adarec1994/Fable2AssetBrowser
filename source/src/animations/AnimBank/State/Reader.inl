std::vector<ModelAnimationBinding> g_model_animation_bindings;
uint64_t g_model_animation_binding_revision = 1;

struct Reader {
    const uint8_t* base = nullptr;
    size_t         size = 0;
    size_t         pos  = 0;
    bool           ok   = true;

    bool need(size_t n) {
        if (!ok) return false;
        if (pos + n > size) { ok = false; return false; }
        return true;
    }
    uint32_t u32() {
        if (!need(4)) return 0;
        const uint8_t* p = base + pos;
        pos += 4;
        return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
               ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
    }
    float f32() {
        uint32_t u = u32();
        float f;
        std::memcpy(&f, &u, 4);
        return f;
    }

    std::string cstr() {
        if (!ok) return {};
        size_t start = pos;
        while (pos < size && base[pos] != 0) ++pos;
        if (pos >= size) { ok = false; return {}; }
        std::string s((const char*)(base + start), pos - start);
        ++pos;
        return s;
    }

    void skip(size_t n) { need(n); if (ok) pos += n; }
};

const std::string& lookup(uint32_t idx,
                          const std::vector<std::string>& strings) {
    static const std::string empty;
    if (idx == 0xFFFFFFFFu || idx >= strings.size()) return empty;
    return strings[idx];
}
