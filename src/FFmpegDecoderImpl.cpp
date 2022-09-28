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
    CVideoDecoder(AVCodecID codecID, VnxVideo::ECodecImpl eci)
        : m_csp(EMF_NONE)
        , m_width(0)
        , m_height(0)
        , m_codecImpl(eci)
    {
        m_cc=createAvDecoderContext(codecID,
            [=](AVCodecContext& cc) {
            cc.pkt_timebase = { 1,1000 };
            cc.time_base = { 1,1000 };

            AVBufferRef* hw = nullptr;
            AVHWDeviceType hwDevType = AV_HWDEVICE_TYPE_NONE;
            AVPixelFormat hwPixFmt = AV_PIX_FMT_NONE;

            const char* const hwDecoderEnv = getenv("VNX_HW_DECODER");
            if (hwDecoderEnv != 0 && strncmp(hwDecoderEnv, "0", 1) == 0 ) {
                hwDevType = AV_HWDEVICE_TYPE_NONE;
                hwPixFmt = AV_PIX_FMT_NONE;
            }
            else if (m_codecImpl == VnxVideo::ECodecImpl::ECI_D3D11VA) {
                hwDevType = AV_HWDEVICE_TYPE_D3D11VA;
                hwPixFmt = AV_PIX_FMT_D3D11;
            }
            else if (m_codecImpl == VnxVideo::ECodecImpl::ECI_VAAPI) {
                hwDevType = AV_HWDEVICE_TYPE_VAAPI;
                hwPixFmt = AV_PIX_FMT_VAAPI;
            }
            else if (m_codecImpl == VnxVideo::ECodecImpl::ECI_CUDA) {
                hwDevType = AV_HWDEVICE_TYPE_CUDA;
                hwPixFmt = AV_PIX_FMT_CUDA;
            }
            else if (m_codecImpl == VnxVideo::ECodecImpl::ECI_QSV) {
                hwDevType = AV_HWDEVICE_TYPE_QSV;
                hwPixFmt = AV_PIX_FMT_QSV;
            }

            if (hwDevType != AV_HWDEVICE_TYPE_NONE) {
                int res = av_hwdevice_ctx_create(&hw, hwDevType, nullptr, nullptr, 0);
                if (res != 0) {
                    VNXVIDEO_LOG(VNXLOG_WARNING, "ffmpeg") << "av_hwdevice_ctx_create failed: " << res << ": " << fferr2str(res);
                    throw VnxVideo::XHWDeviceNotSupported();
                }
                else {
                    cc.hw_device_ctx = hw;
                    m_hwPixFmt = hwPixFmt;

                    if (codecID == AV_CODEC_ID_H264)
                        cc.flags2 &= ~AV_CODEC_FLAG2_CHUNKS;
                }
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
                VNXVIDEO_LOG(VNXLOG_DEBUG, "ffmpeg") << "avcodec_receive_frame failed: "
                    << res << ": " << fferr2str(res);
                return;
            }
            callOnFormat(result.GetAVFrame());
            m_onFrame(&result, result.GetAVFrame()->pts); // pkt_pts said to be deprecated but it's the only valid value
        }
    }
    void fetchDecodedHw() {
        for (;;) {
            std::shared_ptr<AVFrame> hwfrm(avframeAlloc());
            int res = avcodec_receive_frame(m_cc.get(), hwfrm.get());
            if (res == AVERROR(EAGAIN) || res == AVERROR_EOF) {
                return; // or sleep and continue?
            }
            if (res != 0) {
                VNXVIDEO_LOG(VNXLOG_DEBUG, "ffmpeg") << "avcodec_receive_frame failed: "
                    << res << ": " << fferr2str(res);
                return;
            }

            std::shared_ptr<AVFrame> dst;
            if (hwfrm->format == m_hwPixFmt) {
                dst = avframeAlloc();
                dst->format = AV_PIX_FMT_NV12;
                res = av_hwframe_transfer_data(dst.get(), hwfrm.get(), 0);
                if (res < 0) {
                    VNXVIDEO_LOG(VNXLOG_ERROR, "ffmpeg") << "av_hwframe_transfer_data failed: "
                        << res << ": " << fferr2str(res);
                    continue;
                }
                dst->pts = hwfrm->pts;
            }
            else {
                dst = hwfrm;
            }
            callOnFormat(dst.get());
            CAvcodecRawSample result(dst);
            m_onFrame(&result, dst->pts);
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
    const VnxVideo::ECodecImpl m_codecImpl;
    AVPixelFormat m_hwPixFmt;
    std::shared_ptr<AVCodecContext> m_cc;
    VnxVideo::TOnFormatCallback m_onFormat;
    VnxVideo::TOnFrameCallback m_onFrame;
    EColorspace m_csp;
    int m_width, m_height;
};

namespace VnxVideo {
    // Defines the priority for hardware decoders. Must end with ECI_CPU
    ECodecImpl decoderImplPrioTable[] = { ECodecImpl::ECI_CUDA, ECodecImpl::ECI_D3D11VA, ECodecImpl::ECI_QSV, ECodecImpl::ECI_VAAPI, ECodecImpl::ECI_CPU };

    VnxVideo::IVideoDecoder* CreateVideoDecoder_FFmpeg(AVCodecID cid) {
        try {
            for (const VnxVideo::ECodecImpl* eci = decoderImplPrioTable; *eci != ECodecImpl::ECI_CPU; ++eci) {
                if (isCodecImplSupported(*eci))
                    return new CVideoDecoder(cid, *eci);
            }
            return new CVideoDecoder(cid, ECodecImpl::ECI_CPU);
        }
        catch (const XHWDeviceNotSupported&) {
            VNXVIDEO_LOG(VNXLOG_INFO, "ffmpeg") << "Failed to create a HW accelerated decoder; CPU decoder will be used";
            return new CVideoDecoder(cid, ECodecImpl::ECI_CPU);
        }
    }
    VnxVideo::IVideoDecoder* CreateVideoDecoder_FFmpegH264() {
        return CreateVideoDecoder_FFmpeg(AV_CODEC_ID_H264);
    }
    VnxVideo::IVideoDecoder* CreateVideoDecoder_FFmpegHEVC() {
        return CreateVideoDecoder_FFmpeg(AV_CODEC_ID_HEVC);
    }
}
