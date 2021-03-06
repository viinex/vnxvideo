#include <stdexcept>
#include <memory>
#include <algorithm>

extern "C" {
#include "libavcodec/avcodec.h"
}

#include "vnxvideoimpl.h"
#include "vnxvideologimpl.h"


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
    avcodec_register_all();
}


inline EColorspace fromAVPixelFormat(AVPixelFormat format) {
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

inline int nplanesByAVPixelFormat(AVPixelFormat format) {
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


class CAvcodecRawSample : public VnxVideo::IRawSample {
public:
    CAvcodecRawSample() {
        m_frame.reset(av_frame_alloc(), [](AVFrame* f) { av_frame_free(&f); });
    }
    CAvcodecRawSample(const AVFrame* f) {
        m_frame.reset(av_frame_clone(f), [](AVFrame* f) { av_frame_free(&f); });
    }

    VnxVideo::IRawSample* Dup() {
        return new CAvcodecRawSample(m_frame.get());
    }
    virtual void GetFormat(EColorspace &csp, int &width, int &height) {
        csp = fromAVPixelFormat((AVPixelFormat)m_frame->format);
        width = m_frame->width;
        height = m_frame->height;
    }
    virtual void GetData(int* strides, uint8_t** planes) {
        int nplanes = nplanesByAVPixelFormat((AVPixelFormat)m_frame->format);
        memcpy(strides, m_frame->linesize, nplanes * sizeof(int));
        memcpy(planes, m_frame->data, nplanes * sizeof(uint8_t*));
    }
    virtual void GetData(uint8_t* &data, int& size) {
        data = nullptr;
        size = 0;
    }
    AVFrame* GetAVFrame() {
        return m_frame.get();
    }
private:
    std::shared_ptr<AVFrame> m_frame;
};

class CVideoDecoder : public VnxVideo::IVideoDecoder {
public:
    CVideoDecoder(AVCodecID codecID) 
        : m_codecID(codecID)
        , m_csp(EMF_NONE)
        , m_width(0)
        , m_height(0)
    {
        AVCodec* codec = avcodec_find_decoder(codecID);
        if (nullptr == codec)
            throw std::runtime_error("avcodec_find_decoder failed");
        m_cc.reset(avcodec_alloc_context3(codec), [](AVCodecContext* cc) {avcodec_free_context(&cc); });
        int res = avcodec_open2(m_cc.get(), codec, 0);
        if(res<0)
            throw std::runtime_error("avcodec_open2 failed");
        m_cc->flags |= AV_CODEC_FLAG_LOW_DELAY;
        m_cc->flags2 |= AV_CODEC_FLAG2_CHUNKS;
        m_cc->flags2 |= AV_CODEC_FLAG2_FAST;
    }
    virtual void Subscribe(VnxVideo::TOnFormatCallback onFormat, VnxVideo::TOnFrameCallback onFrame) {
        m_onFormat = onFormat;
        m_onFrame = onFrame;
    }
    virtual void Decode(VnxVideo::IBuffer* nalu, uint64_t timestamp) {
        AVPacket p;
        memset(&p, 0, sizeof p);
        nalu->GetData(p.data, p.size);
        p.pts = timestamp;
        p.dts = AV_NOPTS_VALUE;
        p.pos = -1;
        int res = avcodec_send_packet(m_cc.get(), &p);
        if (0 != res)
            VNXVIDEO_LOG(VNXLOG_DEBUG, "ffmpeg") << "avcodec_send_packet failed: " << res;
        fetchDecoded();
    }
    virtual void Flush() {
        AVPacket p;
        memset(&p, 0, sizeof p);
        p.pts = AV_NOPTS_VALUE;
        p.dts = AV_NOPTS_VALUE;
        p.pos = -1;
        int res = avcodec_send_packet(m_cc.get(), &p);
        if (0 != res)
            VNXVIDEO_LOG(VNXLOG_DEBUG, "ffmpeg") << "avcodec_send_packet failed: " << res;
        fetchDecoded();
    }
private:
    void fetchDecoded() {
        while (0 == avcodec_receive_frame(m_cc.get(), m_result.GetAVFrame())) {
            callOnFormat(m_result.GetAVFrame());
            m_onFrame(&m_result, m_result.GetAVFrame()->pkt_pts); // pkt_pts said to be deprecated but it's the only valid value
        }
    }
    void callOnFormat(AVFrame* f) {
        EColorspace csp = fromAVPixelFormat((AVPixelFormat)f->format);
        if (m_width != f->width || m_height != f->height || m_csp != csp) {
            m_width = f->width;
            m_height = f->height;
            m_csp = csp;
            m_onFormat(m_csp, m_width, m_height);
        }
    }
private:
    const AVCodecID m_codecID;
    std::shared_ptr<AVCodecContext> m_cc;
    VnxVideo::TOnFormatCallback m_onFormat;
    VnxVideo::TOnFrameCallback m_onFrame;
    EColorspace m_csp;
    int m_width, m_height;

    CAvcodecRawSample m_result; // this is just an optimization for not calling
    // av_frame_alloc/av_frame_free on each decoded frame.
    // this m_result should not be used by anyone except Decode() 
    // when reading the decoding results out
};

namespace VnxVideo {
    VnxVideo::IVideoDecoder* CreateVideoDecoder_FFmpegH264() {
        return new CVideoDecoder(AV_CODEC_ID_H264);
    }
}
