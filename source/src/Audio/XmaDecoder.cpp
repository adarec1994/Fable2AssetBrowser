#include "XmaDecoder.h"

#include "Xma2Codec/XmaParser.h"
#include "Xma2Codec/XmaSupport.h"
#include "Xma2Codec/XmaWmaProDecoder.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <fstream>

#if F2_HAVE_FFMPEG_REFERENCE
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}
#endif

namespace XmaDecoder {

namespace {

uint32_t rd_u32_le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
uint16_t rd_u16_le(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
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

}  // namespace

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

#if F2_HAVE_FFMPEG_REFERENCE
// Reference decode via the prebuilt FFmpeg .libs. Produces a ground-
// truth PCM stream that the in-tree port is compared against. Used
// only for debugging — the port is the runtime decoder.
static bool ffmpeg_reference_decode(const std::vector<uint8_t>& wav_bytes,
                                    std::vector<int16_t>& out_pcm,
                                    int& out_rate, int& out_channels) {
    out_pcm.clear();
    out_rate = 0;
    out_channels = 0;

    const int riff_off = locate_riff(wav_bytes);
    if (riff_off < 0) return false;
    const auto chunks = parse_chunks(wav_bytes, std::size_t(riff_off) + 12);
    const Chunk* fmt = nullptr;
    const Chunk* data = nullptr;
    const Chunk* xma2 = nullptr;
    for (auto& c : chunks) {
        if      (c.id == 0x20746d66) fmt = &c;
        else if (c.id == 0x61746164) data = &c;
        else if (c.id == 0x32414d58) xma2 = &c;
    }
    if (!fmt || !data) return false;

    auto rd_u32_be = [](const uint8_t* p) -> uint32_t {
        return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
               (uint32_t(p[2]) <<  8) |  uint32_t(p[3]);
    };
    const uint8_t* fmt_p = &wav_bytes[fmt->data_off];
    const uint32_t fmt_rate_le = rd_u32_le(fmt_p + 4);
    const uint32_t fmt_rate_be = rd_u32_be(fmt_p + 4);
    const bool xbox_endian = (fmt_rate_le < 8000u || fmt_rate_le > 200000u)
                          && (fmt_rate_be >= 8000u && fmt_rate_be <= 200000u);
    const uint16_t format_tag   = rd_u16_le(fmt_p + 0);
    const uint16_t fmt_channels = rd_u16_le(fmt_p + 2);
    const uint32_t fmt_rate     = xbox_endian ? fmt_rate_be : fmt_rate_le;

    AVCodecID codec_id = AV_CODEC_ID_NONE;
    if      (format_tag == 0x0166) codec_id = AV_CODEC_ID_XMA2;
    else if (format_tag == 0x0165) codec_id = AV_CODEC_ID_XMA1;
    else if (format_tag == 0xFFFE && fmt->data_size >= 40) {
        const uint16_t sub = rd_u16_le(fmt_p + 24);
        if      (sub == 0x0166) codec_id = AV_CODEC_ID_XMA2;
        else if (sub == 0x0165) codec_id = AV_CODEC_ID_XMA1;
    }
    if (codec_id == AV_CODEC_ID_NONE) return false;

    const AVCodec* codec = avcodec_find_decoder(codec_id);
    if (!codec) return false;
    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    if (!ctx) return false;

    ctx->sample_rate = int(fmt_rate);
    ctx->ch_layout.order = AV_CHANNEL_ORDER_UNSPEC;
    ctx->ch_layout.nb_channels = int(fmt_channels);
    av_channel_layout_default(&ctx->ch_layout, int(fmt_channels));
    ctx->block_align = 2048;

    std::array<uint8_t, 34> synth{};
    Xma::XmaSynthInput synth_in{};
    synth_in.channels       = int(fmt_channels);
    synth_in.sample_rate    = int(fmt_rate);
    synth_in.block_align    = 2048;
    synth_in.fmt_format_tag = format_tag;
    if (xma2) { synth_in.xma2_chunk = &wav_bytes[xma2->data_off]; synth_in.xma2_size = xma2->data_size; }
    synth_in.data_size = data->data_size;
    Xma::xma_synthesize_extradata(synth_in, synth);

    ctx->extradata = static_cast<uint8_t*>(av_mallocz(synth.size() + AV_INPUT_BUFFER_PADDING_SIZE));
    if (!ctx->extradata) { avcodec_free_context(&ctx); return false; }
    std::memcpy(ctx->extradata, synth.data(), synth.size());
    ctx->extradata_size = int(synth.size());

    if (avcodec_open2(ctx, codec, nullptr) < 0) { avcodec_free_context(&ctx); return false; }

    SwrContext* swr = nullptr;
    AVChannelLayout out_layout;
    av_channel_layout_default(&out_layout, ctx->ch_layout.nb_channels);
    if (swr_alloc_set_opts2(&swr,
                            &out_layout, AV_SAMPLE_FMT_S16, ctx->sample_rate,
                            &ctx->ch_layout, ctx->sample_fmt, ctx->sample_rate,
                            0, nullptr) < 0 || swr_init(swr) < 0) {
        if (swr) swr_free(&swr);
        avcodec_free_context(&ctx);
        return false;
    }

    AVPacket* pkt = av_packet_alloc();
    AVFrame*  frm = av_frame_alloc();
    const uint8_t* data_ptr = &wav_bytes[data->data_off];
    const std::size_t data_size = data->data_size;

    auto drain = [&]() {
        while (avcodec_receive_frame(ctx, frm) >= 0) {
            const int max_out = swr_get_out_samples(swr, frm->nb_samples);
            std::vector<int16_t> conv(std::size_t(max_out) * std::size_t(ctx->ch_layout.nb_channels));
            uint8_t* outp[1] = { reinterpret_cast<uint8_t*>(conv.data()) };
            const int got = swr_convert(swr, outp, max_out,
                                        const_cast<const uint8_t**>(frm->extended_data),
                                        frm->nb_samples);
            if (got > 0) {
                out_pcm.insert(out_pcm.end(),
                               conv.begin(),
                               conv.begin() + std::size_t(got) * std::size_t(ctx->ch_layout.nb_channels));
            }
            av_frame_unref(frm);
        }
    };

    const std::size_t kPktSize = 2048;
    for (std::size_t pos = 0; pos < data_size; pos += kPktSize) {
        const std::size_t this_sz = std::min(kPktSize, data_size - pos);
        if (av_new_packet(pkt, int(this_sz)) < 0) break;
        std::memcpy(pkt->data, data_ptr + pos, this_sz);
        if (avcodec_send_packet(ctx, pkt) >= 0) drain();
        av_packet_unref(pkt);
    }
    avcodec_send_packet(ctx, nullptr);
    drain();

    out_rate = ctx->sample_rate;
    out_channels = ctx->ch_layout.nb_channels;

    av_frame_free(&frm);
    av_packet_free(&pkt);
    swr_free(&swr);
    avcodec_free_context(&ctx);
    return !out_pcm.empty();
}
#endif  // F2_HAVE_FFMPEG_REFERENCE

// ---------------------------------------------------------------------
// Runtime decode path: in-tree native C++ port (src/Audio/Xma2Codec/).
// ---------------------------------------------------------------------
bool decode_xma_to_pcm(const std::vector<uint8_t>& wav_bytes,
                       std::vector<int16_t>& pcm_out,
                       int& sample_rate,
                       int& channels,
                       std::string* err_out) {
    pcm_out.clear();
    sample_rate = 0;
    channels = 0;

    Xma::log_reset();
    Xma::log_msg("decode_xma_to_pcm: input %zu bytes", wav_bytes.size());
    if (!wav_bytes.empty()) {
        Xma::log_hexdump("first 32 bytes", wav_bytes.data(),
                         std::min<std::size_t>(32, wav_bytes.size()));
    }

    const int riff_off = locate_riff(wav_bytes);
    if (riff_off < 0) {
        Xma::log_msg("ERROR: no RIFF/WAVE header found");
        if (err_out) *err_out = "no RIFF/WAVE header";
        return false;
    }
    Xma::log_msg("RIFF found at offset %d", riff_off);
    const auto chunks = parse_chunks(wav_bytes, (size_t)riff_off + 12);

    const Chunk* fmt = nullptr;
    const Chunk* data = nullptr;
    const Chunk* xma2 = nullptr;
    for (auto& c : chunks) {
        if      (c.id == 0x20746d66) fmt = &c;
        else if (c.id == 0x61746164) data = &c;
        else if (c.id == 0x32414d58) xma2 = &c;
    }
    if (!fmt || !data) {
        Xma::log_msg("ERROR: missing fmt or data chunk");
        if (err_out) *err_out = "missing fmt or data chunk";
        return false;
    }
    if (fmt->data_size < 16) {
        if (err_out) *err_out = "fmt chunk too small";
        return false;
    }

    auto rd_u32_be = [](const uint8_t* p) -> uint32_t {
        return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
               ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
    };

    const uint8_t* fmt_p = &wav_bytes[fmt->data_off];
    const uint32_t fmt_rate_le = rd_u32_le(fmt_p + 4);
    const uint32_t fmt_rate_be = rd_u32_be(fmt_p + 4);
    const bool xbox_endian = (fmt_rate_le < 8000u || fmt_rate_le > 200000u)
                          && (fmt_rate_be >= 8000u && fmt_rate_be <= 200000u);
    const uint16_t format_tag   = rd_u16_le(fmt_p + 0);
    const uint16_t fmt_channels = rd_u16_le(fmt_p + 2);
    const uint32_t fmt_rate     = xbox_endian ? fmt_rate_be : fmt_rate_le;

    Xma::log_msg("fmt: tag=0x%04X channels=%u rate=%u (xbox_endian=%d) fmt_size=%zu",
                 format_tag, fmt_channels, fmt_rate, int(xbox_endian), fmt->data_size);
    Xma::log_msg("chunks found: fmt=yes data=yes(size=%zu) xma2=%s",
                 data->data_size, xma2 ? "yes" : "no");

    Xma::CodecId codec_id = Xma::CodecId::None;
    if      (format_tag == 0x0166) codec_id = Xma::CodecId::Xma2;
    else if (format_tag == 0x0165) codec_id = Xma::CodecId::Xma1;
    else if (format_tag == 0xFFFE && fmt->data_size >= 40) {
        const uint16_t sub = rd_u16_le(fmt_p + 24);
        if      (sub == 0x0166) codec_id = Xma::CodecId::Xma2;
        else if (sub == 0x0165) codec_id = Xma::CodecId::Xma1;
    }
    if (codec_id == Xma::CodecId::None) {
        Xma::log_msg("ERROR: format tag 0x%04X is not XMA1/XMA2", format_tag);
        if (err_out) {
            char b[96]; std::snprintf(b, sizeof(b), "format tag 0x%04X is not XMA1/XMA2", format_tag);
            *err_out = b;
        }
        return false;
    }
    Xma::log_msg("codec_id resolved: %s",
                 codec_id == Xma::CodecId::Xma2 ? "XMA2" :
                 codec_id == Xma::CodecId::Xma1 ? "XMA1" : "WmaPro");

    Xma::CodecContext ctx;
    ctx.codec_id    = codec_id;
    ctx.sample_rate = int(fmt_rate);
    ctx.block_align = 2048;
    Xma::channel_layout_default(ctx.ch_layout, int(fmt_channels));

    std::array<uint8_t, 34> synth{};
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
    if (!Xma::xma_synthesize_extradata(synth_in, synth)) {
        Xma::log_msg("ERROR: extradata synthesis failed");
        if (err_out) *err_out = "failed to synthesize extradata";
        return false;
    }
    Xma::log_hexdump("synth extradata (34B)", synth.data(), 34);
    ctx.extradata.assign(synth.begin(), synth.end());
    ctx.extradata.resize(ctx.extradata.size() + 64, 0);
    ctx.extradata_size = 34;

    Xma::WmaProDecoder dec;
    const int init_rc = dec.init(ctx);
    Xma::log_msg("WmaProDecoder::init -> %d (ctx.sample_rate=%d ch_layout.nb_channels=%d)",
                 init_rc, ctx.sample_rate, ctx.ch_layout.nb_channels);
    if (init_rc < 0) {
        if (err_out) *err_out = "Xma::WmaProDecoder::init failed";
        return false;
    }

    const uint8_t* data_ptr = &wav_bytes[data->data_off];
    const size_t   data_size = data->data_size;

    auto emit_frame = [&](Xma::Frame& f) {
        const int n = f.nb_samples;
        const int c = f.nb_channels;
        if (n <= 0 || c <= 0) return;
        const std::size_t base = pcm_out.size();
        pcm_out.resize(base + std::size_t(n) * std::size_t(c));
        std::array<const float*, 16> planes_ptr{};
        for (int ci = 0; ci < c && ci < int(planes_ptr.size()); ++ci)
            planes_ptr[ci] = f.planes[ci].data();
        Xma::fltp_to_s16_interleaved(planes_ptr.data(), &pcm_out[base], n, c);
        if (!sample_rate) sample_rate = f.sample_rate;
        if (!channels)    channels    = c;
    };

    constexpr size_t kPktSize = 2048;
    Xma::Frame frame;
    int pkt_count = 0;
    int got_count = 0;
    for (size_t pos = 0; pos < data_size; pos += kPktSize) {
        // Slide pkt.data forward by `rc` bytes between inner calls so
        // the decoder's "continue this packet" branch sees the right
        // bit offset (matches FFmpeg's avpkt-advance convention).
        const uint8_t* base = data_ptr + pos;
        int           pkt_remaining = int(std::min(kPktSize, data_size - pos));
        const int     pkt_total     = pkt_remaining;
        ++pkt_count;
        int inner = 0;
        const bool log_pkt = (pkt_count <= 4 || (pkt_count % 256) == 0);
        while (true) {
            Xma::Packet pkt{};
            pkt.data = base + (pkt_total - pkt_remaining);
            pkt.size = pkt_remaining;
            bool got = false;
            const int rc = dec.decode_packet(pkt, frame, got);
            ++inner;
            if (log_pkt && inner <= 4) {
                Xma::log_msg("pkt[%d.%d] off=0x%zx (sub=%d) size=%d rc=%d got=%d nb_samples=%d done=%d loss=%d",
                             pkt_count - 1, inner - 1, pos,
                             pkt_total - pkt_remaining, pkt.size, rc, int(got),
                             got ? frame.nb_samples : 0,
                             int(dec.packet_done()), int(dec.packet_loss()));
            }
            if (got) { emit_frame(frame); ++got_count; }
            if (rc < 0) break;
            if (dec.packet_done() || dec.packet_loss()) break;
            if (rc <= 0 || rc > pkt_remaining) break;
            pkt_remaining -= rc;
            if (inner > 32) break;
        }
    }
    // Drain.
    Xma::Packet empty{};
    bool got = false;
    dec.decode_packet(empty, frame, got);
    if (got) { emit_frame(frame); ++got_count; }
    dec.flush(frame, got);
    if (got) { emit_frame(frame); ++got_count; }
    Xma::log_msg("decode done: %d packets in, %d frames out, %zu PCM samples (channels=%d rate=%d)",
                 pkt_count, got_count, pcm_out.size(), channels, sample_rate);

#if F2_HAVE_FFMPEG_REFERENCE
    // Reference decode: run FFmpeg on the same input, then compare
    // sample-by-sample against our port. Writes both PCMs as raw S16LE
    // next to the exe so the user can A/B listen.
    {
        std::vector<int16_t> ref_pcm;
        int ref_rate = 0, ref_ch = 0;
        const bool ref_ok = ffmpeg_reference_decode(wav_bytes, ref_pcm, ref_rate, ref_ch);
        if (ref_ok) {
            int16_t rmin = ref_pcm[0], rmax = ref_pcm[0];
            int rabs = 0;
            for (auto v : ref_pcm) {
                if (v < rmin) rmin = v;
                if (v > rmax) rmax = v;
                const int a = v < 0 ? -int(v) : int(v);
                if (a > rabs) rabs = a;
            }
            Xma::log_msg("REFERENCE: ffmpeg decoded %zu samples (ch=%d rate=%d) min=%d max=%d abs_max=%d",
                         ref_pcm.size(), ref_ch, ref_rate, int(rmin), int(rmax), rabs);
            Xma::log_msg("REFERENCE: port produced %zu samples; ratio = %.2f%%",
                         pcm_out.size(),
                         ref_pcm.empty() ? 0.0
                                         : 100.0 * double(pcm_out.size()) / double(ref_pcm.size()));

            const std::size_t common = std::min(pcm_out.size(), ref_pcm.size());
            std::size_t diverge_at = common;
            std::size_t exact_matches = 0;
            std::size_t close_matches = 0;
            long long sum_abs_err = 0;
            long long peak_err = 0;
            for (std::size_t i = 0; i < common; ++i) {
                const int d = int(pcm_out[i]) - int(ref_pcm[i]);
                const long long ad = d < 0 ? -(long long)d : (long long)d;
                if (ad == 0) ++exact_matches;
                if (ad <= 64) ++close_matches;
                sum_abs_err += ad;
                if (ad > peak_err) peak_err = ad;
                if (diverge_at == common && ad > 256) diverge_at = i;
            }
            const double mae = common ? double(sum_abs_err) / double(common) : 0.0;
            Xma::log_msg("REFERENCE: exact matches=%zu (%.2f%%), close(<=64)=%zu (%.2f%%) over common=%zu",
                         exact_matches, 100.0 * double(exact_matches) / double(common ? common : 1),
                         close_matches, 100.0 * double(close_matches) / double(common ? common : 1),
                         common);
            Xma::log_msg("REFERENCE: mean-abs-error=%.1f peak-err=%lld first significant divergence=sample %zu",
                         mae, peak_err, diverge_at);

            const int samples_per_frame = 512;
            const int total_ch = ref_ch > 0 ? ref_ch : 1;
            const std::size_t pair_idx = diverge_at / total_ch;
            const std::size_t in_pair  = diverge_at % total_ch;
            const std::size_t frame_idx = pair_idx / samples_per_frame;
            const std::size_t in_frame  = pair_idx % samples_per_frame;
            const int subframe_idx = int(in_frame / 128);
            Xma::log_msg("REFERENCE: divergence at frame=%zu subframe~%d (sample-in-frame=%zu, ch=%zu)",
                         frame_idx, subframe_idx, in_frame, in_pair);

            if (diverge_at < common) {
                char buf[512];
                int off = std::snprintf(buf, sizeof(buf), "REFERENCE: around divergence [port|ref]: ");
                const std::ptrdiff_t lo = std::max<std::ptrdiff_t>(0, (std::ptrdiff_t)diverge_at - 5);
                const std::ptrdiff_t hi = std::min<std::ptrdiff_t>((std::ptrdiff_t)common, (std::ptrdiff_t)diverge_at + 8);
                for (std::ptrdiff_t i = lo; i < hi && off < (int)sizeof(buf) - 30; ++i) {
                    off += std::snprintf(buf + off, sizeof(buf) - off,
                                         "[%d|%d]%s ",
                                         int(pcm_out[i]), int(ref_pcm[i]),
                                         i == (std::ptrdiff_t)diverge_at ? "*" : "");
                }
                Xma::log_msg("%s", buf);
            }

            try {
                std::ofstream f("xma_reference.s16.raw", std::ios::binary | std::ios::trunc);
                if (f) {
                    f.write(reinterpret_cast<const char*>(ref_pcm.data()),
                            std::streamsize(ref_pcm.size() * sizeof(int16_t)));
                }
                Xma::log_msg("REFERENCE: wrote xma_reference.s16.raw (%zu bytes, ch=%d rate=%d)",
                             ref_pcm.size() * sizeof(int16_t), ref_ch, ref_rate);
                std::ofstream fp("xma_port.s16.raw", std::ios::binary | std::ios::trunc);
                if (fp) {
                    fp.write(reinterpret_cast<const char*>(pcm_out.data()),
                             std::streamsize(pcm_out.size() * sizeof(int16_t)));
                }
                Xma::log_msg("REFERENCE: wrote xma_port.s16.raw (%zu bytes, ch=%d rate=%d)",
                             pcm_out.size() * sizeof(int16_t), channels, sample_rate);
            } catch (...) {
                Xma::log_msg("REFERENCE: failed to write raw PCM files");
            }
        } else {
            Xma::log_msg("REFERENCE: ffmpeg decode failed (no reference data this run)");
        }
    }
#endif

    if (!pcm_out.empty()) {
        int16_t pcm_min = pcm_out[0], pcm_max = pcm_out[0];
        int abs_max = 0;
        int nonzero = 0;
        for (auto v : pcm_out) {
            if (v < pcm_min) pcm_min = v;
            if (v > pcm_max) pcm_max = v;
            const int a = v < 0 ? -int(v) : int(v);
            if (a > abs_max) abs_max = a;
            if (v != 0) ++nonzero;
        }
        Xma::log_msg("pcm stats: min=%d max=%d abs_max=%d nonzero=%d / %zu (%.1f%%)",
                     int(pcm_min), int(pcm_max), abs_max, nonzero, pcm_out.size(),
                     100.0 * double(nonzero) / double(pcm_out.size()));
    }

    if (!channels)    channels    = ctx.ch_layout.nb_channels;
    if (!sample_rate) sample_rate = ctx.sample_rate;

    if (pcm_out.empty()) {
        if (err_out) *err_out = "decoded zero samples";
        return false;
    }
    return true;
}

}  // namespace XmaDecoder
