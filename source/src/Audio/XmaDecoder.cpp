#include "XmaDecoder.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <fstream>

namespace XmaDecoder {

namespace {
uint32_t rd_u32_le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
uint16_t rd_u16_le(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
}

namespace {
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

#if F2_HAVE_FFMPEG

extern "C" {

#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

namespace {

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

}

bool decode_xma_to_pcm(const std::vector<uint8_t>& wav_bytes,
                      std::vector<int16_t>& pcm_out,
                      int& sample_rate,
                      int& channels,
                      std::string* err_out) {
    pcm_out.clear();
    sample_rate = 0;
    channels = 0;

    int riff_off = locate_riff(wav_bytes);
    if (riff_off < 0) {
        if (err_out) *err_out = "no RIFF/WAVE header";
        return false;
    }

    auto chunks = parse_chunks(wav_bytes, (size_t)riff_off + 12);

    const Chunk* fmt = nullptr;
    const Chunk* data = nullptr;
    const Chunk* xma2 = nullptr;
    for (auto& c : chunks) {
        if (c.id == 0x20746d66) fmt = &c;
        else if (c.id == 0x61746164) data = &c;
        else if (c.id == 0x32414d58) xma2 = &c;
    }
    if (!fmt || !data) {
        if (err_out) *err_out = "missing fmt or data chunk";
        return false;
    }
    if (fmt->data_size < 16) {
        if (err_out) *err_out = "fmt chunk too small";
        return false;
    }

    auto rd_u16_be = [](const uint8_t* p) -> uint16_t {
        return ((uint16_t)p[0] << 8) | (uint16_t)p[1];
    };
    auto rd_u32_be = [](const uint8_t* p) -> uint32_t {
        return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
    };

    const uint8_t* fmt_p = &wav_bytes[fmt->data_off];

    uint32_t fmt_rate_le   = rd_u32_le(fmt_p + 4);
    uint32_t fmt_rate_be   = rd_u32_be(fmt_p + 4);
    bool xbox_endian = (fmt_rate_le < 8000u || fmt_rate_le > 200000u)
                       && (fmt_rate_be >= 8000u && fmt_rate_be <= 200000u);

    uint16_t format_tag    = rd_u16_le(fmt_p + 0);
    uint16_t fmt_channels  = rd_u16_le(fmt_p + 2);
    uint32_t fmt_rate      = xbox_endian ? fmt_rate_be : fmt_rate_le;
    uint32_t fmt_byterate  = xbox_endian ? rd_u32_be(fmt_p + 8) : rd_u32_le(fmt_p + 8);
    uint16_t block_align   = xbox_endian ? rd_u16_be(fmt_p + 12) : rd_u16_le(fmt_p + 12);
    uint16_t bits          = rd_u16_le(fmt_p + 14);
    (void)bits;
    (void)fmt_byterate;

    AVCodecID codec_id = AV_CODEC_ID_NONE;
    if (format_tag == 0x0166) codec_id = AV_CODEC_ID_XMA2;
    else if (format_tag == 0x0165) codec_id = AV_CODEC_ID_XMA1;
    else if (format_tag == 0xFFFE && fmt->data_size >= 40) {

        uint16_t sub = rd_u16_le(fmt_p + 24);
        if (sub == 0x0166) codec_id = AV_CODEC_ID_XMA2;
        else if (sub == 0x0165) codec_id = AV_CODEC_ID_XMA1;
    }
    if (codec_id == AV_CODEC_ID_NONE) {
        if (err_out) {
            char b[96]; std::snprintf(b, sizeof(b), "format tag 0x%04X is not XMA1/XMA2", format_tag);
            *err_out = b;
        }
        return false;
    }

    const AVCodec* codec = avcodec_find_decoder(codec_id);
    if (!codec) {
        if (err_out) *err_out = "FFmpeg has no XMA1/XMA2 decoder in this build";
        return false;
    }

    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    if (!ctx) {
        if (err_out) *err_out = "avcodec_alloc_context3 failed";
        return false;
    }

    ctx->sample_rate = (int)fmt_rate;
    ctx->ch_layout.order = AV_CHANNEL_ORDER_UNSPEC;
    ctx->ch_layout.nb_channels = (int)fmt_channels;
    av_channel_layout_default(&ctx->ch_layout, (int)fmt_channels);

    ctx->block_align = 2048;

    auto setup_extradata = [&](const uint8_t* src, int n) {
        if (ctx->extradata) { av_freep(&ctx->extradata); ctx->extradata_size = 0; }
        ctx->extradata = (uint8_t*)av_mallocz((size_t)n + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!ctx->extradata) return;
        std::memcpy(ctx->extradata, src, (size_t)n);
        ctx->extradata_size = n;
    };

    int tail_size = (fmt->data_size > 18) ? (int)(fmt->data_size - 18) : 0;
    if (tail_size > 0) {
        setup_extradata(fmt_p + 18, tail_size);
    }

    uint16_t synth_num_streams = (uint16_t)((fmt_channels + 1) / 2);
    if (synth_num_streams == 0) synth_num_streams = 1;
    uint32_t synth_channel_mask = 0;
    switch (fmt_channels) {
        case 1: synth_channel_mask = 0x00000004; break;
        case 2: synth_channel_mask = 0x00000003; break;
        case 4: synth_channel_mask = 0x00000033; break;
        case 6: synth_channel_mask = 0x0000003F; break;
        case 8: synth_channel_mask = 0x000000FF; break;
        default: synth_channel_mask = (1u << fmt_channels) - 1; break;
    }
    uint32_t synth_samples_encoded = 0;
    uint32_t synth_bytes_per_block = 0x10000;
    uint32_t synth_block_count = 0;

    if (xma2 && xma2->data_size >= 36) {

        const uint8_t* xp = &wav_bytes[xma2->data_off];
        uint8_t  num_streams_ch = xp[1];
        if (num_streams_ch >= 1 && num_streams_ch <= 8) {
            synth_num_streams = (uint16_t)num_streams_ch;
        }
        synth_samples_encoded = rd_u32_be(xp + 16);

        if (xma2->data_size >= 28) {
            uint32_t maybe_bpb = rd_u32_be(xp + 24);
            if (maybe_bpb >= 0x800 && maybe_bpb <= 0x100000) {
                synth_bytes_per_block = maybe_bpb;
            }
        }
    }
    if (synth_block_count == 0 && synth_bytes_per_block > 0) {
        synth_block_count = (uint32_t)((data->data_size + synth_bytes_per_block - 1)
                                       / synth_bytes_per_block);
        if (synth_block_count == 0) synth_block_count = 1;
    }
    if (synth_samples_encoded == 0) {

        synth_samples_encoded = (uint32_t)((uint64_t)synth_block_count * 512u * 32u);
        if (synth_samples_encoded == 0) synth_samples_encoded = 0x7FFFFFFF;
    }

    {
        uint8_t synth[34] = {0};
        auto wrU16 = [&](int o, uint16_t v) { synth[o] = (uint8_t)v; synth[o+1] = (uint8_t)(v>>8); };
        auto wrU32 = [&](int o, uint32_t v) { synth[o]=(uint8_t)v; synth[o+1]=(uint8_t)(v>>8); synth[o+2]=(uint8_t)(v>>16); synth[o+3]=(uint8_t)(v>>24); };
        wrU16(0,  synth_num_streams);
        wrU32(2,  synth_channel_mask);
        wrU32(6,  synth_samples_encoded);
        wrU32(10, synth_bytes_per_block);
        wrU32(14, 0);
        wrU32(18, synth_samples_encoded);
        wrU32(22, 0);
        wrU32(26, 0);
        synth[30] = 0;
        synth[31] = 4;
        wrU16(32, (uint16_t)synth_block_count);
        setup_extradata(synth, 34);
    }

    int open_rc = avcodec_open2(ctx, codec, nullptr);

    if (open_rc < 0) {
        char b[160];
        std::snprintf(b, sizeof(b),
            "avcodec_open2 rc=%d (fmt=0x%04X ch=%u rate=%u blkalign=%u extra=%d).",
            open_rc, format_tag, fmt_channels, fmt_rate, block_align,
            ctx->extradata_size);
        avcodec_free_context(&ctx);
        if (err_out) *err_out = b;
        return false;
    }

    SwrContext* swr = nullptr;
    AVChannelLayout out_layout;
    av_channel_layout_default(&out_layout, ctx->ch_layout.nb_channels);
    if (swr_alloc_set_opts2(&swr,
                            &out_layout, AV_SAMPLE_FMT_S16, ctx->sample_rate,
                            &ctx->ch_layout, ctx->sample_fmt, ctx->sample_rate,
                            0, nullptr) < 0 || swr_init(swr) < 0) {
        if (swr) swr_free(&swr);
        avcodec_free_context(&ctx);
        if (err_out) *err_out = "swresample setup failed";
        return false;
    }

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frm = av_frame_alloc();

    const uint8_t* data_ptr = &wav_bytes[data->data_off];
    size_t        data_size = data->data_size;

    auto feed_frame = [&]() {
        int err = 0;
        while ((err = avcodec_receive_frame(ctx, frm)) >= 0) {
            int max_out = swr_get_out_samples(swr, frm->nb_samples);
            std::vector<int16_t> conv(max_out * ctx->ch_layout.nb_channels);
            uint8_t* outp[1] = { (uint8_t*)conv.data() };
            int got = swr_convert(swr, outp, max_out,
                                  (const uint8_t**)frm->extended_data, frm->nb_samples);
            if (got > 0) {
                pcm_out.insert(pcm_out.end(),
                               conv.begin(),
                               conv.begin() + (size_t)got * (size_t)ctx->ch_layout.nb_channels);
            }
            av_frame_unref(frm);
        }
    };

    const size_t pkt_sz = 2048;
    for (size_t pos = 0; pos < data_size; pos += pkt_sz) {
        size_t this_sz = std::min(pkt_sz, data_size - pos);
        if (av_new_packet(pkt, (int)this_sz) < 0) break;
        std::memcpy(pkt->data, data_ptr + pos, this_sz);
        if (avcodec_send_packet(ctx, pkt) >= 0) {
            feed_frame();
        }
        av_packet_unref(pkt);
    }

    avcodec_send_packet(ctx, nullptr);
    feed_frame();

    av_frame_free(&frm);
    av_packet_free(&pkt);
    swr_free(&swr);
    sample_rate = ctx->sample_rate;
    channels = ctx->ch_layout.nb_channels;
    avcodec_free_context(&ctx);

    if (pcm_out.empty()) {
        if (err_out) *err_out = "decoded zero samples";
        return false;
    }
    return true;
}

#else

bool decode_xma_to_pcm(const std::vector<uint8_t>& ,
                      std::vector<int16_t>& ,
                      int& ,
                      int& ,
                      std::string* err_out) {
    if (err_out) {
        *err_out = "XMA2 decoder not available — drop FFmpeg prebuilts (libavcodec/avformat/avutil/swresample) "
                   "into include/ffmpeg/{include,lib}/ and rebuild.";
    }
    return false;
}

#endif

}
