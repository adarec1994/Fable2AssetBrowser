#include "LightmapFile.h"

#include "Level/Loading/LevelBinaryReader.h"
#include "Level/Terrain/TextureAtlasDecoder.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <sstream>
#include <vector>
#include <zlib.h>

namespace Level {
namespace {

constexpr char kMagic[] = "LightmapFile";
constexpr size_t kMagicSize = sizeof(kMagic) - 1;
constexpr size_t kMaxInflatedBytes = 512ull * 1024ull * 1024ull;

bool inflate_gzip(const std::vector<uint8_t>& input,
                  std::vector<uint8_t>& output,
                  std::string& error) {
    output.clear();
    if (input.empty() || input.size() > std::numeric_limits<uInt>::max()) {
        error = "LMP gzip input is empty or too large";
        return false;
    }

    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(input.data());
    stream.avail_in = static_cast<uInt>(input.size());
    if (inflateInit2(&stream, 15 + 32) != Z_OK) {
        error = "LMP gzip decompressor initialization failed";
        return false;
    }

    std::array<uint8_t, 64 * 1024> chunk{};
    bool ok = false;
    for (;;) {
        stream.next_out = chunk.data();
        stream.avail_out = static_cast<uInt>(chunk.size());
        const int result = inflate(&stream, Z_NO_FLUSH);
        const size_t produced = chunk.size() - stream.avail_out;
        if (produced > kMaxInflatedBytes - output.size()) {
            error = "LMP inflated payload exceeds the safety limit";
            break;
        }
        output.insert(output.end(), chunk.begin(),
                      chunk.begin() + static_cast<std::ptrdiff_t>(produced));
        if (result == Z_STREAM_END) {
            ok = true;
            break;
        }
        if (result != Z_OK) {
            std::ostringstream os;
            os << "LMP gzip inflate failed (zlib " << result << ")";
            error = os.str();
            break;
        }
        if (produced == 0 && stream.avail_in == 0) {
            error = "LMP gzip stream ended without a terminator";
            break;
        }
    }
    inflateEnd(&stream);
    if (!ok) output.clear();
    return ok;
}

bool skip_counted_words(BeReader& reader, uint32_t count) {
    const size_t remaining = reader.n - reader.i;
    return size_t(count) <= remaining / 4u &&
           reader.skip(size_t(count) * 4u);
}

struct RawStaticRecord {
    uint64_t key = 0;
    size_t begin = 0;
    size_t end = 0;
};

struct RawLmpLayout {
    size_t static_count_offset = 0;
    uint32_t static_count = 0;
    size_t static_end = 0;
    std::vector<RawStaticRecord> static_records;
};

bool parse_raw_layout(const std::vector<uint8_t>& raw,
                      RawLmpLayout& layout,
                      std::string& error) {
    layout = {};
    BeReader reader{raw.data(), raw.size(), 0};
    if (!reader.need(kMagicSize) ||
        std::memcmp(reader.p + reader.i, kMagic, kMagicSize) != 0 ||
        !reader.skip(kMagicSize)) {
        error = "LMP magic mismatch";
        return false;
    }
    uint32_t version = 0;
    uint32_t ignored = 0;
    if (!reader.u32(version) || version < 10 || version > 12) {
        error = "LMP version is not supported (expected 10..12)";
        return false;
    }
    if (version >= 12 && !reader.u32(ignored)) {
        error = "LMP version-12 header is truncated";
        return false;
    }

    layout.static_count_offset = reader.i;
    if (!reader.u32(layout.static_count) ||
        size_t(layout.static_count) > (reader.n - reader.i) / 20u) {
        error = "LMP static-lightmap count is invalid";
        return false;
    }
    layout.static_records.reserve(layout.static_count);
    for (uint32_t i = 0; i < layout.static_count; ++i) {
        RawStaticRecord record;
        record.begin = reader.i;
        uint32_t sample_width = 0;
        uint32_t sample_height = 0;
        uint32_t blob_size = 0;
        if (!reader.u64(record.key) || !reader.u32(sample_width) ||
            !reader.u32(sample_height) || !reader.u32(blob_size) ||
            blob_size > reader.n - reader.i || !reader.skip(blob_size)) {
            error = "LMP static-lightmap record is truncated";
            return false;
        }
        record.end = reader.i;
        layout.static_records.push_back(record);
    }
    layout.static_end = reader.i;

    uint32_t count = 0;
    if (!reader.u32(count) || size_t(count) > (reader.n - reader.i) / 13u) {
        error = "LMP variant-resource count is invalid";
        return false;
    }
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t kind = 0;
        uint64_t key = 0;
        uint32_t count_or_size = 0;
        if (!reader.u8(kind) || !reader.u64(key) ||
            !reader.u32(count_or_size)) {
            error = "LMP variant-resource record is truncated";
            return false;
        }
        const bool skipped = kind != 0
            ? reader.skip(count_or_size)
            : skip_counted_words(reader, count_or_size);
        if (!skipped) {
            error = "LMP variant-resource payload is truncated";
            return false;
        }
    }

    if (!reader.u32(count) || size_t(count) > (reader.n - reader.i) / 12u) {
        error = "LMP word-resource count is invalid";
        return false;
    }
    for (uint32_t i = 0; i < count; ++i) {
        uint64_t key = 0;
        uint32_t word_count = 0;
        if (!reader.u64(key) || !reader.u32(word_count) ||
            !skip_counted_words(reader, word_count)) {
            error = "LMP word-resource record is truncated";
            return false;
        }
    }

    if (version >= 11) {
        if (!reader.u32(count) || size_t(count) > (reader.n - reader.i) / 56u ||
            !reader.skip(size_t(count) * 56u)) {
            error = "LMP fixed-resource section is truncated";
            return false;
        }
    }
    if (reader.i != reader.n) {
        error = "LMP has trailing bytes after its final section";
        return false;
    }
    return true;
}

void write_be_u32(std::vector<uint8_t>& bytes, size_t offset,
                  uint32_t value) {
    bytes[offset + 0] = static_cast<uint8_t>(value >> 24);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 16);
    bytes[offset + 2] = static_cast<uint8_t>(value >> 8);
    bytes[offset + 3] = static_cast<uint8_t>(value);
}

bool deflate_gzip(const std::vector<uint8_t>& raw,
                  std::vector<uint8_t>& gzip,
                  std::string& error) {
    gzip.clear();
    if (raw.empty() || raw.size() > std::numeric_limits<uInt>::max()) {
        error = "LMP inflated payload is empty or too large to encode";
        return false;
    }
    z_stream stream{};
    if (deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED,
                     15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        error = "LMP gzip compressor initialization failed";
        return false;
    }
    const uLong bound = deflateBound(&stream, static_cast<uLong>(raw.size()));
    if (bound > std::numeric_limits<uInt>::max() - 64u) {
        deflateEnd(&stream);
        error = "LMP gzip output bound is too large";
        return false;
    }
    gzip.resize(static_cast<size_t>(bound) + 64u);
    stream.next_in = const_cast<Bytef*>(raw.data());
    stream.avail_in = static_cast<uInt>(raw.size());
    stream.next_out = gzip.data();
    stream.avail_out = static_cast<uInt>(gzip.size());
    const int result = deflate(&stream, Z_FINISH);
    if (result != Z_STREAM_END) {
        deflateEnd(&stream);
        gzip.clear();
        error = "LMP gzip compression failed";
        return false;
    }
    gzip.resize(gzip.size() - stream.avail_out);
    deflateEnd(&stream);
    return true;
}

bool decode_static_texture(const uint8_t* data, size_t size,
                           TerrainLightmap& out, std::string& error) {
    if (!data || size < 28) {
        error = "LMP static texture blob is too small";
        return false;
    }

    BeReader reader{data, size, 0};
    uint32_t marker_word = 0;
    if (!reader.u32(marker_word)) {
        error = "LMP static texture marker is truncated";
        return false;
    }
    const int32_t marker = static_cast<int32_t>(marker_word);
    uint32_t version = 1;
    if (marker < 0) {
        if (marker == std::numeric_limits<int32_t>::min()) {
            error = "LMP static texture version is invalid";
            return false;
        }
        version = static_cast<uint32_t>(-marker);
    }
    if (version == 0 || version > 16) {
        error = "LMP static texture version is outside the supported range";
        return false;
    }

    std::array<uint32_t, 7> scalars{};
    if (marker < 0) {
        for (uint32_t& value : scalars) {
            if (!reader.u32(value)) {
                error = "LMP static texture scalar header is truncated";
                return false;
            }
        }
    } else {
        scalars[0] = marker_word;
        for (size_t i = 1; i < scalars.size(); ++i) {
            if (!reader.u32(scalars[i])) {
                error = "LMP static texture scalar header is truncated";
                return false;
            }
        }
    }

    uint32_t data_offset = static_cast<uint32_t>(reader.i);
    if (version >= 2) {
        std::array<uint32_t, 12> mip_offsets{};
        for (uint32_t& value : mip_offsets) {
            if (!reader.u32(value)) {
                error = "LMP static texture mip-offset table is truncated";
                return false;
            }
        }
        uint32_t extra = 0;
        if (!reader.u32(extra)) {
            error = "LMP static texture v2 tail is truncated";
            return false;
        }
        (void)extra;
        data_offset = mip_offsets[0];
    } else if (scalars[2] & 1u) {
        if (!skip_counted_words(reader, scalars[6])) {
            error = "LMP static texture v1 mip offsets are truncated";
            return false;
        }
        data_offset = static_cast<uint32_t>(reader.i);
    }

    if (data_offset < reader.i || data_offset > size) {
        error = "LMP static texture data offset is invalid";
        return false;
    }
    if (!reader.skip(size_t(data_offset) - reader.i)) {
        error = "LMP static texture header/data gap is truncated";
        return false;
    }

    const uint32_t encoded_size = scalars[0];
    const uint32_t flags = scalars[2];
    const uint32_t width = scalars[3];
    const uint32_t height = scalars[4];
    const uint32_t pixel_format = scalars[5];
    const uint32_t mip_count = scalars[6];
    if ((flags & 2u) == 0 || (flags & 4u) != 0) {
        std::ostringstream os;
        os << "LMP static texture uses unsupported storage flags 0x"
           << std::hex << flags;
        error = os.str();
        return false;
    }
    if (pixel_format != 35u) {
        error = "LMP static texture is not PF35/BC1";
        return false;
    }
    if (width == 0 || height == 0 || width > 16384 || height > 16384 ||
        mip_count == 0) {
        error = "LMP static texture dimensions or mip count are invalid";
        return false;
    }
    if (encoded_size < 4 || encoded_size > size - reader.i ||
        reader.i + encoded_size != size) {
        error = "LMP static texture encoded region has an invalid length";
        return false;
    }

    uint32_t raw_size = 0;
    if (!reader.u32(raw_size) || raw_size != encoded_size - 4u ||
        raw_size > reader.n - reader.i) {
        error = "LMP static texture raw payload length is inconsistent";
        return false;
    }

    std::vector<uint8_t> rgba;
    if (!TextureAtlas::DecodeTiledBc1ToRgba(
            reader.p + reader.i, raw_size,
            static_cast<int>(width), static_cast<int>(height), rgba)) {
        error = "LMP static PF35 texture decode failed";
        return false;
    }

    out.texture_width = width;
    out.texture_height = height;
    out.pixel_format = pixel_format;
    out.storage_flags = flags;
    out.rgba = std::move(rgba);
    return true;
}

}

bool DecodeTerrainLightmap(const std::vector<uint8_t>& gzip_bytes,
                           uint64_t key,
                           TerrainLightmap& out) {
    out = {};
    out.key = key;

    std::vector<uint8_t> raw;
    if (!inflate_gzip(gzip_bytes, raw, out.error)) return false;
    BeReader reader{raw.data(), raw.size(), 0};
    if (!reader.need(kMagicSize) ||
        std::memcmp(reader.p + reader.i, kMagic, kMagicSize) != 0 ||
        !reader.skip(kMagicSize)) {
        out.error = "LMP magic mismatch";
        return false;
    }
    if (!reader.u32(out.file_version) ||
        out.file_version < 10 || out.file_version > 12) {
        out.error = "LMP version is not supported (expected 10..12)";
        return false;
    }
    if (out.file_version >= 12 && !reader.u32(out.version12_word)) {
        out.error = "LMP version-12 header is truncated";
        return false;
    }

    if (!reader.u32(out.static_record_count) ||
        size_t(out.static_record_count) > (reader.n - reader.i) / 20u) {
        out.error = "LMP static-lightmap count is invalid";
        return false;
    }

    uint32_t matching_records = 0;
    for (uint32_t i = 0; i < out.static_record_count; ++i) {
        uint64_t record_key = 0;
        uint32_t sample_width = 0;
        uint32_t sample_height = 0;
        uint32_t blob_size = 0;
        if (!reader.u64(record_key) || !reader.u32(sample_width) ||
            !reader.u32(sample_height) || !reader.u32(blob_size) ||
            blob_size > reader.n - reader.i) {
            out.error = "LMP static-lightmap record is truncated";
            return false;
        }
        if (record_key == key) {
            ++matching_records;
            if (matching_records > 1) {
                out.error = "LMP contains duplicate static-lightmap keys";
                return false;
            }
            out.sample_width = sample_width;
            out.sample_height = sample_height;
            if (!decode_static_texture(reader.p + reader.i, blob_size,
                                       out, out.error)) {
                return false;
            }
        }
        if (!reader.skip(blob_size)) {
            out.error = "LMP static-lightmap blob exceeds the file";
            return false;
        }
    }

    if (!reader.u32(out.variant_record_count) ||
        size_t(out.variant_record_count) > (reader.n - reader.i) / 13u) {
        out.error = "LMP variant-resource count is invalid";
        return false;
    }
    for (uint32_t i = 0; i < out.variant_record_count; ++i) {
        uint8_t kind = 0;
        uint64_t ignored_key = 0;
        uint32_t count_or_size = 0;
        if (!reader.u8(kind) || !reader.u64(ignored_key) ||
            !reader.u32(count_or_size)) {
            out.error = "LMP variant-resource record is truncated";
            return false;
        }
        const bool skipped = kind != 0
            ? reader.skip(count_or_size)
            : skip_counted_words(reader, count_or_size);
        if (!skipped) {
            out.error = "LMP variant-resource payload is truncated";
            return false;
        }
    }

    if (!reader.u32(out.word_record_count) ||
        size_t(out.word_record_count) > (reader.n - reader.i) / 12u) {
        out.error = "LMP word-resource count is invalid";
        return false;
    }
    for (uint32_t i = 0; i < out.word_record_count; ++i) {
        uint64_t ignored_key = 0;
        uint32_t word_count = 0;
        if (!reader.u64(ignored_key) || !reader.u32(word_count) ||
            !skip_counted_words(reader, word_count)) {
            out.error = "LMP word-resource record is truncated";
            return false;
        }
    }

    if (out.file_version >= 11) {
        if (!reader.u32(out.fixed_record_count) ||
            size_t(out.fixed_record_count) > (reader.n - reader.i) / 56u) {
            out.error = "LMP fixed-resource count is invalid";
            return false;
        }
        const size_t fixed_bytes = size_t(out.fixed_record_count) * 56u;
        if (!reader.skip(fixed_bytes)) {
            out.error = "LMP fixed-resource section is truncated";
            return false;
        }
    }

    if (reader.i != reader.n) {
        out.error = "LMP has trailing bytes after its final section";
        return false;
    }
    if (matching_records != 1) {
        std::ostringstream os;
        os << "LMP has no static-lightmap record for key 0x"
           << std::hex << key;
        out.error = os.str();
        return false;
    }
    if (out.sample_width != out.texture_width + 1u ||
        out.sample_height != out.texture_height + 1u) {
        out.error = "LMP static-lightmap sample and texture dimensions disagree";
        return false;
    }

    out.ok = true;
    return true;
}

bool AttachTerrainLightmapRecord(const std::vector<uint8_t>& existing_gzip,
                                 const std::vector<uint8_t>& donor_gzip,
                                 uint64_t key,
                                 std::vector<uint8_t>& out_gzip,
                                 std::string& error) {
    out_gzip.clear();
    error.clear();

    TerrainLightmap donor_lightmap;
    if (!DecodeTerrainLightmap(donor_gzip, key, donor_lightmap)) {
        error = "donor LMP terrain lightmap is invalid: " +
                donor_lightmap.error;
        return false;
    }

    std::vector<uint8_t> existing_raw;
    std::vector<uint8_t> donor_raw;
    if (!inflate_gzip(existing_gzip, existing_raw, error)) return false;
    if (!inflate_gzip(donor_gzip, donor_raw, error)) {
        error = "donor " + error;
        return false;
    }

    RawLmpLayout existing_layout;
    RawLmpLayout donor_layout;
    if (!parse_raw_layout(existing_raw, existing_layout, error)) return false;
    if (!parse_raw_layout(donor_raw, donor_layout, error)) {
        error = "donor " + error;
        return false;
    }

    const RawStaticRecord* donor_record = nullptr;
    for (const RawStaticRecord& record : donor_layout.static_records) {
        if (record.key != key) continue;
        if (donor_record) {
            error = "donor LMP contains duplicate terrain-lightmap keys";
            return false;
        }
        donor_record = &record;
    }
    if (!donor_record) {
        error = "donor LMP has no selected static-lightmap record";
        return false;
    }

    const RawStaticRecord* existing_record = nullptr;
    for (const RawStaticRecord& record : existing_layout.static_records) {
        if (record.key != key) continue;
        if (existing_record) {
            error = "existing LMP contains duplicate terrain-lightmap keys";
            return false;
        }
        existing_record = &record;
    }
    const auto donor_begin = donor_raw.begin() +
        static_cast<std::ptrdiff_t>(donor_record->begin);
    const auto donor_end = donor_raw.begin() +
        static_cast<std::ptrdiff_t>(donor_record->end);
    if (existing_record) {
        const size_t existing_size = existing_record->end -
                                     existing_record->begin;
        const size_t donor_size = donor_record->end - donor_record->begin;
        if (existing_size != donor_size ||
            !std::equal(donor_begin, donor_end,
                        existing_raw.begin() + static_cast<std::ptrdiff_t>(
                            existing_record->begin))) {
            error = "existing LMP has a conflicting record for the donor key";
            return false;
        }
        out_gzip = existing_gzip;
        return true;
    }
    if (existing_layout.static_count == std::numeric_limits<uint32_t>::max()) {
        error = "existing LMP static-lightmap count cannot be incremented";
        return false;
    }

    std::vector<uint8_t> merged;
    const size_t donor_size = donor_record->end - donor_record->begin;
    merged.reserve(existing_raw.size() + donor_size);
    merged.insert(merged.end(), existing_raw.begin(),
                  existing_raw.begin() + static_cast<std::ptrdiff_t>(
                      existing_layout.static_end));
    merged.insert(merged.end(), donor_begin, donor_end);
    merged.insert(merged.end(),
                  existing_raw.begin() + static_cast<std::ptrdiff_t>(
                      existing_layout.static_end),
                  existing_raw.end());
    write_be_u32(merged, existing_layout.static_count_offset,
                 existing_layout.static_count + 1u);

    RawLmpLayout check_layout;
    if (!parse_raw_layout(merged, check_layout, error)) {
        error = "merged " + error;
        return false;
    }
    if (!deflate_gzip(merged, out_gzip, error)) return false;
    TerrainLightmap check;
    if (!DecodeTerrainLightmap(out_gzip, key, check)) {
        error = "merged LMP validation failed: " + check.error;
        out_gzip.clear();
        return false;
    }
    return true;
}

}
