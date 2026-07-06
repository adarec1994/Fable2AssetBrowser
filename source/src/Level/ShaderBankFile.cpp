#include "ShaderBankFile.h"

#include <zlib.h>
#include <cstring>

namespace ShaderBank {
namespace {

struct R {
    const uint8_t* p;
    size_t         n;
    size_t         i = 0;

    bool need(size_t k) const { return i + k <= n; }

    bool u32(uint32_t& v) {
        if (!need(4)) return false;
        v = (uint32_t(p[i]) << 24) | (uint32_t(p[i + 1]) << 16) |
            (uint32_t(p[i + 2]) << 8) | uint32_t(p[i + 3]);
        i += 4;
        return true;
    }
    bool u8(uint8_t& v) {
        if (!need(1)) return false;
        v = p[i++];
        return true;
    }
    bool strz(std::string& s) {
        size_t j = i;
        while (j < n && p[j] != 0) ++j;
        if (j >= n) return false;
        s.assign(reinterpret_cast<const char*>(p + i), j - i);
        i = j + 1;
        return true;
    }
};

bool inflate_zlib(const uint8_t* src, size_t src_len,
                  std::vector<uint8_t>& out, size_t expected) {
    out.assign(expected, 0);
    if (expected == 0) return true;
    z_stream z{};
    if (inflateInit(&z) != Z_OK) return false;
    z.next_in   = const_cast<Bytef*>(src);
    z.avail_in  = static_cast<uInt>(src_len);
    z.next_out  = out.data();
    z.avail_out = static_cast<uInt>(expected);
    int ret = inflate(&z, Z_FINISH);
    size_t produced = expected - z.avail_out;
    inflateEnd(&z);
    out.resize(produced);
    return (ret == Z_STREAM_END || ret == Z_OK) && produced == expected;
}

}

bool ParseShaderBank(const std::vector<uint8_t>& data, Bank& out) {
    out = Bank{};
    R r{ data.data(), data.size() };

    static const char kMagic[] = "ShaderBankFile";
    if (r.n < 19 || std::memcmp(r.p, kMagic, 14) != 0) {
        out.error = "bad magic";
        return false;
    }
    r.i = 14;
    if (!r.u32(out.version) || out.version != 3) { out.error = "bad version"; return false; }
    if (!r.u8(out.endian)) { out.error = "endian"; return false; }

    uint32_t pcnt = 0;
    if (!r.u32(pcnt) || pcnt > 100000) { out.error = "param count"; return false; }
    out.params.resize(pcnt);
    for (uint32_t k = 0; k < pcnt; ++k) {
        for (int j = 0; j < 9; ++j)
            if (!r.u32(out.params[k].hdr[j])) { out.error = "param hdr"; return false; }
        uint32_t sc = 0;
        if (!r.u32(sc) || sc > 100000) { out.error = "param subcount"; return false; }
        out.params[k].subs.resize(sc);
        for (uint32_t s = 0; s < sc; ++s) {
            if (!r.strz(out.params[k].subs[s].first) ||
                !r.u32(out.params[k].subs[s].second)) { out.error = "param sub"; return false; }
        }
    }

    uint32_t rcnt = 0;
    if (!r.u32(rcnt) || rcnt > 100000) { out.error = "resource count"; return false; }
    out.resources.resize(rcnt);
    for (uint32_t k = 0; k < rcnt; ++k)
        if (!r.strz(out.resources[k])) { out.error = "resource name"; return false; }

    uint32_t shaderCount = 0;
    if (!r.u32(shaderCount) || shaderCount > 1000000) { out.error = "shader count"; return false; }
    if (!r.u32(out.bank_hash)) { out.error = "bank hash"; return false; }
    out.shaders.resize(shaderCount);
    for (uint32_t k = 0; k < shaderCount; ++k) {
        if (!r.strz(out.shaders[k].name) ||
            !r.u8(out.shaders[k].type) ||
            !r.u32(out.shaders[k].parent)) { out.error = "shader entry"; return false; }
    }

    uint32_t nA = 0;
    if (!r.u32(nA) || nA > 100000) { out.error = "program count"; return false; }
    std::vector<uint32_t> dsizes(nA), offsets(nA);
    for (uint32_t k = 0; k < nA; ++k)
        if (!r.u32(dsizes[k])) { out.error = "dsize table"; return false; }
    for (uint32_t k = 0; k < nA; ++k)
        if (!r.u32(offsets[k])) { out.error = "offset table"; return false; }

    uint32_t blobLen = 0;
    if (!r.u32(blobLen) || !r.need(blobLen)) { out.error = "blob length"; return false; }
    const uint8_t* blob = r.p + r.i;

    out.programs.resize(nA);
    for (uint32_t k = 0; k < nA; ++k) {
        out.programs[k].offset            = offsets[k];
        out.programs[k].decompressed_size = dsizes[k];
        const uint32_t start = offsets[k];
        const uint32_t end   = (k + 1 < nA) ? offsets[k + 1] : blobLen;
        if (start + 4 > blobLen || end > blobLen || end < start + 4) continue;
        inflate_zlib(blob + start + 4, end - (start + 4),
                     out.programs[k].microcode, dsizes[k]);
    }

    out.ok = true;
    return true;
}

}
