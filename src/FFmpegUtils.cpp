#include <functional>
#include "FFmpegUtils.h"

AVCodecID codecIdFromSubtype(EMediaSubtype subtype) {
    switch (subtype) {
    case EMST_H264: return AV_CODEC_ID_H264;
    case EMST_HEVC: return AV_CODEC_ID_HEVC;
    case EMST_PCMU: return AV_CODEC_ID_PCM_MULAW;
    case EMST_PCMA: return AV_CODEC_ID_PCM_ALAW;
    case EMST_OPUS: return AV_CODEC_ID_OPUS;
    case EMST_AAC: return AV_CODEC_ID_AAC;
    case EMST_WAV: return AV_CODEC_ID_PCM_S16LE;
    }
    throw std::runtime_error("unhandled case in codecIdFromSubtype: "+std::to_string((int)subtype));
}

ELogLevel ffmpeg2loglevel(int level) {
    if (level <= AV_LOG_ERROR)
        return VNXLOG_ERROR;
    else if (level <= AV_LOG_WARNING)
        return VNXLOG_WARNING;
    else if (level <= AV_LOG_INFO)
        return VNXLOG_INFO;
    else
        return VNXLOG_HIGHEST; // we do not want ffmpeg debug output really
}
int loglevel2ffmpeg(ELogLevel level) {
    switch (level) {
    case VNXLOG_NONE: return AV_LOG_QUIET;
    case VNXLOG_ERROR: return AV_LOG_ERROR;
    case VNXLOG_WARNING: return AV_LOG_WARNING;
    case VNXLOG_INFO: return AV_LOG_INFO;
    case VNXLOG_DEBUG: return AV_LOG_INFO; // no debug logs from ffmpeg //AV_LOG_VERBOSE;
    default: return AV_LOG_INFO;
    }
}

void ffmpeglog(void*, int level, const char* format, va_list args) {
    char buf[256];
    buf[255] = 0;
    VNXVIDEO_LOG(ffmpeg2loglevel(level), "ffmpeg") << NVnxVideoLogImpl::removecrlf(vsnprintf(buf, 255, format, args), buf);
}

void vnxvideo_init_ffmpeg(ELogLevel level) {
    av_log_set_level(loglevel2ffmpeg(level));
    av_log_set_callback(ffmpeglog);
}

std::string fferr2str(int errnum) {
    char res[AV_ERROR_MAX_STRING_SIZE]{ 0 };
    av_make_error_string(res, AV_ERROR_MAX_STRING_SIZE, errnum);
    return res;
}

std::shared_ptr<AVCodecContext> createAvDecoderContext(AVCodecID codecId, std::function<void(AVCodecContext&)> setup) {
    const AVCodec* codec = avcodec_find_decoder(codecId);
    if (!codec)
        throw std::runtime_error("createAvDecoderContext: avcodec_find_decoder failed: " + std::to_string(codecId));
    std::shared_ptr<AVCodecContext> res(avcodec_alloc_context3(codec), [](AVCodecContext* cc) {avcodec_free_context(&cc); });
    setup(*res.get());

    res->flags |= AV_CODEC_FLAG_LOW_DELAY;
    res->flags2 |= AV_CODEC_FLAG2_CHUNKS;
    res->flags2 |= AV_CODEC_FLAG2_FAST;

    int r = avcodec_open2(res.get(), codec, 0);
    if (r < 0)
        throw std::runtime_error("createAvDecoderContext: avcodec_open2 failed: "+std::to_string(r)+": "+fferr2str(r));
    return res;
}

std::shared_ptr<AVCodecContext> createAvEncoderContext(AVCodecID codecId, std::function<void(AVCodecContext&)> setup) {
    const AVCodec* codec = avcodec_find_encoder(codecId);
    if (!codec)
        throw std::runtime_error("createAvEncoderContext: avcodec_find_encoder failed: " + std::to_string(codecId));
    std::shared_ptr<AVCodecContext> res(avcodec_alloc_context3(codec), [](AVCodecContext* cc) {avcodec_free_context(&cc); });

    setup(*res.get());

    int r = avcodec_open2(res.get(), codec, 0);
    if (r < 0)
        throw std::runtime_error("createAvEncoderContext: avcodec_open2 failed: "+ std::to_string(r) + ": " + fferr2str(r));
    return res;
}

std::shared_ptr<AVFrame> avframeAlloc() {
    return std::shared_ptr<AVFrame>(av_frame_alloc(), [](AVFrame* f) { av_frame_free(&f); });
}

std::shared_ptr<AVPacket> avpacketAlloc() {
    return std::shared_ptr<AVPacket>(av_packet_alloc(), [](AVPacket* p) { av_packet_free(&p); });
}