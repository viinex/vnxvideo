#include <stdexcept>
#include <memory>
#include <algorithm>

extern "C" {
#include "libavcodec/avcodec.h"
}

#include "vnxvideoimpl.h"
#include "vnxvideologimpl.h"

#include "FFmpegUtils.h"

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
        : m_csp(EMF_NONE)
        , m_width(0)
        , m_height(0)
    {
        m_cc=createAvDecoderContext(codecID,
            [=](AVCodecContext& cc) {
            AVBufferRef* hw;
            AVHWDeviceType hwDevType = AV_HWDEVICE_TYPE_NONE;
#ifdef _WIN32
            hwDevType = AV_HWDEVICE_TYPE_D3D11VA;
#endif
#ifdef __linux__
            hwDevType = AV_HWDEVICE_TYPE_DRM;
#endif
            int res = av_hwdevice_ctx_create(&hw, hwDevType, nullptr, nullptr, 0);
            if (res != 0) {
                VNXVIDEO_LOG(VNXLOG_INFO, "ffmpeg") << "av_hwdevice_ctx_create failed: " << res << ": " << fferr2str(res);
            }
            else {
                cc.hw_device_ctx = hw;
                if(codecID == AV_CODEC_ID_H264)
                    cc.flags2 &= ~AV_CODEC_FLAG2_CHUNKS;
            }
        });
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
        int res = avcodec_send_packet(m_cc.get(), &p);
        if (0 != res)
            VNXVIDEO_LOG(VNXLOG_DEBUG, "ffmpeg") << "avcodec_send_packet failed: " << res << ": " << fferr2str(res);
        fetchDecoded();
    }
    virtual void Flush() {
        AVPacket p;
        memset(&p, 0, sizeof p);
        p.pts = AV_NOPTS_VALUE;
        int res = avcodec_send_packet(m_cc.get(), &p);
        if (0 != res)
            VNXVIDEO_LOG(VNXLOG_DEBUG, "ffmpeg") << "avcodec_send_packet failed: " << res << ": " << fferr2str(res);
        fetchDecoded();
    }
private:
    void fetchDecoded() {
        if (m_cc->hw_device_ctx)
            fetchDecodedHw();
        else
            fetchDecodedSw();
    }
    void fetchDecodedSw(){
        for(;;){
            CAvcodecRawSample result;
            int res = avcodec_receive_frame(m_cc.get(), result.GetAVFrame());
            if (res == AVERROR(EAGAIN) || res == AVERROR_EOF) {
                return;
            }
            if (res != 0) {
                VNXVIDEO_LOG(VNXLOG_ERROR, "ffmpeg") << "avcodec_receive_frame failed: "
                    << res << ": " << fferr2str(res);
                return;
            }
            callOnFormat(result.GetAVFrame());
            m_onFrame(&result, result.GetAVFrame()->pts); // pkt_pts said to be deprecated but it's the only valid value
        }
    }
    void fetchDecodedHw() {
        for (;;) {
            auto hwfrm = avframeAlloc();
            int res = avcodec_receive_frame(m_cc.get(), hwfrm.get());
            if (res == AVERROR(EAGAIN) || res == AVERROR_EOF) {
                return; // or sleep and continue?
            }
            if (res != 0) {
                VNXVIDEO_LOG(VNXLOG_ERROR, "ffmpeg") << "avcodec_receive_frame failed: "
                    << res << ": " << fferr2str(res);
                return;
            }
            CAvcodecRawSample result;
            AVFrame* dst = result.GetAVFrame();
            dst->format = AV_PIX_FMT_NV12;
            res = av_hwframe_transfer_data(dst, hwfrm.get(), 0);
            if (res < 0) {
                VNXVIDEO_LOG(VNXLOG_ERROR, "ffmpeg") << "av_hwframe_transfer_data failed: " 
                    << res << ": " << fferr2str(res);
                continue;
            }
            callOnFormat(dst);
            m_onFrame(&result, dst->pts); // pkt_pts said to be deprecated but it's the only valid value
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
    std::shared_ptr<AVCodecContext> m_cc;
    VnxVideo::TOnFormatCallback m_onFormat;
    VnxVideo::TOnFrameCallback m_onFrame;
    EColorspace m_csp;
    int m_width, m_height;
};

namespace VnxVideo {
    VnxVideo::IVideoDecoder* CreateVideoDecoder_FFmpegH264() {
        return new CVideoDecoder(AV_CODEC_ID_H264);
    }
    VnxVideo::IVideoDecoder* CreateVideoDecoder_FFmpegHEVC() {
        return new CVideoDecoder(AV_CODEC_ID_HEVC);
    }
}
