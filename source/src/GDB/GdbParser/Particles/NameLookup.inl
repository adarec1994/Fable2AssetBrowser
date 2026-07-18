constexpr uint32_t kHashParticleEffect = 0x5B009F68;
constexpr uint32_t kHashParticleEmitter = 0x4EDC9083;
constexpr uint32_t kHashDummyObject = 0xF4DF0382;
constexpr uint32_t kHashOffsetFromDummy = 0xE867B8A0;
constexpr uint32_t kHashMaxVisibilityDistance = 0x59029324;
constexpr uint32_t kHashOverrideMaxVisibilityDistance = 0xC835A9F4;
constexpr uint32_t kHashDisableWhenParentIsInvisible = 0xD27705D0;
constexpr uint32_t kHashOrientParticleToAttachmentPoint = 0xF825B96A;

inline uint32_t Fnv1LowerStr(const uint8_t* p, size_t n) {
    uint32_t h = 0x811C9DC5u;
    for (size_t i = 0; i < n; ++i) {
        h = uint32_t(h * 0x01000193u);
        h ^= uint8_t(std::tolower(p[i]));
    }
    return h;
}

void CollectFxNameStrings(const std::vector<uint8_t>& bytes,
                          std::unordered_map<uint32_t, std::string>& out) {
    const uint8_t* p = bytes.data();
    const size_t n = bytes.size();
    size_t i = 4;
    while (i + 1 < n) {

        if (p[i] < 0x20 || p[i] >= 0x7f) { ++i; continue; }
        size_t j = i;
        while (j < n && p[j] >= 0x20 && p[j] < 0x7f) ++j;
        const size_t len = j - i;
        if (len >= 4 && j < n && p[j] == 0 && i >= 4) {
            const uint32_t stored = ReadBeU32(p + i - 4);
            uint32_t exact = 0x811C9DC5u;
            for (size_t k = i; k < j; ++k) { exact = uint32_t(exact * 0x01000193u); exact ^= p[k]; }
            if (exact == stored) {
                const uint32_t lower = Fnv1LowerStr(p + i, len);
                out.emplace(lower, std::string(reinterpret_cast<const char*>(p + i), len));
            }
        }
        i = j + 1;
    }
}

void CollectGdbNameStrings(
    const std::vector<uint8_t>& bytes,
    std::unordered_map<uint32_t, std::string>& exact,
    std::unordered_map<uint32_t, std::string>& lower) {
    const uint8_t* p = bytes.data();
    const size_t n = bytes.size();
    size_t i = 4;
    while (i + 1 < n) {
        if (p[i] < 0x20 || p[i] >= 0x7f) { ++i; continue; }
        size_t j = i;
        while (j < n && p[j] >= 0x20 && p[j] < 0x7f) ++j;
        const size_t len = j - i;
        if (len >= 1 && j < n && p[j] == 0 && i >= 4) {
            const uint32_t stored = ReadBeU32(p + i - 4);
            uint32_t h = 0x811C9DC5u;
            for (size_t k = i; k < j; ++k) {
                h = uint32_t(h * 0x01000193u);
                h ^= p[k];
            }
            if (h == stored) {
                std::string s(reinterpret_cast<const char*>(p + i), len);
                exact.emplace(stored, s);
                lower.emplace(Fnv1LowerStr(p + i, len), std::move(s));
            }
        }
        i = j + 1;
    }
}
