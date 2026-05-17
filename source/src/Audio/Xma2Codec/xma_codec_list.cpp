
#include <cstring>
#include <cstdint>
#include <cstddef>

extern "C" {
#include "libavcodec/codec.h"
#include "libavcodec/codec_id.h"
#include "libavcodec/avcodec.h"
}

struct FFCodec;

extern "C" const FFCodec ff_xma1_decoder;
extern "C" const FFCodec ff_xma2_decoder;
extern "C" const FFCodec ff_wmapro_decoder;

extern "C" {

const FFCodec * codec_list[] = {
    &ff_xma1_decoder,
    &ff_xma2_decoder,
    &ff_wmapro_decoder,
    nullptr,
};

static const AVCodec *codec_avcodec(const FFCodec *c) {

    return reinterpret_cast<const AVCodec *>(c);
}

const AVCodec *av_codec_iterate(void **opaque) {
    auto i = reinterpret_cast<std::uintptr_t>(*opaque);
    const FFCodec *c = codec_list[i];
    if (!c) return nullptr;
    *opaque = reinterpret_cast<void *>(i + 1);
    return codec_avcodec(c);
}

const AVCodec *avcodec_find_decoder(enum AVCodecID id) {    
    void *iter = nullptr;
    while (const AVCodec *p = av_codec_iterate(&iter)) {
        if (av_codec_is_decoder(p) && p->id == id) return p;
    }
    return nullptr;
}

const AVCodec *avcodec_find_encoder(enum AVCodecID id) {
    void *iter = nullptr;
    while (const AVCodec *p = av_codec_iterate(&iter)) {
        if (av_codec_is_encoder(p) && p->id == id) return p;
    }
    return nullptr;
}

const AVCodec *avcodec_find_decoder_by_name(const char *name) {
    if (!name) return nullptr;
    void *iter = nullptr;
    while (const AVCodec *p = av_codec_iterate(&iter)) {
        if (av_codec_is_decoder(p) && p->name && std::strcmp(p->name, name) == 0) return p;
    }
    return nullptr;
}

const AVCodec *avcodec_find_encoder_by_name(const char *name) {
    if (!name) return nullptr;
    void *iter = nullptr;
    while (const AVCodec *p = av_codec_iterate(&iter)) {
        if (av_codec_is_encoder(p) && p->name && std::strcmp(p->name, name) == 0) return p;
    }
    return nullptr;
}

}
