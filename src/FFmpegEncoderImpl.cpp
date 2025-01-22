#include <stdexcept>
#include <memory>
#include <algorithm>
#include <cstring>

#include "RawSample.h"
#include "FFmpegUtils.h"
#include "BufferImpl.h"

extern "C" {
#include <libswscale/swscale.h>
#include "libavutil/opt.h"
}


class CFFmpegEncoderImpl : public VnxVideo::IMediaEncoder
{
private:
    const std::string m_profile;
    const std::string m_preset;
    const std::string m_quality;
    const int m_qp;
    const int m_fps;
    const VnxVideo::ECodecImpl m_codecImpl;

    VnxVideo::TOnBufferCallback m_onBuffer;

    std::shared_ptr<SwsContext> m_swsc;
    std::shared_ptr<AVCodecContext> m_cc;

    EColorspace m_csp;
    int m_width;
    int m_height;
public:
    CFFmpegEncoderImpl(const char* profile, const char* preset, int fps, const char* quality, VnxVideo::ECodecImpl eci)
        : m_profile(profile)
        , m_preset(preset)
        , m_fps(fps)
        , m_quality(quality)
        , m_codecImpl(eci)
        , m_qp(qualityEnumToQp(quality))
    {
        if (m_preset == "ultrafast" || m_preset == "superfast") { // QSV does not support these
            const_cast<std::string&>(m_preset) = "veryfast";
        }
        if (m_profile == "baseline" && m_codecImpl == VnxVideo::ECodecImpl::ECI_VAAPI) {
            const_cast<std::string&>(m_profile) = "constrained_baseline";
        }
    }
    virtual void Subscribe(VnxVideo::TOnBufferCallback onBuffer) {
        m_onBuffer = onBuffer;
    }
    void SetFormat(EColorspace csp, int width, int height) {
        if (!vnxvideo_emf_is_video(csp))
            return;
        m_csp = csp;
        m_width = width;
        m_height = height;

        m_cc.reset();

        if (m_codecImpl != VnxVideo::ECodecImpl::ECI_CPU && csp != EMF_NV12) {
            m_swsc.reset(sws_getContext(width, height, toAVPixelFormat(csp),
                width, height, AV_PIX_FMT_NV12, SWS_BILINEAR,
                nullptr, nullptr, nullptr), sws_freeContext);
        }
        else
            m_swsc.reset();

    }
    void OutputNAL(uint8_t *data, int size, uint64_t ts) {
        CNoOwnershipNalBuffer b(data, size);
        m_onBuffer(&b, ts);
    }
    virtual void Process(VnxVideo::IRawSample* sample, uint64_t timestamp) {
        ERawMediaFormat emf;
        int width, height;
        sample->GetFormat(emf, width, height);
        if (!vnxvideo_emf_is_video(emf)) {
            return;
        }

        try {
            checkCreateCc();
        }
        catch (const std::runtime_error&e) {
            VNXVIDEO_LOG(VNXLOG_WARNING, "renderer") << "CFFmpegEncoderImpl::Process: failed to create codec context: " << e.what();
            return;
        }

        uint8_t* planes_src[4] = { 0,0,0,0 };
        int strides_src[4] = { 0,0,0,0 };
        sample->GetData(strides_src, planes_src);

        VnxVideo::PRawSample sampleNV12;
        uint8_t* planes_dst[4] = { 0,0,0,0 };
        int strides_dst[4] = { 0,0,0,0 };

        uint8_t** planes = planes_src;
        int* strides = strides_src;

        if (m_swsc.get()) {
            sampleNV12.reset(new CRawSample(EMF_NV12, m_width, m_height, nullptr, nullptr, true));
            sampleNV12->GetData(strides_dst, planes_dst);
            int res = sws_scale(m_swsc.get(), planes, strides, 0, m_height, planes_dst, strides_dst);
            if (res != m_height) {
                VNXVIDEO_LOG(VNXLOG_WARNING, "renderer") << "CFFmpegEncoderImpl::Process: sws_scale failed";
                return;
            }
            else {
                planes = planes_dst;
                strides = strides_dst;
            }
        }

        std::shared_ptr<AVFrame> frm(avframeAlloc());
        memcpy(frm->data, planes, 4 * sizeof(uint8_t*));
        memcpy(frm->linesize, strides, 4 * sizeof(int));
        frm->format = (m_codecImpl == VnxVideo::ECodecImpl::ECI_CPU) ? AV_PIX_FMT_YUV420P : AV_PIX_FMT_NV12;
        frm->width = m_width;
        frm->height = m_height;
        frm->pts = timestamp;

#if defined(_WIN64) || defined(__linux__)
        if (m_cc->hw_frames_ctx != nullptr) {
            std::shared_ptr<AVFrame> dst(avframeAlloc());
            int res = av_hwframe_get_buffer(m_cc->hw_frames_ctx, dst.get(), 0);
            if (res < 0) {
                VNXVIDEO_LOG(VNXLOG_ERROR, "ffmpeg") << "CFFmpegEncoderImpl::Process: av_hwframe_get_buffer failed: "
                    << res << ": " << fferr2str(res);
                return;
            }
            //dst->hw_frames_ctx = av_buffer_ref(m_cc->hw_frames_ctx);
            res = av_hwframe_transfer_data(dst.get(), frm.get(), 0);
            if (res < 0) {
                VNXVIDEO_LOG(VNXLOG_ERROR, "ffmpeg") << "CFFmpegEncoderImpl::Process: av_hwframe_transfer_data failed: "
                    << res << ": " << fferr2str(res);
                return;
            }
            dst->pts = timestamp;
            frm = dst;
        }
#endif

        int ret = avcodec_send_frame(m_cc.get(), frm.get());
        if (ret < 0) {
            VNXVIDEO_LOG(VNXLOG_INFO, "vnxvideo") << "CFFmpegEncoderImpl::Process: Failed to avcodec_send_frame: "
                << ret << ": " << fferr2str(ret);
            return;
        }
        std::shared_ptr<AVPacket> pkt(avpacketAlloc());
        while (ret >= 0) {
            ret = avcodec_receive_packet(m_cc.get(), pkt.get());
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                return;
            else if (ret < 0) {
                VNXVIDEO_LOG(VNXLOG_INFO, "vnxvideo") << "CFFmpegEncoderImpl::Process: Failed to avcodec_receive_packet: "
                    << ret << ": " << fferr2str(ret);
                return;
            }
            else {
                CNoOwnershipNalBuffer buffer(pkt->data, pkt->size);
                m_onBuffer(&buffer, pkt->pts);
            }
        }
    }
    virtual void Flush() {

    }
private:
    void checkCreateCc() {
        if (m_cc.get())
            return;
        const char* encoderName = "h264";
        AVPixelFormat hwPixFmt = AV_PIX_FMT_NONE;

#if defined(_WIN64) || defined(__linux__)
        AVHWDeviceType hwDevType = AV_HWDEVICE_TYPE_NONE;
        if (m_codecImpl == VnxVideo::ECodecImpl::ECI_QSV) {
            encoderName = "h264_qsv";
            hwDevType = AV_HWDEVICE_TYPE_QSV;
            hwPixFmt = AV_PIX_FMT_QSV;
        }
        else if (m_codecImpl == VnxVideo::ECodecImpl::ECI_VAAPI) {
            encoderName = "h264_vaapi";
            hwDevType = AV_HWDEVICE_TYPE_VAAPI;
            hwPixFmt = AV_PIX_FMT_VAAPI;
        }
        else if (m_codecImpl == VnxVideo::ECodecImpl::ECI_CUDA) {
            encoderName = "h264_nvenc";
            hwDevType = AV_HWDEVICE_TYPE_CUDA;
            hwPixFmt = AV_PIX_FMT_CUDA;
        }
        else if (m_codecImpl == VnxVideo::ECodecImpl::ECI_RKMPP) {
            encoderName = "h264_rkmpp";
            hwDevType = AV_HWDEVICE_TYPE_RKMPP;
            hwPixFmt = AV_PIX_FMT_DRM_PRIME;
        }
#endif

        m_cc = createAvEncoderContext(encoderName,
            [=](AVCodecContext& cc) {
            cc.time_base = { 1, 1000 };
            cc.pix_fmt = hwPixFmt; // AV_PIX_FMT_D3D11; // AV_PIX_FMT_NV12; // AV_PIX_FMT_YUV420P;
            cc.width = m_width;
            cc.height = m_height;
            cc.framerate = { 25, 1 };
            cc.flags |= AV_CODEC_FLAG_LOW_DELAY;
            cc.flags2 |= AV_CODEC_FLAG2_FAST;
            cc.gop_size = 50;
            cc.max_b_frames = 0;
            cc.has_b_frames = 0;

            AVBufferRef* hw = nullptr;

            int res = av_opt_set(&cc, "profile", m_profile.c_str(), AV_OPT_SEARCH_CHILDREN);
            if (res < 0) {
                VNXVIDEO_LOG(VNXLOG_INFO, "vnxvideo") << "CFFmpegEncoderImpl::checkCreateCc: Failed to set profile: "
                    << res << ": " << fferr2str(res);
            }
            res = av_opt_set(&cc, "preset", m_preset.c_str(), AV_OPT_SEARCH_CHILDREN);
            if (res < 0) {
                VNXVIDEO_LOG(VNXLOG_INFO, "vnxvideo") << "CFFmpegEncoderImpl::checkCreateCc: Failed to set preset: "
                    << res << ": " << fferr2str(res);
            }
            res = av_opt_set_int(&cc, "qp", m_qp, AV_OPT_SEARCH_CHILDREN);
            if (res < 0) {
                VNXVIDEO_LOG(VNXLOG_INFO, "vnxvideo") << "CFFmpegEncoderImpl::checkCreateCc: Failed to set qp: "
                    << res << ": " << fferr2str(res);
            }
            res = av_opt_set_int(&cc, "idr_interval", 50, AV_OPT_SEARCH_CHILDREN);
            if (res < 0) {
                VNXVIDEO_LOG(VNXLOG_INFO, "vnxvideo") << "CFFmpegEncoderImpl::checkCreateCc: Failed to set idr_interval: "
                    << res << ": " << fferr2str(res);
            }

#if defined(_WIN64) || defined(__linux__)
            res = av_hwdevice_ctx_create(&hw, hwDevType, nullptr, nullptr, 0);
            if (res != 0) {
                VNXVIDEO_LOG(VNXLOG_INFO, "ffmpeg") << "CFFmpegEncoderImpl::checkCreateCc: av_hwdevice_ctx_create failed: " << res << ": " << fferr2str(res);
            }
            else {
                cc.hw_device_ctx = hw;
                checkFramesContext(cc, m_width, m_height, hwPixFmt);
            }
#endif
        });
    }
    static int qualityEnumToQp(const std::string& q)
    {
        if (q == "best_quality") {
            return 18;
        }
        else if (q == "fine_quality") {
            return 21;
        }
        else if (q == "good_quality") {
            return 24;
        }
        else if (q == "normal") {
            return 27;
        }
        else if (q == "small_size") {
            return 32;
        }
        else if (q == "tiny_size") {
            return 38;
        }
        else if (q == "best_size") {
            return 45;
        }
        else {
            throw std::runtime_error("`quality' enum literal value not recognized");
        }
    }
};

namespace VnxVideo {
    ECodecImpl encoderImplPrioTable[] = { ECodecImpl::ECI_CUDA, ECodecImpl::ECI_VAAPI, ECodecImpl::ECI_QSV, ECodecImpl::ECI_CPU };

    IMediaEncoder* CreateVideoEncoder_FFmpeg(const char* profile, const char* preset, int fps, const char* quality, ECodecImpl eci) {
        return new CFFmpegEncoderImpl(profile, preset, fps, quality, eci);
    }

    IMediaEncoder* CreateVideoEncoder_FFmpeg_Auto(const char* profile, const char* preset, int fps, const char* quality) {
        const char* const hwEncoderEnv = getenv("VNX_HW_ENCODER");
        if (hwEncoderEnv != 0 && strncmp(hwEncoderEnv, "0", 1) == 0 ) {
            return nullptr;
        }
        for (const VnxVideo::ECodecImpl* eci = encoderImplPrioTable; *eci != ECodecImpl::ECI_CPU; ++eci) {
            if (isCodecImplSupported(*eci))
                return new CFFmpegEncoderImpl(profile, preset, fps, quality, *eci);
        }
        return nullptr;
    }
}
