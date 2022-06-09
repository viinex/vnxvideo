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
    //else if (level <= AV_LOG_DEBUG)
    //    return VNXLOG_DEBUG;
    else
        return VNXLOG_HIGHEST; 
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

    res->flags |= AV_CODEC_FLAG_LOW_DELAY;
    res->flags2 |= AV_CODEC_FLAG2_CHUNKS;
    res->flags2 |= AV_CODEC_FLAG2_FAST;

    setup(*res.get());

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

EColorspace fromAVPixelFormat(AVPixelFormat format) {
    switch (format) {
    case AV_PIX_FMT_YUVJ420P: // dont know how it differs from AV_PIX_FMT_YUV420P
    case AV_PIX_FMT_YUV420P: return EMF_I420; break;
    case AV_PIX_FMT_YUV422P: return EMF_YV12; break;
    case AV_PIX_FMT_NV12: return EMF_NV12; break;
    case AV_PIX_FMT_NV21: return EMF_NV21; break;
    case AV_PIX_FMT_YUYV422: return EMF_YUY2; break;
    case AV_PIX_FMT_UYVY422: return EMF_UYVY; break;
    case AV_PIX_FMT_YUV410P: return EMF_YVU9; break;

    case AV_PIX_FMT_RGB24: return EMF_RGB24; break;
    case AV_PIX_FMT_RGBA: return EMF_RGB32; break;
    case AV_PIX_FMT_BGR565BE:
    case AV_PIX_FMT_BGR565LE:
    case AV_PIX_FMT_BGR555LE:
    case AV_PIX_FMT_BGR555BE: return EMF_RGB16; break;
    default: return EMF_NONE;
    }
}

AVPixelFormat toAVPixelFormat(EColorspace csp) {
    switch (csp) {
    case EMF_I420: return AV_PIX_FMT_YUV420P;
    case EMF_YV12: return AV_PIX_FMT_YUV422P;
    case EMF_NV12: return AV_PIX_FMT_NV12;
    case EMF_NV21: return AV_PIX_FMT_NV21;
    case EMF_YUY2: return AV_PIX_FMT_YUYV422;
    case EMF_UYVY: return AV_PIX_FMT_UYVY422;
    case EMF_YVU9: return AV_PIX_FMT_YUV410P;
    case EMF_RGB24: return AV_PIX_FMT_RGB24;
    case EMF_RGB32: return AV_PIX_FMT_RGBA;
    case EMF_RGB16: return AV_PIX_FMT_BGR565LE;
    default: return AV_PIX_FMT_NONE;
    }
}

int nplanesByAVPixelFormat(AVPixelFormat format) {
    switch (format) {
    case AV_PIX_FMT_YUVJ420P:
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUV422P:
    case AV_PIX_FMT_YUV410P: return 3;
    case AV_PIX_FMT_NV12:
    case AV_PIX_FMT_NV21: return 2;
    case AV_PIX_FMT_YUYV422:
    case AV_PIX_FMT_UYVY422: return 1;
    case AV_PIX_FMT_RGB24:
    case AV_PIX_FMT_RGBA:
    case AV_PIX_FMT_BGR565BE:
    case AV_PIX_FMT_BGR565LE:
    case AV_PIX_FMT_BGR555LE:
    case AV_PIX_FMT_BGR555BE: return 1;
    default: return 0;
    }
}

CAvcodecRawSample::CAvcodecRawSample() {
    m_frame.reset(av_frame_alloc(), [](AVFrame* f) { av_frame_free(&f); });
}
CAvcodecRawSample::CAvcodecRawSample(const AVFrame* f) {
    m_frame.reset(av_frame_clone(f), [](AVFrame* f) { av_frame_free(&f); });
}

VnxVideo::IRawSample* CAvcodecRawSample::Dup() {
    return new CAvcodecRawSample(m_frame.get());
}
void CAvcodecRawSample::GetFormat(EColorspace &csp, int &width, int &height) {
    csp = fromAVPixelFormat((AVPixelFormat)m_frame->format);
    width = m_frame->width;
    height = m_frame->height;
}
void CAvcodecRawSample::GetData(int* strides, uint8_t** planes) {
    int nplanes = nplanesByAVPixelFormat((AVPixelFormat)m_frame->format);
    memcpy(strides, m_frame->linesize, nplanes * sizeof(int));
    memcpy(planes, m_frame->data, nplanes * sizeof(uint8_t*));
}
void CAvcodecRawSample::GetData(uint8_t* &data, int& size) {
    data = nullptr;
    size = 0;
}
AVFrame* CAvcodecRawSample::GetAVFrame() {
    return m_frame.get();
}
