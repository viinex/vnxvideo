#include <stdexcept>
#include <memory>
#include <algorithm>

extern "C" {
#include "libavcodec/avcodec.h"
}

#include "vnxvideoimpl.h"
#include "vnxvideologimpl.h"

#include "FFmpegUtils.h"

class CVideoDecoder : public VnxVideo::IVideoDecoder {
public:
    CVideoDecoder(AVCodecID codecID) 
        : m_csp(EMF_NONE)
        , m_width(0)
        , m_height(0)
    {
        m_cc=createAvDecoderContext(codecID,
            [=](AVCodecContext& cc) {
            cc.pkt_timebase = { 1,1000 };
            cc.time_base = { 1,1000 };

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
