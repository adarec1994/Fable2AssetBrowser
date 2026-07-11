#include "CloudShaderBank.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <sstream>

namespace CloudShaderBank {
namespace {

// IDA-Python provenance anchor for the retail bank used to recover these
// records: 653060 bytes, SHA-256
// c2732102a0aedb8c7a950e5781fa15092a3c12c2721380944c4e717dade5e736.

struct ExpectedBinding {
    std::uint32_t logical_id;
    std::uint32_t hardware_binding;
    std::uint32_t count;
    std::uint32_t tag;
};

struct ExpectedProgram {
    std::uint32_t shader_entry;
    std::uint32_t shader_hash;
    std::uint8_t shader_type;
    std::uint32_t program_ordinal;
    std::uint32_t group_index;
    std::uint32_t group_slot;
    std::uint32_t record_offset;
    std::size_t record_size;
    std::size_t compiled_size;
    std::uint32_t compiled_flags;
    std::uint32_t compiled_header_size;
    std::size_t ucode_size;
    std::uint32_t descriptor_offset;
    std::uint32_t subprogram_offset;
    std::uint32_t subprogram_size;
    std::uint32_t first_packet;
    std::size_t control_flow_count;
    std::size_t clause_instruction_count;
    std::size_t fetch_instruction_count;
    std::uint16_t anchor_fetch_packet;
    std::array<std::uint32_t, 3> anchor_fetch_words;
    const ExpectedBinding* bindings;
    std::size_t binding_count;
    const char* record_sha256;
    const char* compiled_sha256;
    const char* ucode_sha256;
};

constexpr std::array<ExpectedBinding, 11> kContext2Bindings = {{
    {0,   46, 1, 3},
    {9,   71, 1, 0},
    {23,  51, 1, 0},
    {37,  13, 1, 1}, // Texture logical 37 is fetch slot 13.
    {38,  31, 1, 3},
    {122, 47, 1, 5},
    {131, 77, 1, 5},
    {157, 70, 1, 0},
    {264, 69, 1, 0},
    {271, 78, 1, 5},
    {272, 79, 1, 5},
}};

constexpr std::array<ExpectedBinding, 5> kOtherContextBindings = {{
    {0,   46, 1, 3},
    {37,  13, 1, 1}, // Texture logical 37 is fetch slot 13.
    {38,  31, 1, 3},
    {264, 77, 1, 0},
    {272, 47, 1, 5},
}};

// 0x82B86DF8 declaration records selected by CloudLayer_Draw, with the
// exact encoded formats from table 0x83316B80.  The patcher 0x821D3318 uses
// DWORD offsets/strides; DrawPrimitiveUP installs 20/4 == 5 for stream zero.
constexpr std::array<XenosShaderBinary::VertexDeclarationElement, 2>
    kCloudVertexElements = {{
        {0, 0,  0x002a23b9u, 0, 0, 0},
        {0, 12, 0x002c23a5u, 0, 5, 0},
    }};

constexpr std::array<std::array<std::uint32_t, 3>, 2>
    kExpectedRuntimeVertexFetches = {{
        {{0x05f81000u, 0x00393a88u, 0x00000005u}},
        {{0x05f80000u, 0x00253fc8u, 0x00000305u}},
    }};

// Raw IEEE-754 words uploaded to pixel c254/c255 by sub_8222BFF8.
constexpr std::array<
    std::array<std::array<std::uint32_t, 4>, 2>, 3>
    kExpectedEmbeddedPixelConstants = {{
        {{{{0x00000000u, 0x3a83126fu, 0x447a0000u, 0x3f800000u}},
          {{0x42000000u, 0x00000000u, 0x00000000u, 0x00000000u}}}},
        {{{{0x3f000000u, 0x42000000u, 0x00000000u, 0x00000000u}},
          {{0x3f800000u, 0x00000000u, 0x3a83126fu, 0x447a0000u}}}},
        {{{{0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u}},
          {{0x447a0000u, 0x00000000u, 0x3a83126fu, 0x3f800000u}}}},
    }};

constexpr std::array<ExpectedProgram, 4> kExpectedPrograms = {{
    {
        135, 0xf75c0644u, 2, 113, 28, 1, 544,
        400, 356, 0x102a1101u, 212, 144,
        0x94, 0x00, 0x90, 3, 6, 8, 2, 3,
        {{0x05f81000u, 0x00000a88u, 0x00000000u}},
        nullptr, 0,
        "75b0cbc989f2e9096768890f45034e46381ddc5926798767a1a30eec3b46c867",
        "8b315fd7a41a514c7bf32e4257028f0882cab0c65a2f8c46e7f3d56825b8cfa6",
        "d28394276f47cfce7f48ccbab0a2c339ab069eef81d4eb15aa59790eb4a3dabb",
    },
    {
        136, 0x0f9e4df2u, 1, 114, 28, 2, 944,
        1504, 1284, 0x102a1100u, 704, 580,
        0x298, 0x40, 0x204, 6, 12, 36, 5, 7,
        {{0xa4d02021u, 0x1f1d5443u, 0x00004000u}},
        kContext2Bindings.data(), kContext2Bindings.size(),
        "376c0d7ad066ba13c7b9a2666ff4f2faab17e19257093cf2bfdcda036e02d01d",
        "10ab4f8ba6b5aa9821ca229fe5675b73d2c162fbb262fa667ad539a0c84db007",
        "fcae8f57a2df9fa43900457e69c9856b1a6e4cbe8eba2360c7dc0108a88b840d",
    },
    {
        137, 0x7a79ff27u, 1, 115, 28, 3, 2448,
        2548, 2328, 0x102a1100u, 1160, 1168,
        0x460, 0x40, 0x450, 10, 20, 81, 8, 11,
        {{0x50d02061u, 0x1f1d5458u, 0x00004000u}},
        kContext2Bindings.data(), kContext2Bindings.size(),
        "ef546a657f00b3cf039b74946ac97aef118340eabf91f1db1b85ee8728ae27f6",
        "469617c55e42470bab30b7cc9901fadc0dcfae538cdc3206b49c27f4c8d947bb",
        "faa2b6c405eb6f69ad2f00cc9c9d21a18e0333d69ac318027c60c5b42c428484",
    },
    {
        138, 0x98f766a6u, 1, 116, 29, 0, 20,
        756, 632, 0x102a1100u, 424, 208,
        0x180, 0x40, 0x90, 2, 4, 9, 1, 3,
        {{0x50d80021u, 0x1f1d57ffu, 0x00004000u}},
        kOtherContextBindings.data(), kOtherContextBindings.size(),
        "c379bf96b4329b5839f2724723e0f798fa2001b5d80a0daa20a3fdf0b1eed69a",
        "b6a25bf70ce4b808cf42ba9b2a37374a0679d12b9daf0be33827ce7e72f51713",
        "40de622e5340fa8476d89e228a81bf79d79387f2db4df491dce0ae8c9778afa3",
    },
}};

constexpr std::array<ExpectedBinding, 4> kBackgroundMapBindings = {{
    {101, 14, 1, 1},
    {141, 46, 1, 0},
    {142, 31, 1, 2},
    {171, 13, 1, 1},
}};

constexpr std::array<ExpectedProgram, 2> kExpectedBackgroundMapPrograms = {{
    {
        85, 0x11e95630u, 2, 71, 17, 3, 4652,
        284, 240, 0x102a1101u, 144, 96,
        0x58, 0x00, 0x60, 3, 6, 4, 2, 3,
        {{0x05f81000u, 0x00000a88u, 0x00000000u}},
        nullptr, 0,
        "d1e2494cf382928df71726b546a85d4f69352aefb77970e6d7c75b19ed138e8a",
        "9b9b88247c7a16efa576338ddae5b228be395dd3520651fec2d4a5930e5c5706",
        "b88a0cadefb415d93a12db89b588f4e60981972193c654c9bc523c02eb2fc4bf",
    },
    {
        86, 0x0f66d308u, 1, 72, 18, 0, 20,
        2092, 1984, 0x102a1100u, 360, 1624,
        0x144, 0x40, 0x618, 11, 22, 118, 44, 23,
        {{0xfcd03041u, 0x1f1ffff8u, 0x00000000u}},
        kBackgroundMapBindings.data(), kBackgroundMapBindings.size(),
        "46940458b043f85f821a8c618f36d8c05639aa6899322499d8f6a7303c9af91e",
        "e57391bfd6a143b852943bafb2eb59d0048665b1e35f18ebf046611a4f244691",
        "467973f8074b9eab8846cc6ec298b583370218ff737f074569f9813f9053e242",
    },
}};

constexpr std::array<std::uint32_t, 64> kSha256RoundConstants = {{
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
}};

std::uint32_t RotateRight(std::uint32_t value, unsigned amount)
{
    return (value >> amount) | (value << (32u - amount));
}

std::uint32_t ReadBigEndian(const std::uint8_t* bytes)
{
    return (std::uint32_t(bytes[0]) << 24) |
           (std::uint32_t(bytes[1]) << 16) |
           (std::uint32_t(bytes[2]) << 8) |
           std::uint32_t(bytes[3]);
}

std::uint16_t ReadBigEndian16(const std::uint8_t* bytes)
{
    return static_cast<std::uint16_t>(
        (std::uint16_t(bytes[0]) << 8) | std::uint16_t(bytes[1]));
}

void Sha256Transform(std::array<std::uint32_t, 8>& state,
                     const std::uint8_t* block)
{
    std::array<std::uint32_t, 64> words{};
    for (std::size_t i = 0; i < 16; ++i) {
        words[i] = ReadBigEndian(block + i * 4);
    }
    for (std::size_t i = 16; i < words.size(); ++i) {
        const std::uint32_t s0 = RotateRight(words[i - 15], 7) ^
                                 RotateRight(words[i - 15], 18) ^
                                 (words[i - 15] >> 3);
        const std::uint32_t s1 = RotateRight(words[i - 2], 17) ^
                                 RotateRight(words[i - 2], 19) ^
                                 (words[i - 2] >> 10);
        words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }

    std::uint32_t a = state[0];
    std::uint32_t b = state[1];
    std::uint32_t c = state[2];
    std::uint32_t d = state[3];
    std::uint32_t e = state[4];
    std::uint32_t f = state[5];
    std::uint32_t g = state[6];
    std::uint32_t h = state[7];
    for (std::size_t i = 0; i < words.size(); ++i) {
        const std::uint32_t upper_e = RotateRight(e, 6) ^
                                      RotateRight(e, 11) ^
                                      RotateRight(e, 25);
        const std::uint32_t choice = (e & f) ^ (~e & g);
        const std::uint32_t temporary1 = h + upper_e + choice +
            kSha256RoundConstants[i] + words[i];
        const std::uint32_t upper_a = RotateRight(a, 2) ^
                                      RotateRight(a, 13) ^
                                      RotateRight(a, 22);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temporary2 = upper_a + majority;
        h = g;
        g = f;
        f = e;
        e = d + temporary1;
        d = c;
        c = b;
        b = a;
        a = temporary1 + temporary2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

std::array<std::uint8_t, 32> Sha256(ByteView bytes)
{
    std::array<std::uint32_t, 8> state = {{
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
    }};

    std::size_t consumed = 0;
    while (bytes.size - consumed >= 64) {
        Sha256Transform(state, bytes.data + consumed);
        consumed += 64;
    }

    std::array<std::uint8_t, 128> tail{};
    const std::size_t remainder = bytes.size - consumed;
    if (remainder != 0) {
        std::memcpy(tail.data(), bytes.data + consumed, remainder);
    }
    tail[remainder] = 0x80;
    const std::size_t tail_size = remainder < 56 ? 64 : 128;
    const std::uint64_t bit_length =
        static_cast<std::uint64_t>(bytes.size) * 8u;
    for (unsigned i = 0; i < 8; ++i) {
        tail[tail_size - 1 - i] =
            static_cast<std::uint8_t>(bit_length >> (i * 8));
    }
    Sha256Transform(state, tail.data());
    if (tail_size == 128) Sha256Transform(state, tail.data() + 64);

    std::array<std::uint8_t, 32> digest{};
    for (std::size_t word = 0; word < state.size(); ++word) {
        digest[word * 4] = static_cast<std::uint8_t>(state[word] >> 24);
        digest[word * 4 + 1] =
            static_cast<std::uint8_t>(state[word] >> 16);
        digest[word * 4 + 2] =
            static_cast<std::uint8_t>(state[word] >> 8);
        digest[word * 4 + 3] = static_cast<std::uint8_t>(state[word]);
    }
    return digest;
}

bool DigestMatches(ByteView bytes, const char* expected_hex)
{
    static constexpr char kHex[] = "0123456789abcdef";
    const std::array<std::uint8_t, 32> digest = Sha256(bytes);
    for (std::size_t i = 0; i < digest.size(); ++i) {
        if (expected_hex[i * 2] != kHex[digest[i] >> 4] ||
            expected_hex[i * 2 + 1] != kHex[digest[i] & 0x0f]) {
            return false;
        }
    }
    return expected_hex[64] == '\0';
}

ByteView View(const std::vector<std::uint8_t>& bytes)
{
    return {bytes.data(), bytes.size()};
}

bool Fail(std::string* error, const ExpectedProgram* expected,
          const char* detail)
{
    if (error != nullptr) {
        std::ostringstream message;
        if (expected != nullptr) {
            message << "cloud shader entry " << expected->shader_entry
                    << ": ";
        }
        message << detail;
        *error = message.str();
    }
    return false;
}

bool ValidateBindings(const ShaderBank::Program& program,
                      const ExpectedProgram& expected)
{
    if (program.binding_lists[0].size() != expected.binding_count ||
        !program.binding_lists[1].empty()) {
        return false;
    }
    for (std::size_t i = 0; i < expected.binding_count; ++i) {
        const ShaderBank::BindingDescriptor& actual =
            program.binding_lists[0][i];
        const ExpectedBinding& wanted = expected.bindings[i];
        // hardware_binding is the direct constant-register index (or, for
        // logical texture 37, fetch slot 13).  Constant dirty tracking later
        // groups four registers; it does not change this serialized value.
        if (actual.logical_id != wanted.logical_id ||
            actual.hardware_binding != wanted.hardware_binding ||
            actual.count != wanted.count || actual.tag != wanted.tag) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool ResolveRetailCloudShaders(const ShaderBank::Bank& bank,
                               RetailCloudShaders& out,
                               std::string* error)
{
    out = RetailCloudShaders{};
    if (error != nullptr) error->clear();

    if (!bank.ok) return Fail(error, nullptr, "shader bank is not parsed");
    if (bank.version != 3) {
        return Fail(error, nullptr, "retail shader bank version is not 3");
    }
    if (bank.stores_shader_names != 0) {
        return Fail(error, nullptr, "shader bank is not release/hash mode");
    }
    if (bank.bank_hash != 0x6c0c9bacu) {
        return Fail(error, nullptr, "retail shader bank hash mismatch");
    }

    for (std::size_t expected_index = 0;
         expected_index < kExpectedPrograms.size(); ++expected_index) {
        const ExpectedProgram& expected = kExpectedPrograms[expected_index];
        if (expected.shader_entry >= bank.shaders.size()) {
            return Fail(error, &expected, "shader table entry is missing");
        }
        const ShaderBank::ShaderEntry& shader =
            bank.shaders[expected.shader_entry];
        if (!shader.name.empty() || shader.name_hash != expected.shader_hash ||
            shader.type != expected.shader_type ||
            shader.program_ordinal != expected.program_ordinal) {
            return Fail(error, &expected, "shader entry metadata mismatch");
        }
        if (expected.program_ordinal >= bank.programs.size()) {
            return Fail(error, &expected, "packed program is missing");
        }
        const ShaderBank::Program& program =
            bank.programs[expected.program_ordinal];
        if (program.group_index != expected.group_index ||
            program.group_slot != expected.group_slot ||
            program.record_offset != expected.record_offset) {
            return Fail(error, &expected,
                        "packed group, slot, or record offset mismatch");
        }
        if (program.record.size() != expected.record_size ||
            program.compiled_shader.size() != expected.compiled_size ||
            program.compiled_header_size != expected.compiled_header_size ||
            program.xenos_ucode.size() != expected.ucode_size) {
            return Fail(error, &expected, "program byte size mismatch");
        }
        if (program.compiled_flags != expected.compiled_flags) {
            return Fail(error, &expected, "compiled shader flags mismatch");
        }
        if (!ValidateBindings(program, expected)) {
            return Fail(error, &expected, "binding list mismatch");
        }
        if (!DigestMatches(View(program.record), expected.record_sha256)) {
            return Fail(error, &expected, "program record SHA-256 mismatch");
        }
        if (!DigestMatches(View(program.compiled_shader),
                           expected.compiled_sha256)) {
            return Fail(error, &expected,
                        "compiled shader SHA-256 mismatch");
        }
        if (!DigestMatches(View(program.xenos_ucode),
                           expected.ucode_sha256)) {
            return Fail(error, &expected, "Xenos ucode SHA-256 mismatch");
        }

        ProgramView view{};
        view.shader_entry = expected.shader_entry;
        view.program_ordinal = expected.program_ordinal;
        view.shader = &shader;
        view.program = &program;
        view.record = View(program.record);
        view.compiled_shader = View(program.compiled_shader);
        view.xenos_ucode = View(program.xenos_ucode);
        std::string decode_error;
        if (!XenosShaderBinary::DecodeProgram(
                {view.compiled_shader.data, view.compiled_shader.size},
                {view.xenos_ucode.data, view.xenos_ucode.size},
                view.decoded_program, &decode_error)) {
            if (error != nullptr) {
                std::ostringstream message;
                message << "cloud shader entry " << expected.shader_entry
                        << ": Xenos decode failed: " << decode_error;
                *error = message.str();
            }
            return false;
        }
        const XenosShaderBinary::ProgramDescriptor& descriptor =
            view.decoded_program.descriptor;
        if (descriptor.descriptor_offset != expected.descriptor_offset ||
            descriptor.subprogram_offset != expected.subprogram_offset ||
            descriptor.subprogram_size != expected.subprogram_size ||
            descriptor.first_packet != expected.first_packet) {
            return Fail(error, &expected,
                        "Xenos subprogram descriptor mismatch");
        }
        const std::size_t fetch_count = static_cast<std::size_t>(
            std::count_if(
                view.decoded_program.clause_instructions.begin(),
                view.decoded_program.clause_instructions.end(),
                [](const XenosShaderBinary::ClauseInstruction& instruction) {
                    return instruction.kind ==
                        XenosShaderBinary::InstructionKind::Fetch;
                }));
        if (view.decoded_program.control_flow.size() !=
                expected.control_flow_count ||
            view.decoded_program.clause_instructions.size() !=
                expected.clause_instruction_count ||
            fetch_count != expected.fetch_instruction_count) {
            return Fail(error, &expected,
                        "Xenos decoded instruction counts mismatch");
        }
        const auto anchor = std::find_if(
            view.decoded_program.clause_instructions.begin(),
            view.decoded_program.clause_instructions.end(),
            [&](const XenosShaderBinary::ClauseInstruction& instruction) {
                return instruction.kind ==
                           XenosShaderBinary::InstructionKind::Fetch &&
                       instruction.packet_index ==
                           expected.anchor_fetch_packet;
            });
        if (anchor == view.decoded_program.clause_instructions.end() ||
            anchor->words != expected.anchor_fetch_words) {
            return Fail(error, &expected,
                        "Xenos anchor fetch packet mismatch");
        }
        if (expected_index == 0) {
            for (std::size_t fetch_index = 0;
                 fetch_index < kCloudVertexElements.size(); ++fetch_index) {
                const std::uint16_t packet = static_cast<std::uint16_t>(
                    3u + fetch_index);
                const auto raw = std::find_if(
                    view.decoded_program.clause_instructions.begin(),
                    view.decoded_program.clause_instructions.end(),
                    [&](const XenosShaderBinary::ClauseInstruction& instruction) {
                        return instruction.kind ==
                                   XenosShaderBinary::InstructionKind::Fetch &&
                               instruction.packet_index == packet &&
                               (instruction.words[0] & 0x1fu) == 0;
                    });
                if (raw == view.decoded_program.clause_instructions.end()) {
                    return Fail(error, &expected,
                                "cloud vertex-fetch placeholder is missing");
                }
                view.runtime_vertex_fetch_words[fetch_index] =
                    XenosShaderBinary::PatchVertexFetch(
                        raw->words, kCloudVertexElements[fetch_index], 5, 0);
                if (view.runtime_vertex_fetch_words[fetch_index] !=
                    kExpectedRuntimeVertexFetches[fetch_index]) {
                    return Fail(error, &expected,
                                "runtime vertex-fetch patch differs");
                }
                const XenosShaderBinary::VertexFetchInstruction patched =
                    XenosShaderBinary::DecodeVertexFetch(
                        view.runtime_vertex_fetch_words[fetch_index]);
                const std::uint8_t expected_format =
                    fetch_index == 0 ? 57u : 37u;
                const std::uint32_t expected_offset =
                    fetch_index == 0 ? 0u : 3u;
                if (patched.data_format != expected_format ||
                    patched.stride_dwords != 5 ||
                    patched.offset_dwords != expected_offset) {
                    return Fail(error, &expected,
                                "runtime vertex-fetch fields differ");
                }
            }
            view.runtime_vertex_fetches_valid = true;
            out.vertex_shader = view;
        } else {
            // Pixel constructor/flush path 0x82B84550 -> 0x8221B140 ->
            // 0x8222BFF8 consumes this exact one-entry compiler upload list.
            if (view.compiled_shader.size < 0x18) {
                return Fail(error, &expected,
                            "pixel constant-upload root is missing");
            }
            const std::uint32_t upload_root = ReadBigEndian(
                view.compiled_shader.data + 0x14);
            if (upload_root > view.compiled_shader.size ||
                view.compiled_shader.size - upload_root < 0x1c) {
                return Fail(error, &expected,
                            "pixel constant-upload record is outside metadata");
            }
            const std::uint8_t* upload =
                view.compiled_shader.data + upload_root;
            const std::uint32_t upload_count = ReadBigEndian(upload + 4);
            const std::uint32_t entry_offset = ReadBigEndian(upload + 0x10);
            if (upload_count != 1 || entry_offset != 0x14 ||
                entry_offset > view.compiled_shader.size - upload_root ||
                view.compiled_shader.size - upload_root - entry_offset < 8) {
                return Fail(error, &expected,
                            "pixel constant-upload descriptor differs");
            }
            const std::uint8_t* entry = upload + entry_offset;
            if (ReadBigEndian16(entry) != 0x01fcu ||
                ReadBigEndian16(entry + 2) != 0x0010u ||
                ReadBigEndian(entry + 4) != 0 ||
                view.decoded_program.literal_prefix.size != 0x40) {
                return Fail(error, &expected,
                            "pixel literal upload range differs");
            }
            if (!std::all_of(
                    view.decoded_program.literal_prefix.data,
                    view.decoded_program.literal_prefix.data + 0x20,
                    [](std::uint8_t value) { return value == 0; })) {
                return Fail(error, &expected,
                            "embedded pixel c252/c253 are nonzero");
            }
            for (std::size_t constant = 0; constant < 2; ++constant) {
                for (std::size_t component = 0; component < 4; ++component) {
                    view.embedded_pixel_constant_words[constant][component] =
                        ReadBigEndian(
                            view.decoded_program.literal_prefix.data + 0x20 +
                            constant * 16 + component * 4);
                }
            }
            if (view.embedded_pixel_constant_words !=
                kExpectedEmbeddedPixelConstants[expected_index - 1]) {
                return Fail(error, &expected,
                            "embedded pixel c254/c255 words differ");
            }
            view.embedded_pixel_constants_valid = true;
            out.pixel_shaders[expected_index - 1] = view;
        }
    }

    out.valid = true;
    return true;
}

bool ResolveRetailBackgroundMapShaders(
    const ShaderBank::Bank& bank,
    RetailBackgroundMapShaders& out,
    std::string* error)
{
    out = RetailBackgroundMapShaders{};
    if (error != nullptr) error->clear();

    if (!bank.ok) return Fail(error, nullptr, "shader bank is not parsed");
    if (bank.version != 3 || bank.stores_shader_names != 0 ||
        bank.bank_hash != 0x6c0c9bacu) {
        return Fail(error, nullptr,
                    "retail shader-bank identity differs");
    }

    for (std::size_t expected_index = 0;
         expected_index < kExpectedBackgroundMapPrograms.size();
         ++expected_index) {
        const ExpectedProgram& expected =
            kExpectedBackgroundMapPrograms[expected_index];
        if (expected.shader_entry >= bank.shaders.size()) {
            return Fail(error, &expected, "shader table entry is missing");
        }
        const ShaderBank::ShaderEntry& shader =
            bank.shaders[expected.shader_entry];
        if (!shader.name.empty() || shader.name_hash != expected.shader_hash ||
            shader.type != expected.shader_type ||
            shader.program_ordinal != expected.program_ordinal) {
            return Fail(error, &expected, "shader entry metadata mismatch");
        }
        if (expected.program_ordinal >= bank.programs.size()) {
            return Fail(error, &expected, "packed program is missing");
        }
        const ShaderBank::Program& program =
            bank.programs[expected.program_ordinal];
        if (program.group_index != expected.group_index ||
            program.group_slot != expected.group_slot ||
            program.record_offset != expected.record_offset ||
            program.record.size() != expected.record_size ||
            program.compiled_shader.size() != expected.compiled_size ||
            program.compiled_flags != expected.compiled_flags ||
            program.compiled_header_size != expected.compiled_header_size ||
            program.xenos_ucode.size() != expected.ucode_size) {
            return Fail(error, &expected,
                        "packed program metadata mismatch");
        }
        if (!ValidateBindings(program, expected)) {
            return Fail(error, &expected, "binding list mismatch");
        }
        if (!DigestMatches(View(program.record), expected.record_sha256) ||
            !DigestMatches(View(program.compiled_shader),
                           expected.compiled_sha256) ||
            !DigestMatches(View(program.xenos_ucode),
                           expected.ucode_sha256)) {
            return Fail(error, &expected, "program SHA-256 mismatch");
        }

        ProgramView view{};
        view.shader_entry = expected.shader_entry;
        view.program_ordinal = expected.program_ordinal;
        view.shader = &shader;
        view.program = &program;
        view.record = View(program.record);
        view.compiled_shader = View(program.compiled_shader);
        view.xenos_ucode = View(program.xenos_ucode);
        std::string decode_error;
        if (!XenosShaderBinary::DecodeProgram(
                {view.compiled_shader.data, view.compiled_shader.size},
                {view.xenos_ucode.data, view.xenos_ucode.size},
                view.decoded_program, &decode_error)) {
            if (error != nullptr) {
                std::ostringstream message;
                message << "background-map shader entry "
                        << expected.shader_entry
                        << ": Xenos decode failed: " << decode_error;
                *error = message.str();
            }
            return false;
        }
        const XenosShaderBinary::ProgramDescriptor& descriptor =
            view.decoded_program.descriptor;
        if (descriptor.descriptor_offset != expected.descriptor_offset ||
            descriptor.subprogram_offset != expected.subprogram_offset ||
            descriptor.subprogram_size != expected.subprogram_size ||
            descriptor.first_packet != expected.first_packet) {
            return Fail(error, &expected,
                        "Xenos subprogram descriptor mismatch");
        }
        const std::size_t fetch_count = static_cast<std::size_t>(
            std::count_if(
                view.decoded_program.clause_instructions.begin(),
                view.decoded_program.clause_instructions.end(),
                [](const XenosShaderBinary::ClauseInstruction& instruction) {
                    return instruction.kind ==
                        XenosShaderBinary::InstructionKind::Fetch;
                }));
        if (view.decoded_program.control_flow.size() !=
                expected.control_flow_count ||
            view.decoded_program.clause_instructions.size() !=
                expected.clause_instruction_count ||
            fetch_count != expected.fetch_instruction_count) {
            return Fail(error, &expected,
                        "Xenos decoded instruction counts mismatch");
        }
        const auto anchor = std::find_if(
            view.decoded_program.clause_instructions.begin(),
            view.decoded_program.clause_instructions.end(),
            [&](const XenosShaderBinary::ClauseInstruction& instruction) {
                return instruction.kind ==
                           XenosShaderBinary::InstructionKind::Fetch &&
                       instruction.packet_index ==
                           expected.anchor_fetch_packet;
            });
        if (anchor == view.decoded_program.clause_instructions.end() ||
            anchor->words != expected.anchor_fetch_words) {
            return Fail(error, &expected,
                        "Xenos anchor fetch packet mismatch");
        }

        if (expected_index == 0) out.vertex_shader = view;
        else out.pixel_shader = view;
    }
    out.valid = true;
    return true;
}

std::uint32_t SelectPixelShaderEntry(
    std::uint32_t render_context,
    std::uint32_t background_map_argument) noexcept
{
    if (render_context == 2) {
        return background_map_argument != 0
            ? kPixelShaderEntryContext2Background
            : kPixelShaderEntryContext2;
    }
    return kPixelShaderEntryOtherContext;
}

const ProgramView* SelectPixelShader(
    const RetailCloudShaders& shaders,
    std::uint32_t render_context,
    std::uint32_t background_map_argument) noexcept
{
    if (!shaders.valid) return nullptr;
    const std::uint32_t entry = SelectPixelShaderEntry(
        render_context, background_map_argument);
    const std::size_t index =
        static_cast<std::size_t>(entry - kPixelShaderEntryContext2);
    if (index >= shaders.pixel_shaders.size()) return nullptr;
    const ProgramView& view = shaders.pixel_shaders[index];
    return view.shader_entry == entry && view.program != nullptr
        ? &view : nullptr;
}

}  // namespace CloudShaderBank
