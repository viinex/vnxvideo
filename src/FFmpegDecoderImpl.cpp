#include <stdexcept>
#include <memory>
#include <algorithm>

extern "C" {
#include "libavcodec/avcodec.h"
}

#include "vnxvideoimpl.h"
#include "vnxvideologimpl.h"

#include "FFmpegUtils.h"


class CVideoDecoder : public VnxVideo::IMediaDecoder {
public:
    CVideoDecoder(AVCodecID codecID, VnxVideo::ECodecImpl eci)
        : m_csp(EMF_NONE)
        , m_width(0)
        , m_height(0)
        , m_codecID(codecID)
        , m_codecImpl(eci)
    {
        reInitialize();
    }
    void reInitialize(){
        m_cc.reset();
        m_parser.reset();
        m_errorsInARow = 0;

        AVPixelFormat hwPixFmt = AV_PIX_FMT_NONE;
        AVHWDeviceType hwDevType = AV_HWDEVICE_TYPE_NONE;
        FAVCCGetPixelFormat get_format = nullptr;

        const char* const hwDecoderEnv = getenv("VNX_HW_DECODER");
        if (hwDecoderEnv == 0 || strncmp(hwDecoderEnv, "0", 1) != 0) {
            std::tie(hwDevType, hwPixFmt, get_format) = fromHwDeviceType(m_codecImpl);
        }

        m_cc=createAvDecoderContext(m_codecID, hwDevType,
            [=](AVCodecContext& cc) {
            cc.thread_count = 1;
            cc.pkt_timebase = { 1,1000 };
            cc.time_base = { 1,1000 };

#if defined(_WIN64) || defined(__linux__)
            AVBufferRef* hw = nullptr;
    	    VNXVIDEO_LOG(VNXLOG_DEBUG, "ffmpeg") << "av_hwdevice_ctx_create about to be called, hwDevType=" << hwDevType;
            if (hwDevType != AV_HWDEVICE_TYPE_NONE) {
                int res = av_hwdevice_ctx_create(&hw, hwDevType, nullptr, nullptr, 0);
                if (res != 0) {
                    VNXVIDEO_LOG(VNXLOG_WARNING, "ffmpeg") << "av_hwdevice_ctx_create failed: " << res << ": " << fferr2str(res);
                    throw VnxVideo::XHWDeviceNotSupported();
                }
                else {
                    VNXVIDEO_LOG(VNXLOG_DEBUG, "ffmpeg") << "av_hwdevice_ctx_create succeeded, hwDevType=" << hwDevType;
                    cc.hw_device_ctx = hw;
                    m_hwPixFmt = hwPixFmt;
                    if(get_format)
                        cc.get_format = get_format;
                }
            }
#endif
        });
        m_parser.reset(av_parser_init(m_codecID), av_parser_close);
    }
    virtual void Subscribe(VnxVideo::TOnFormatCallback onFormat, VnxVideo::TOnFrameCallback onFrame) {
        m_onFormat = onFormat;
        m_onFrame = onFrame;
    }
    virtual void Decode(VnxVideo::IBuffer* nalu, uint64_t timestamp) {
        if (m_errorsInARow > 250) {
            VNXVIDEO_LOG(VNXLOG_INFO, "ffmpeg") << "Reached 250 errors in a row mark; will re-initialize the decoder";
            reInitialize();
        }
        uint8_t* data=nullptr;
        int size=0;
        nalu->GetData(data, size);
        while (size > 0) {
            AVPacket p;
            memset(&p, 0, sizeof p);
            int res = av_parser_parse2(m_parser.get(), m_cc.get(), &p.data, &p.size, data, size, timestamp, AV_NOPTS_VALUE, 0);
            if (res < 0) {
                VNXVIDEO_LOG(VNXLOG_DEBUG, "ffmpeg") << "av_parser_parse2 failed: " << res << ": " << fferr2str(res);
                break;
            }
            size -= res;
            data += res;
            if (p.size == 0)
                continue; // ignore empty packet, -- progress is made by parser
            p.pts = m_parser->pts;
            p.dts = m_parser->dts;
            p.pos = -1;
            res = avcodec_send_packet(m_cc.get(), &p);
            if (res == AVERROR_EOF) {
                avcodec_flush_buffers(m_cc.get());
                ++m_errorsInARow;
                continue;
            }
            if (0 != res) {
                VNXVIDEO_LOG(VNXLOG_DEBUG, "ffmpeg") << "avcodec_send_packet failed: " << res << ": " << fferr2str(res);
                ++m_errorsInARow;
            }
            else
                fetchDecoded();
        }
    }
    virtual void Flush() {
        AVPacket p;
        memset(&p, 0, sizeof p);
        p.pts = AV_NOPTS_VALUE;
        int res = avcodec_send_packet(m_cc.get(), &p);
        if (0 != res)
            VNXVIDEO_LOG(VNXLOG_DEBUG, "ffmpeg") << "avcodec_send_packet failed: " << res << ": " << fferr2str(res);
        else
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
                ++m_errorsInARow;
                return;
            }
            if (res != 0) {
                VNXVIDEO_LOG(VNXLOG_DEBUG, "ffmpeg") << "avcodec_receive_frame failed: "
                    << res << ": " << fferr2str(res);
                ++m_errorsInARow;
                return;
            }
            m_errorsInARow = 0;
            callOnFormat(result.GetAVFrame());
            m_onFrame(&result, result.GetAVFrame()->pts); // pkt_pts said to be deprecated but it's the only valid value
        }
    }
    void fetchDecodedHw() {
#if defined(_WIN64) || defined(__linux__)
        for (;;) {
            std::shared_ptr<AVFrame> hwfrm(avframeAlloc());
            int res = avcodec_receive_frame(m_cc.get(), hwfrm.get());
            if (res == AVERROR(EAGAIN) || res == AVERROR_EOF) {
                ++m_errorsInARow;
                return; // or sleep and continue?
            }
            if (res != 0) {
                VNXVIDEO_LOG(VNXLOG_DEBUG, "ffmpeg") << "avcodec_receive_frame failed: "
                    << res << ": " << fferr2str(res);
                ++m_errorsInARow;
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
                    ++m_errorsInARow;
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
            m_errorsInARow = 0;
        }
#endif
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
    const VnxVideo::ECodecImpl m_codecImpl;
    AVPixelFormat m_hwPixFmt;
    std::shared_ptr<AVCodecContext> m_cc;
    std::shared_ptr<AVCodecParserContext> m_parser;
    VnxVideo::TOnFormatCallback m_onFormat;
    VnxVideo::TOnFrameCallback m_onFrame;
    EColorspace m_csp;
    int m_width, m_height;
    int m_errorsInARow;
};

namespace VnxVideo {
    // Defines the priority for hardware decoders. Must end with ECI_CPU
    ECodecImpl decoderImplPrioTable[] = { 
        ECodecImpl::ECI_CUDA, 
#if defined(__aarch64__)
        ECodecImpl::ECI_RKMPP, 
#endif
        ECodecImpl::ECI_QSV,
        ECodecImpl::ECI_D3D12VA,
        ECodecImpl::ECI_VAAPI, 
        ECodecImpl::ECI_CPU };

    VnxVideo::IMediaDecoder* CreateVideoDecoder_FFmpeg(AVCodecID cid, bool cpuOnly) {
        try {
            if (!cpuOnly) {
                for (const VnxVideo::ECodecImpl* eci = decoderImplPrioTable; *eci != ECodecImpl::ECI_CPU; ++eci) {
                    if (isCodecImplSupported(*eci))
                        return new CVideoDecoder(cid, *eci);
                }
            }
            return new CVideoDecoder(cid, ECodecImpl::ECI_CPU);
        }
        catch (const XHWDeviceNotSupported&) {
            VNXVIDEO_LOG(VNXLOG_INFO, "ffmpeg") << "Failed to create a HW accelerated decoder; CPU decoder will be used";
            return new CVideoDecoder(cid, ECodecImpl::ECI_CPU);
        }
    }
    VnxVideo::IMediaDecoder* CreateVideoDecoder_FFmpegH264(bool cpuOnly) {
        return CreateVideoDecoder_FFmpeg(AV_CODEC_ID_H264, cpuOnly);
    }
    VnxVideo::IMediaDecoder* CreateVideoDecoder_FFmpegHEVC(bool cpuOnly) {
        return CreateVideoDecoder_FFmpeg(AV_CODEC_ID_HEVC, cpuOnly);
    }
}
