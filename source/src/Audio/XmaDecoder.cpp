#include "XmaDecoder.h"

#include "Xma2Codec/XmaParser.h"
#include "Xma2Codec/XmaSupport.h"
#include "Xma2Codec/XmaWmaProDecoder.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <memory>

namespace XmaDecoder {

namespace {

uint32_t rd_u32_le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
uint16_t rd_u16_le(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
uint32_t rd_u32_be(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}

bool write_pcm_wav_file(const std::string& out_path,
                        const std::vector<int16_t>& pcm,
                        int sample_rate,
                        int channels) {
    if (pcm.empty() || sample_rate <= 0 || channels <= 0) return false;
    std::ofstream f(out_path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    auto put_u32 = [&](uint32_t v) {
        uint8_t b[4] = { (uint8_t)v, (uint8_t)(v>>8), (uint8_t)(v>>16), (uint8_t)(v>>24) };
        f.write((const char*)b, 4);
    };
    auto put_u16 = [&](uint16_t v) {
        uint8_t b[2] = { (uint8_t)v, (uint8_t)(v>>8) };
        f.write((const char*)b, 2);
    };
    const uint32_t data_bytes = (uint32_t)(pcm.size() * sizeof(int16_t));
    const uint32_t byterate   = (uint32_t)sample_rate * (uint32_t)channels * 2u;
    const uint16_t blockalign = (uint16_t)(channels * 2);
    f.write("RIFF", 4);
    put_u32(36u + data_bytes);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    put_u32(16);
    put_u16(1);
    put_u16((uint16_t)channels);
    put_u32((uint32_t)sample_rate);
    put_u32(byterate);
    put_u16(blockalign);
    put_u16(16);
    f.write("data", 4);
    put_u32(data_bytes);
    f.write((const char*)pcm.data(), (std::streamsize)data_bytes);
    return f.good();
}

int locate_riff(const std::vector<uint8_t>& buf) {
    if (buf.size() < 12) return -1;
    if (std::memcmp(&buf[0], "RIFF", 4) == 0 && std::memcmp(&buf[8], "WAVE", 4) == 0) return 0;
    if (buf.size() >= 16 && buf[0] == 'x' && buf[1] == 'm' && buf[2] == 'a' && buf[3] == 0
        && std::memcmp(&buf[4], "RIFF", 4) == 0
        && std::memcmp(&buf[12], "WAVE", 4) == 0) {
        return 4;
    }
    return -1;
}

struct Chunk {
    uint32_t id;
    size_t data_off;
    size_t data_size;
};

std::vector<Chunk> parse_chunks(const std::vector<uint8_t>& buf, size_t start) {
    std::vector<Chunk> out;
    size_t pos = start;
    while (pos + 8 <= buf.size()) {
        uint32_t id = rd_u32_le(&buf[pos]);
        uint32_t sz = rd_u32_le(&buf[pos + 4]);
        size_t data_off = pos + 8;
        size_t data_end = data_off + sz;
        if (data_off > buf.size()) break;
        if (data_end > buf.size()) data_end = buf.size();
        out.push_back({id, data_off, data_end - data_off});
        if ((uint64_t)pos + 8 + (uint64_t)sz + (uint64_t)(sz & 1u) <= (uint64_t)buf.size()) {
            pos = data_off + sz + (sz & 1u);
        } else {
            break;
        }
    }
    return out;
}

struct ParsedXma {
    int          sample_rate = 0;
    int          channels    = 0;
    uint16_t     format_tag  = 0;
    Xma::CodecId codec_id    = Xma::CodecId::None;

    size_t   data_off  = 0;
    size_t   data_size = 0;

    uint64_t total_samples_per_channel = 0;

    std::array<uint8_t, 34> extradata{};
};

bool parse_xma_source(const std::vector<uint8_t>& wav_bytes,
                      ParsedXma& out,
                      std::string* err) {
    const int riff_off = locate_riff(wav_bytes);
    if (riff_off < 0) {
        if (err) *err = "no RIFF/WAVE header";
        return false;
    }
    const auto chunks = parse_chunks(wav_bytes, (size_t)riff_off + 12);

    const Chunk* fmt  = nullptr;
    const Chunk* data = nullptr;
    const Chunk* xma2 = nullptr;
    for (auto& c : chunks) {
        if      (c.id == 0x20746d66) fmt  = &c;
        else if (c.id == 0x61746164) data = &c;
        else if (c.id == 0x32414d58) xma2 = &c;
    }
    if (!fmt || !data) {
        if (err) *err = "missing fmt or data chunk";
        return false;
    }
    if (fmt->data_size < 16) {
        if (err) *err = "fmt chunk too small";
        return false;
    }

    const uint8_t* fmt_p = &wav_bytes[fmt->data_off];
    const uint32_t fmt_rate_le = rd_u32_le(fmt_p + 4);
    const uint32_t fmt_rate_be = rd_u32_be(fmt_p + 4);
    const bool xbox_endian = (fmt_rate_le < 8000u || fmt_rate_le > 200000u)
                          && (fmt_rate_be >= 8000u && fmt_rate_be <= 200000u);
    const uint16_t format_tag   = rd_u16_le(fmt_p + 0);
    const uint16_t fmt_channels = rd_u16_le(fmt_p + 2);
    const uint32_t fmt_rate     = xbox_endian ? fmt_rate_be : fmt_rate_le;

    Xma::CodecId codec_id = Xma::CodecId::None;
    if      (format_tag == 0x0166) codec_id = Xma::CodecId::Xma2;
    else if (format_tag == 0x0165) codec_id = Xma::CodecId::Xma1;
    else if (format_tag == 0xFFFE && fmt->data_size >= 40) {
        const uint16_t sub = rd_u16_le(fmt_p + 24);
        if      (sub == 0x0166) codec_id = Xma::CodecId::Xma2;
        else if (sub == 0x0165) codec_id = Xma::CodecId::Xma1;
    }
    if (codec_id == Xma::CodecId::None) {
        if (err) {
            char b[96];
            std::snprintf(b, sizeof(b), "format tag 0x%04X is not XMA1/XMA2", format_tag);
            *err = b;
        }
        return false;
    }

    out.sample_rate = int(fmt_rate);
    out.channels    = int(fmt_channels);
    out.format_tag  = format_tag;
    out.codec_id    = codec_id;
    out.data_off    = data->data_off;
    out.data_size   = data->data_size;

    out.total_samples_per_channel = 0;
    if (fmt->data_size >= 12 && fmt_rate > 0) {
        const uint32_t avg_bps = xbox_endian
            ? rd_u32_be(fmt_p + 8)
            : rd_u32_le(fmt_p + 8);
        if (avg_bps > 100 && avg_bps < 10000000) {
            const double duration_sec = double(data->data_size) / double(avg_bps);
            const double frames       = duration_sec * double(fmt_rate);
            if (frames > 0.0 && frames < 1e12) {
                out.total_samples_per_channel = uint64_t(frames);
            }
        }
    }
    if (out.total_samples_per_channel == 0 && xma2 && xma2->data_size >= 20 && fmt_rate > 0) {
        const uint8_t* xp = &wav_bytes[xma2->data_off];
        const uint64_t hi = uint64_t(fmt_rate) * 60u * 60u * 24u;
        const uint64_t lo = uint64_t(fmt_rate) / 8u;
        for (size_t off : {size_t(12), size_t(16)}) {
            if (xma2->data_size < off + 4) continue;
            const uint64_t s = rd_u32_be(xp + off);
            if (s >= lo && s <= hi) {
                out.total_samples_per_channel = s;
                break;
            }
        }
    }

    Xma::XmaSynthInput synth_in{};
    synth_in.channels       = int(fmt_channels);
    synth_in.sample_rate    = int(fmt_rate);
    synth_in.block_align    = 2048;
    synth_in.fmt_format_tag = format_tag;
    if (xma2) {
        synth_in.xma2_chunk = &wav_bytes[xma2->data_off];
        synth_in.xma2_size  = xma2->data_size;
    }
    synth_in.data_size = data->data_size;
    if (!Xma::xma_synthesize_extradata(synth_in, out.extradata)) {
        if (err) *err = "failed to synthesize extradata";
        return false;
    }
    return true;
}

void emit_frame_to_pcm(Xma::Frame& f, std::vector<int16_t>& pcm_out) {
    const int n = f.nb_samples;
    const int c = f.nb_channels;
    if (n <= 0 || c <= 0) return;
    const size_t base = pcm_out.size();
    pcm_out.resize(base + size_t(n) * size_t(c));
    std::array<const float*, 16> planes_ptr{};
    for (int ci = 0; ci < c && ci < int(planes_ptr.size()); ++ci) {
        planes_ptr[ci] = f.planes[ci].data();
    }
    Xma::fltp_to_s16_interleaved(planes_ptr.data(), &pcm_out[base], n, c);
}

}

struct StreamDecoder::Impl {
    std::vector<uint8_t> src;
    ParsedXma            parsed;
    Xma::CodecContext    ctx;
    Xma::WmaProDecoder   dec;

    size_t pos = 0;

    enum class Stage { Decoding, EmptyPacket, Flushing, Done };
    Stage stage = Stage::Decoding;
};

StreamDecoder::StreamDecoder() : impl_(std::make_unique<Impl>()) {}
StreamDecoder::~StreamDecoder() = default;

bool StreamDecoder::open(const std::vector<uint8_t>& bytes, std::string* err) {
    auto& I = *impl_;
    I.src   = bytes;
    I.pos   = 0;
    I.stage = Impl::Stage::Decoding;

    if (!parse_xma_source(I.src, I.parsed, err)) return false;

    I.ctx.codec_id    = I.parsed.codec_id;
    I.ctx.sample_rate = I.parsed.sample_rate;
    I.ctx.block_align = 2048;
    Xma::channel_layout_default(I.ctx.ch_layout, I.parsed.channels);

    I.ctx.extradata.assign(I.parsed.extradata.begin(), I.parsed.extradata.end());
    I.ctx.extradata.resize(I.ctx.extradata.size() + 64, 0);
    I.ctx.extradata_size = 34;

    const int init_rc = I.dec.init(I.ctx);
    if (init_rc < 0) {
        if (err) *err = "Xma::WmaProDecoder::init failed";
        return false;
    }
    return true;
}

int      StreamDecoder::sample_rate() const { return impl_->parsed.sample_rate; }
int      StreamDecoder::channels()    const { return impl_->parsed.channels; }
uint64_t StreamDecoder::total_frames() const { return impl_->parsed.total_samples_per_channel; }

bool StreamDecoder::decode_chunk(std::vector<int16_t>& out) {
    auto& I = *impl_;
    if (I.stage == Impl::Stage::Done) return false;

    constexpr size_t kPktSize = 2048;
    Xma::Frame frame;

    if (I.stage == Impl::Stage::Decoding) {
        if (I.pos < I.parsed.data_size) {
            const uint8_t* data_ptr = &I.src[I.parsed.data_off];
            const uint8_t* base     = data_ptr + I.pos;
            int       pkt_remaining = int(std::min(kPktSize, I.parsed.data_size - I.pos));
            const int pkt_total     = pkt_remaining;
            int inner = 0;
            while (true) {
                Xma::Packet pkt{};
                pkt.data = base + (pkt_total - pkt_remaining);
                pkt.size = pkt_remaining;
                bool got = false;
                const int rc = I.dec.decode_packet(pkt, frame, got);
                ++inner;
                if (got) emit_frame_to_pcm(frame, out);
                if (rc < 0) break;
                if (I.dec.packet_done() || I.dec.packet_loss()) break;
                if (rc <= 0 || rc > pkt_remaining) break;
                pkt_remaining -= rc;
                if (inner > 32) break;
            }
            I.pos += kPktSize;
            return true;
        }
        I.stage = Impl::Stage::EmptyPacket;
    }

    if (I.stage == Impl::Stage::EmptyPacket) {
        Xma::Packet empty{};
        bool got = false;
        I.dec.decode_packet(empty, frame, got);
        if (got) emit_frame_to_pcm(frame, out);
        I.stage = Impl::Stage::Flushing;
        return true;
    }

    if (I.stage == Impl::Stage::Flushing) {
        bool got = false;
        I.dec.flush(frame, got);
        if (got) emit_frame_to_pcm(frame, out);
        I.stage = Impl::Stage::Done;
        return false;
    }

    return false;
}

bool decode_xma_wav_file_to_pcm_wav(const std::vector<uint8_t>& src_bytes,
                                    const std::string& out_path,
                                    std::string* err_out) {
    std::vector<int16_t> pcm;
    int rate = 0, ch = 0;
    if (!decode_xma_to_pcm(src_bytes, pcm, rate, ch, err_out)) return false;
    if (!write_pcm_wav_file(out_path, pcm, rate, ch)) {
        if (err_out) *err_out = "could not write PCM .wav";
        return false;
    }
    return true;
}

bool decode_xma_to_pcm(const std::vector<uint8_t>& wav_bytes,
                       std::vector<int16_t>& pcm_out,
                       int& sample_rate,
                       int& channels,
                       std::string* err_out) {
    pcm_out.clear();
    sample_rate = 0;
    channels    = 0;

    StreamDecoder sd;
    if (!sd.open(wav_bytes, err_out)) return false;

    sample_rate = sd.sample_rate();
    channels    = sd.channels();

    const uint64_t hinted = sd.total_frames();
    const std::size_t pcm_hard_cap = std::size_t(48000) * 32 * std::size_t(60 * 60);
    const std::size_t reserve_cap  = std::size_t(1) << 28;

    std::size_t reserve_samples = 0;
    if (hinted > 0 && channels > 0) {
        reserve_samples = std::size_t(std::min<uint64_t>(hinted * uint64_t(channels),
                                                        uint64_t(reserve_cap)));
    } else {
        const std::size_t ratio = std::size_t(channels > 0 ? channels : 1) * 4u;
        reserve_samples = wav_bytes.size() * ratio;
        if (reserve_samples > reserve_cap) reserve_samples = reserve_cap;
    }
    try {
        pcm_out.reserve(reserve_samples);
    } catch (const std::bad_alloc&) {
    }

    while (sd.decode_chunk(pcm_out)) {
        if (pcm_out.size() > pcm_hard_cap) break;
    }

    if (pcm_out.empty()) {
        if (err_out) *err_out = "decoded zero samples";
        return false;
    }
    return true;
}

}
