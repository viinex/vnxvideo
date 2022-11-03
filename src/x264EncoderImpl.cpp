#include <stdexcept>
#include <memory>
#include <algorithm>
#include <cstring>

#include "vnxvideoimpl.h"
#include "vnxvideologimpl.h"

#include "BufferImpl.h"

#if 0

#include "x264.h"

void x264log(void*, int level, const char* format, va_list args) {
    char buf[256];
    buf[255] = 0;
    VNXVIDEO_LOG((ELogLevel)(level + 1), "x264engine") << NVnxVideoLogImpl::removecrlf(vsnprintf(buf, 255, format, args), buf);
}

class Cx264Encoder : public VnxVideo::IMediaEncoder
{
private:
    const std::string m_profile;
    const std::string m_preset;
    const int m_fps;
    const int m_quality;
    VnxVideo::TOnBufferCallback m_onBuffer;
    x264_param_t param;
    x264_picture_t pic;
    x264_picture_t pic_out;
    std::shared_ptr<x264_t> m_encoder;

    EColorspace m_csp;
    int m_width;
    int m_height;
    int m_nplanes;
public:
    Cx264Encoder(const char* profile, const char* preset, int fps, int quality)
        : m_preset(preset)
        , m_profile(profile)
        , m_csp(EMF_NONE)
        , m_nplanes(0)
        , m_fps(fps)
        , m_quality(quality)
        , m_encoder(nullptr)
    {
        x264_picture_init(&pic);
        x264_picture_init(&pic_out);
    }
    ~Cx264Encoder() {
        m_encoder.reset();
    }
    virtual void Subscribe(VnxVideo::TOnBufferCallback onBuffer) {
        m_onBuffer = onBuffer;
    }
    void SetFormat(EColorspace csp, int width, int height) {
        if (x264_param_default_preset(&param, m_preset.c_str(), "zerolatency") < 0) {
            VNXVIDEO_LOG(VNXLOG_ERROR, "x264") << "Could not set default preset " << m_preset;
            throw std::runtime_error("could not set default preset");
        }
        m_csp = csp;
        m_nplanes = EColorspace2nplanes(csp);
        param.i_csp = EColorspace2x264csp(csp);
        param.i_width = m_width = width;
        param.i_height = m_height = height;
        param.b_vfr_input = 0;
        param.b_repeat_headers = 1;
        param.b_annexb = 1;

        param.i_threads = 1;
        param.i_fps_num = m_fps;
        param.i_fps_den = 1;
        // Intra refres:
        param.i_keyint_max = std::max(m_fps, 10);
        param.b_intra_refresh = 0;
        //Rate control:
        //param.rc.i_rc_method = X264_RC_CRF;
        //param.rc.f_rf_constant = 35;
        //param.rc.f_rf_constant_max = 35; 
        
        param.rc.i_rc_method = X264_RC_CQP;
        param.rc.i_qp_constant = m_quality;
        param.rc.i_qp_min = 18;
        param.rc.i_qp_max = 45;

        //For streaming:
        param.b_repeat_headers = 1;
        param.b_annexb = 1;

        param.pf_log = x264log;

        if (x264_param_apply_profile(&param, m_profile.c_str()) < 0) {
            VNXVIDEO_LOG(VNXLOG_ERROR, "x264") << "Could not apply profile " << m_profile;
            throw std::runtime_error("could not apply profile");
        }

        m_encoder.reset(x264_encoder_open(&param), x264_encoder_close);
        if (!m_encoder.get()) {
            VNXVIDEO_LOG(VNXLOG_ERROR, "x264") << "Could not create encoder";
            throw std::runtime_error("could not create encoder");
        }
        else
            VNXVIDEO_LOG(VNXLOG_DEBUG, "x264") << "Successfully created encoder: colorspace " << csp 
                << ", " << width << "x" << height 
                << ", preset " << m_preset << ", profile " << m_profile;

        pic.param = &param;
    }
    virtual void Process(VnxVideo::IRawSample* sample, uint64_t timestamp) {
        int i_frame_size;
        x264_nal_t *nal;
        int i_nal;

        uint8_t* planes[4];
        int strides[4];
        sample->GetData(strides, planes);

        pic.img.i_csp = m_csp;
        pic.img.i_plane = m_nplanes;
        memcpy(pic.img.i_stride, strides, m_nplanes * sizeof(int));
        memcpy(pic.img.plane, planes, m_nplanes * sizeof(uint8_t*));
        pic.i_pts = timestamp;
        //pic.b_keyframe = 1;

        i_frame_size = x264_encoder_encode(m_encoder.get(), &nal, &i_nal, &pic, &pic_out);
        if (i_frame_size < 0) {
            VNXVIDEO_LOG(VNXLOG_ERROR, "x264") << "x264_encoder_encode failed";
            throw std::runtime_error("x264_encoder_encode failed");
        }
        else if (i_frame_size)
        {
            for (int k = 0; k<i_nal; ++k)
                OutputNAL(nal[k].p_payload, nal[k].i_payload, pic_out.i_pts);
        }
    }
    virtual void Flush() {
        int i_frame_size;
        x264_nal_t *nal;
        int i_nal;

        while (x264_encoder_delayed_frames(m_encoder.get())) {
            i_frame_size = x264_encoder_encode(m_encoder.get(), &nal, &i_nal, NULL, &pic_out);
            if (i_frame_size < 0) {
                VNXVIDEO_LOG(VNXLOG_ERROR, "x264") << "Flush: encode failed";
                throw std::runtime_error("Flush: encode failed");
            }
            else if (i_frame_size)
            {
                VNXVIDEO_LOG(VNXLOG_DEBUG, "x264") << "Flushing the encoder: bytes left: " << i_frame_size;
                for(int k=0; k<i_nal; ++k)
                    OutputNAL(nal[k].p_payload, nal[k].i_payload, pic_out.i_pts);
            }
        }
    }
    void OutputNAL(uint8_t *data, int size, uint64_t ts) {
        CNoOwnershipNalBuffer b(data, size);
        m_onBuffer(&b, ts);
    }
private:
    static int EColorspace2x264csp(EColorspace csp) {
        switch (csp) {
        case EMF_I420: return X264_CSP_I420;
        case EMF_YV12: return X264_CSP_YV12;
        case EMF_NV12: return X264_CSP_NV12;
        case EMF_NV21: return X264_CSP_NV21;
        default: throw std::runtime_error("unsupported colorspace value");
        }
    }
    static int EColorspace2nplanes(EColorspace csp) {
        switch (csp) {
        case EMF_I420: return 3;
        case EMF_YV12: return 3;
        case EMF_NV12: return 2;
        case EMF_NV21: return 2;
        default: throw std::runtime_error("unsupported colorspace value");
        }
    }
};

int qualityEnumToQP(const std::string& q)
{
    if (q == "best_quality")
        return 18;
    else if (q == "fine_quality")
        return 21;
    else if (q == "good_quality")
        return 24;
    else if (q == "normal")
        return 27;
    else if (q == "small_size")
        return 32;
    else if (q == "tiny_size")
        return 38;
    else if (q == "best_size")
        return 45;
    else
        return 27;
}

namespace VnxVideo {
    IMediaEncoder* CreateVideoEncoder_x264(const char* profile, const char* preset, int fps, const char* quality) {
        return new Cx264Encoder(profile, preset, fps, qualityEnumToQP(quality));
    }
}
#else
namespace VnxVideo {
    IMediaEncoder* CreateVideoEncoder_x264(const char* profile, const char* preset, int fps, const char* quality) {
        throw std::runtime_error("x264 encoder is not supported");
    }
}
#endif