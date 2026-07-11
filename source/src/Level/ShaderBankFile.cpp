#include "ShaderBankFile.h"

#include <zlib.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

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
    // Lionhead's chunks end in Z_SYNC_FLUSH (00 00 FF FF), without a final
    // BFINAL/trailer.  Asking for Z_FINISH turns that valid exact-size stream
    // into Z_BUF_ERROR; the XEX likewise inflates to the known output size.
    int ret = inflate(&z, Z_SYNC_FLUSH);
    size_t produced = expected - z.avail_out;
    inflateEnd(&z);
    out.resize(produced);
    return (ret == Z_STREAM_END || ret == Z_OK) && produced == expected;
}

bool be_u32(const std::vector<uint8_t>& bytes, size_t offset,
            uint32_t& value) {
    if (offset > bytes.size() || bytes.size() - offset < 4) return false;
    value = (uint32_t(bytes[offset]) << 24) |
            (uint32_t(bytes[offset + 1]) << 16) |
            (uint32_t(bytes[offset + 2]) << 8) |
            uint32_t(bytes[offset + 3]);
    return true;
}

bool advance(size_t& cursor, size_t amount, size_t limit) {
    if (cursor > limit || amount > limit - cursor) return false;
    cursor += amount;
    return true;
}

bool parse_program_record(Program& program) {
    size_t cursor = 0;

    // sub_82B8AA48 reads two arrays of 16-byte records.  Only the first is
    // retained by the runtime, but both are present in the serialized child.
    for (unsigned array_index = 0; array_index < 2; ++array_index) {
        uint32_t count = 0;
        if (!be_u32(program.record, cursor, count) ||
            !advance(cursor, 4, program.record.size())) {
            return false;
        }
        if (count > std::numeric_limits<size_t>::max() / 16 ||
            size_t(count) * 16 > program.record.size() - cursor) {
            return false;
        }
        auto& bindings = program.binding_lists[array_index];
        bindings.resize(count);
        for (uint32_t binding_index = 0; binding_index < count;
             ++binding_index) {
            BindingDescriptor& binding = bindings[binding_index];
            if (!be_u32(program.record, cursor, binding.logical_id) ||
                !be_u32(program.record, cursor + 4,
                        binding.hardware_binding) ||
                !be_u32(program.record, cursor + 8, binding.count) ||
                !be_u32(program.record, cursor + 12, binding.tag)) {
                return false;
            }
            cursor += 16;
        }
    }

    // Four raw 8-byte fields, followed by the exact byte count passed to the
    // Xbox vertex/pixel shader constructors.
    if (!advance(cursor, 32, program.record.size())) return false;
    uint32_t compiled_size = 0;
    if (!be_u32(program.record, cursor, compiled_size) ||
        !advance(cursor, 4, program.record.size()) ||
        compiled_size > program.record.size() - cursor) {
        return false;
    }
    program.compiled_shader.assign(program.record.begin() + cursor,
                                   program.record.begin() + cursor +
                                       compiled_size);

    uint32_t ucode_size = 0;
    if (!be_u32(program.compiled_shader, 0, program.compiled_flags) ||
        !be_u32(program.compiled_shader, 4,
                program.compiled_header_size) ||
        !be_u32(program.compiled_shader, 8, ucode_size) ||
        program.compiled_header_size > program.compiled_shader.size() ||
        ucode_size > program.compiled_shader.size() -
                         program.compiled_header_size) {
        return false;
    }
    const size_t ucode_begin = program.compiled_header_size;
    program.xenos_ucode.assign(program.compiled_shader.begin() + ucode_begin,
                               program.compiled_shader.begin() + ucode_begin +
                                   ucode_size);
    return true;
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
    // This byte is not an endianness marker.  The retail loader at
    // 0x82B8A758 uses it to select the shader identifier representation:
    // debug banks store a NUL-terminated name, release banks store a u32 hash.
    if (!r.u8(out.stores_shader_names)) { out.error = "shader identifier mode"; return false; }

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
        const bool identifier_ok = out.stores_shader_names
            ? r.strz(out.shaders[k].name)
            : r.u32(out.shaders[k].name_hash);
        if (!identifier_ok || !r.u8(out.shaders[k].type) ||
            !r.u32(out.shaders[k].program_ordinal)) { out.error = "shader entry"; return false; }
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

    out.program_groups.resize(nA);
    if (nA > std::numeric_limits<size_t>::max() / 4) {
        out.error = "program count overflow";
        return false;
    }
    out.programs.resize(size_t(nA) * 4);
    for (uint32_t k = 0; k < nA; ++k) {
        const uint32_t start = offsets[k];
        const uint32_t end   = (k + 1 < nA) ? offsets[k + 1] : blobLen;
        if (start > blobLen || blobLen - start < 4 || end > blobLen ||
            end < start + 4) {
            out.error = "program group bounds";
            return false;
        }

        const uint32_t compressed_size =
            (uint32_t(blob[start]) << 24) |
            (uint32_t(blob[start + 1]) << 16) |
            (uint32_t(blob[start + 2]) << 8) |
            uint32_t(blob[start + 3]);
        if (compressed_size > end - (start + 4)) {
            out.error = "program group compressed size";
            return false;
        }

        ProgramGroup& group = out.program_groups[k];
        group.blob_offset = start;
        group.compressed_size = compressed_size;
        group.decompressed_size = dsizes[k];
        if (!inflate_zlib(blob + start + 4, compressed_size,
                          group.decompressed, dsizes[k])) {
            out.error = "program group inflate";
            return false;
        }

        uint32_t payload_base = 0;
        std::array<uint32_t, 4> relative_offsets{};
        if (!be_u32(group.decompressed, 0, payload_base)) {
            out.error = "program group header";
            return false;
        }
        for (size_t slot = 0; slot < relative_offsets.size(); ++slot) {
            if (!be_u32(group.decompressed, 4 + slot * 4,
                        relative_offsets[slot])) {
                out.error = "program group offsets";
                return false;
            }
        }

        std::array<size_t, 4> record_starts{};
        if (payload_base > group.decompressed.size()) {
            out.error = "program payload base";
            return false;
        }
        for (size_t slot = 0; slot < record_starts.size(); ++slot) {
            if (relative_offsets[slot] >
                group.decompressed.size() - payload_base) {
                out.error = "program record offset";
                return false;
            }
            record_starts[slot] = size_t(payload_base) +
                                  relative_offsets[slot];
            if (record_starts[slot] > group.decompressed.size() ||
                (slot != 0 && record_starts[slot] <
                                  record_starts[slot - 1])) {
                out.error = "program record ordering";
                return false;
            }
        }

        for (size_t slot = 0; slot < record_starts.size(); ++slot) {
            const size_t record_end =
                slot + 1 < record_starts.size()
                    ? record_starts[slot + 1]
                    : group.decompressed.size();
            Program& program = out.programs[size_t(k) * 4 + slot];
            program.group_index = k;
            program.group_slot = static_cast<uint32_t>(slot);
            program.record_offset = static_cast<uint32_t>(record_starts[slot]);
            program.record.assign(
                group.decompressed.begin() + record_starts[slot],
                group.decompressed.begin() + record_end);
            if (!parse_program_record(program)) {
                out.error = "program record";
                return false;
            }
        }
    }

    out.ok = true;
    return true;
}

}
