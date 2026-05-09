// xma_codec_list.cpp — minimal override for libavcodec/allcodecs.obj.
//
// libavcodec.lib (the prebuilt static library) ships hundreds of codecs.
// Its allcodecs.obj declares an externally-visible array
//
//     const FFCodec * codec_list[] = { ...all codecs... };
//
// along with the public lookup functions avcodec_find_decoder(),
// avcodec_find_encoder(), avcodec_find_decoder_by_name(),
// avcodec_find_encoder_by_name(), and av_codec_iterate(). Because
// codec_list pins every codec's `ff_*_(de|en)coder` symbol, /OPT:REF
// cannot drop them — pulling ~25MB of unused codec code into the exe.
//
// This file replaces allcodecs.obj entirely. It defines all six of the
// symbols above, but its codec_list contains only the three we actually
// need (XMA1, XMA2, WMA Pro). At link time:
//
//   - The linker uses our codec_list and our find/iterate functions
//     (this .obj is on the command line ahead of libavcodec.lib).
//   - libavcodec.lib's allcodecs.obj is never pulled in (all of its
//     externally-visible symbols are already satisfied).
//   - The codec definitions for mp3/h264/aac/vp9/etc. are unreachable
//     from any retained code path, so /OPT:REF drops them.
//
// Result: the exe carries only wmaprodec + its transitive deps
// (bit reader, MDCT, libavutil mem/log/channel_layout) instead of the
// full FFmpeg surface.
//
// No FFmpeg source code is reproduced here — only ABI-level declarations
// (function signatures and the leading-field layout of FFCodec) needed
// to interoperate with the prebuilt libavcodec.lib. The headers we
// include (codec.h, avcodec.h) are FFmpeg's public LGPL headers shipped
// with the prebuilt and consumed unchanged. See
// external/ffmpeg/README.txt for the source/version of the libs we link.

#include <cstring>
#include <cstdint>
#include <cstddef>

extern "C" {
#include "libavcodec/codec.h"
#include "libavcodec/codec_id.h"
#include "libavcodec/avcodec.h"
}

// FFCodec is private to libavcodec; we don't have codec_internal.h.
// Forward-declare it as an incomplete type — we only ever take its
// address, never sizeof or copy. The reinterpret to AVCodec * relies
// on FFmpeg placing `AVCodec p` at offset 0 of FFCodec, which has been
// the layout for every supported version.
struct FFCodec;

extern "C" const FFCodec ff_xma1_decoder;
extern "C" const FFCodec ff_xma2_decoder;
extern "C" const FFCodec ff_wmapro_decoder;

// All six symbols below need C linkage so they can substitute for the
// matching ones in libavcodec.lib's allcodecs.obj at link time.
extern "C" {

// Match the exact symbol allcodecs.obj defines: a non-static, NULL-
// terminated array of FFCodec pointers.
const FFCodec * codec_list[] = {
    &ff_xma1_decoder,
    &ff_xma2_decoder,
    &ff_wmapro_decoder,
    nullptr,
};

static const AVCodec *codec_avcodec(const FFCodec *c) {
    // AVCodec p sits at offset 0 of FFCodec — see the comment on the
    // forward declaration above.
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

} // extern "C"
