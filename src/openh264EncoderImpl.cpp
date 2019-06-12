#include <stdexcept>
#include <memory>
#include <algorithm>
#include <cstring>

#include <wels/codec_api.h>

#include "vnxvideoimpl.h"
#include "vnxvideologimpl.h"

#include "BufferImpl.h"

#ifdef _WIN32
#pragma comment (lib,"openh264.lib")
#endif // _WIN32

inline ELogLevel loglevel2vnx(int level) {
    switch (level) {
    case WELS_LOG_DETAIL:
    case WELS_LOG_DEBUG: return VNXLOG_DEBUG;
    case WELS_LOG_INFO:  return VNXLOG_DEBUG;
    case WELS_LOG_WARNING: return VNXLOG_WARNING;
    case WELS_LOG_ERROR: return VNXLOG_ERROR;
    default: return VNXLOG_INFO;
    }
}

void openh264log(void*, int level, const char* msg) {
    VNXVIDEO_LOG(loglevel2vnx(level), "openh264engine") << msg;
}

class COpenH264Encoder : public VnxVideo::IVideoEncoder
{
private:
    const std::string m_profile;
    const std::string m_preset;
    const std::string m_quality;
    const int m_fps;
    VnxVideo::TOnBufferCallback m_onBuffer;

    std::shared_ptr<ISVCEncoder> m_encoder;

    EColorspace m_csp;
    int m_width;
    int m_height;
public:
    COpenH264Encoder(const char* profile, const char* preset, int fps, const char* quality)
        : m_profile(profile)
        , m_preset(preset)
        , m_fps(fps)
        , m_quality(quality)
    {
    }
    virtual void Subscribe(VnxVideo::TOnBufferCallback onBuffer) {
        m_onBuffer = onBuffer;
    }
    void SetFormat(EColorspace csp, int width, int height) {
        m_csp = csp;
        m_width = width;
        m_height = height;
        init();
        EVideoFormatType videoFormat(csp2vf(m_csp));
        m_encoder->SetOption(ENCODER_OPTION_DATAFORMAT, &videoFormat);
    }
    void OutputNAL(uint8_t *data, int size, uint64_t ts) {
        CNoOwnershipNalBuffer b(data, size);
        m_onBuffer(&b, ts);
    }
    virtual void Process(VnxVideo::IRawSample* sample, uint64_t timestamp) {
        if (!m_encoder.get())
            return;
        uint8_t* planes[4];
        int strides[4];
        sample->GetData(strides, planes);

        SSourcePicture pic;
        memset(&pic, 0, sizeof(SSourcePicture));
        pic.iPicWidth = m_width;
        pic.iPicHeight = m_height;
        pic.iColorFormat = csp2vf(m_csp);
        pic.uiTimeStamp = timestamp;
        memcpy(pic.iStride, strides, 4 * sizeof(int));
        memcpy(pic.pData, planes, 4 * sizeof(uint8_t*));

        SFrameBSInfo info;
        memset(&info, 0, sizeof(SFrameBSInfo));
        m_encoder->EncodeFrame(&pic, &info);
        if (info.eFrameType != videoFrameTypeSkip) {
            for (int j = 0; j < info.iLayerNum; ++j) {
                uint8_t* ptr = info.sLayerInfo[j].pBsBuf;
                for (int k = 0; k < info.sLayerInfo[j].iNalCount; ++k) {
                    OutputNAL(ptr, info.sLayerInfo[j].pNalLengthInByte[k], info.uiTimeStamp);
                    ptr += info.sLayerInfo[0].pNalLengthInByte[k];
                }
            }
        }
    }
    virtual void Flush() {

    }
private:
    void init(){
        m_encoder.reset();
        ISVCEncoder* encoder;
        int res = WelsCreateSVCEncoder(&encoder);
        if (res)
            throw std::runtime_error("WelsCreateSVCEncoder failed");
        auto logcb = openh264log;
        encoder->SetOption(ENCODER_OPTION_TRACE_CALLBACK, &logcb);
        auto loglevel = WELS_LOG_INFO;
        encoder->SetOption(ENCODER_OPTION_TRACE_LEVEL, &loglevel);

        int qp;
        ECOMPLEXITY_MODE complexity;
        EProfileIdc profile;
        qualityEnumToQpAndComplexity(m_profile, m_quality, m_preset, profile, qp, complexity);

        SEncParamExt param;
        encoder->GetDefaultParams(&param);
        param.iUsageType = CAMERA_VIDEO_REAL_TIME;
        param.fMaxFrameRate = (float)m_fps;
        param.iPicWidth = m_width;
        param.iPicHeight = m_height;
        param.iTargetBitrate = UNSPECIFIED_BIT_RATE;
        param.iRCMode = RC_OFF_MODE; // RC_QUALITY_MODE;
        param.iTemporalLayerNum = 1;
        param.iSpatialLayerNum = 1;
        for (int i = 0; i < param.iSpatialLayerNum; i++) {
            param.sSpatialLayers[i].uiProfileIdc = profile;
            param.sSpatialLayers[i].uiLevelIdc = LEVEL_5_2;
            param.sSpatialLayers[i].iVideoWidth = m_width >> (param.iSpatialLayerNum - 1 - i);
            param.sSpatialLayers[i].iVideoHeight = m_height >> (param.iSpatialLayerNum - 1 - i);
            param.sSpatialLayers[i].fFrameRate = (float)m_fps;
            param.sSpatialLayers[i].iSpatialBitrate = UNSPECIFIED_BIT_RATE;
            param.sSpatialLayers[i].iDLayerQp = qp;
            param.sSpatialLayers[i].iMaxSpatialBitrate = UNSPECIFIED_BIT_RATE;

            param.sSpatialLayers[i].sSliceArgument.uiSliceMode = SM_SINGLE_SLICE;
            param.sSpatialLayers[i].sSliceArgument.uiSliceNum = 1;
        }
        param.iEntropyCodingModeFlag = (profile == PRO_BASELINE) ? 0 : 1;
        param.bEnableDenoise = 0;
        param.bEnableBackgroundDetection = 1;
        param.bEnableAdaptiveQuant = 1;
        param.bEnableFrameSkip = 0;
        param.bEnableLongTermReference = 0; // long term reference control
        param.iLtrMarkPeriod = 30;
        param.uiIntraPeriod = 30; // period of Intra frame
        param.bUseLoadBalancing = false;
        param.eSpsPpsIdStrategy = CONSTANT_ID;
        param.bPrefixNalAddingCtrl = 0;
        param.iComplexityMode = complexity;
        param.bSimulcastAVC = false;
        param.iMaxQp = qp+1;
        param.iMinQp = qp-1;
        res = encoder->InitializeExt(&param);
        if (res) {
            WelsDestroySVCEncoder(encoder);
            VNXVIDEO_LOG(VNXLOG_ERROR, "openh264") << "ISVCEncoder::InitializeExt failed, res=" << res;
            throw std::runtime_error("Could not initialize encoder: ISVCEncoder::InitializeExt failed");
        }
        m_encoder.reset(encoder, [](ISVCEncoder* e) {
            e->Uninitialize();
            WelsDestroySVCEncoder(e);
        });
    }
    static EVideoFormatType csp2vf(EColorspace csp) {
        switch (csp) {
        case EMF_I420:
            return videoFormatI420;
        case EMF_YV12:
            return videoFormatYV12;
        case EMF_UYVY:
            return videoFormatUYVY;
        case EMF_NV12:
            return videoFormatNV12;
        default:
            throw std::runtime_error("unsupported raw video format");
        }
    }
    static void qualityEnumToQpAndComplexity(const std::string& f, const std::string& q, const std::string& p, 
        EProfileIdc& profile, int& qp, ECOMPLEXITY_MODE& complexity)
    {
        if (f == "baseline")
            profile = PRO_BASELINE;
        else if (f == "main")
            profile = PRO_MAIN;
        else if (f == "high")
            profile = PRO_HIGH;
        else
            throw std::runtime_error("`profile' enum literal value not recognized");

        if (p == "ultrafast" || p == "superfast" || p == "veryfast")
            complexity = LOW_COMPLEXITY;
        else if (p == "faster" || p == "fast" || p == "medium")
            complexity = MEDIUM_COMPLEXITY;
        else if (p == "slow" || p == "slower" || p == "veryslow")
            complexity = HIGH_COMPLEXITY;
        else
            throw std::runtime_error("`preset' enum literal value not recognized");

        if (q == "best_quality") {
            qp = 18;
        }
        else if (q == "fine_quality") {
            qp = 21;
        }
        else if (q == "good_quality") {
            qp = 24;
        }
        else if (q == "normal") {
            qp = 27;
        }
        else if (q == "small_size") {
            qp = 32;
        }
        else if (q == "tiny_size") {
            qp = 38;
        }
        else if (q == "best_size") {
            qp = 45;
        }
        else {
            throw std::runtime_error("`quality' enum literal value not recognized");
        }
    }
};

namespace VnxVideo {
    IVideoEncoder* CreateVideoEncoder_OpenH264(const char* profile, const char* preset, int fps, const char* quality) {
        return new COpenH264Encoder(profile, preset, fps, quality);
    }
}
