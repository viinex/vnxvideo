#include <functional>
#include <set>
#include <mutex>
#include "FFmpegUtils.h"

AVCodecID codecIdFromSubtype(EMediaSubtype subtype) {
    switch (subtype) {
    case EMST_H264: return AV_CODEC_ID_H264;
    case EMST_HEVC: return AV_CODEC_ID_HEVC;
    case EMST_PCMU: return AV_CODEC_ID_PCM_MULAW;
    case EMST_PCMA: return AV_CODEC_ID_PCM_ALAW;
    case EMST_OPUS: return AV_CODEC_ID_OPUS;
    case EMST_AAC: return AV_CODEC_ID_AAC;
    case EMST_G726: return AV_CODEC_ID_ADPCM_G726LE;
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
    return createAvDecoderContext(codec, setup);
}

std::shared_ptr<AVCodecContext> createAvDecoderContext(const char* name, std::function<void(AVCodecContext&)> setup) {
    const AVCodec* codec = avcodec_find_decoder_by_name(name);
    if (!codec)
        throw std::runtime_error("createAvDecoderContext: avcodec_find_decoder_by_name failed: " + std::string(name));
    return createAvDecoderContext(codec, setup);
}

std::shared_ptr<AVCodecContext> createAvDecoderContext(const AVCodec* codec, std::function<void(AVCodecContext&)> setup){
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
    return createAvEncoderContext(codec, setup);
}

std::shared_ptr<AVCodecContext> createAvEncoderContext(const char* name, std::function<void(AVCodecContext&)> setup) {
    const AVCodec* codec = avcodec_find_encoder_by_name(name);
    if (!codec)
        throw std::runtime_error("createAvEncoderContext: avcodec_find_encoder_by_name failed: " + std::string(name));
    return createAvEncoderContext(codec, setup);
}

std::shared_ptr<AVCodecContext> createAvEncoderContext(const AVCodec* codec, std::function<void(AVCodecContext&)> setup) {
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

ERawMediaFormat fromAVPixelFormat(AVPixelFormat format) {
    switch (format) {
    case AV_PIX_FMT_YUVJ420P: // dont know how it differs from AV_PIX_FMT_YUV420P
    case AV_PIX_FMT_YUV420P: return EMF_I420;
    case AV_PIX_FMT_YUV422P: return EMF_YV12;
    case AV_PIX_FMT_NV12: return EMF_NV12;
    case AV_PIX_FMT_NV21: return EMF_NV21;
    case AV_PIX_FMT_YUYV422: return EMF_YUY2;
    case AV_PIX_FMT_UYVY422: return EMF_UYVY;
    case AV_PIX_FMT_YUV410P: return EMF_YVU9;

    case AV_PIX_FMT_RGB24: return EMF_RGB24;
    case AV_PIX_FMT_RGBA: return EMF_RGB32;
    case AV_PIX_FMT_BGR565BE:
    case AV_PIX_FMT_BGR565LE:
    case AV_PIX_FMT_BGR555LE:
    case AV_PIX_FMT_BGR555BE: return EMF_RGB16;

    default: return EMF_NONE;
    }
}
ERawMediaFormat fromAVSampleFormat(AVSampleFormat format) {
    switch (format) {
    case AV_SAMPLE_FMT_S16:  return EMF_LPCM16;
    case AV_SAMPLE_FMT_S32:  return EMF_LPCM32;
    case AV_SAMPLE_FMT_FLT:  return EMF_LPCMF;
    case AV_SAMPLE_FMT_S16P: return EMF_LPCM16P;
    case AV_SAMPLE_FMT_S32P: return EMF_LPCM32P;
    case AV_SAMPLE_FMT_FLTP: return EMF_LPCMFP;

    default: return EMF_NONE;
    }
}

AVPixelFormat toAVPixelFormat(ERawMediaFormat csp) {
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
AVSampleFormat toAVSampleFormat(ERawMediaFormat emf) {
    switch (emf) {
    case EMF_LPCM16:  return AV_SAMPLE_FMT_S16;
    case EMF_LPCM32:  return AV_SAMPLE_FMT_S32;
    case EMF_LPCMF:   return AV_SAMPLE_FMT_FLT;
    case EMF_LPCM16P: return AV_SAMPLE_FMT_S16P;
    case EMF_LPCM32P: return AV_SAMPLE_FMT_S32P;
    case EMF_LPCMFP:  return AV_SAMPLE_FMT_FLTP;
    default: return AV_SAMPLE_FMT_NONE;
    }
}

int toAVFormat(ERawMediaFormat emf) {
    if (vnxvideo_emf_is_video(emf))
        return toAVPixelFormat(emf);
    else if (vnxvideo_emf_is_audio(emf))
        return toAVSampleFormat(emf);
    else
        return -1;
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

int bitsPerSampleByAVSampleFormat(AVSampleFormat format) {
    switch (format) {
    case AV_SAMPLE_FMT_S16: return 16;
    case AV_SAMPLE_FMT_S32: return 32;
    case AV_SAMPLE_FMT_FLT: return 32;
    case AV_SAMPLE_FMT_S16P: return 16;
    case AV_SAMPLE_FMT_S32P: return 32;
    case AV_SAMPLE_FMT_FLTP: return 32;
    default: return 0;
    }
}

bool isPlanarAudioFormat(AVSampleFormat format) {
    switch (format) {
    case AV_SAMPLE_FMT_S16:
    case AV_SAMPLE_FMT_S32:
    case AV_SAMPLE_FMT_FLT: return false;
    case AV_SAMPLE_FMT_S16P:
    case AV_SAMPLE_FMT_S32P:
    case AV_SAMPLE_FMT_FLTP: return true;
    default: return false;
    }
}

CAvcodecRawSample::CAvcodecRawSample() {
    m_frame.reset(av_frame_alloc(), [](AVFrame* f) { av_frame_free(&f); });
}
CAvcodecRawSample::CAvcodecRawSample(const AVFrame* f) {
    m_frame.reset(av_frame_clone(f), [](AVFrame* f) { av_frame_free(&f); });
}
CAvcodecRawSample::CAvcodecRawSample(std::shared_ptr<AVFrame> f): m_frame(f) {
}

bool avfrmIsVideo(AVFrame* frm) {
    return frm->width > 0 && frm->height > 0 && (AVPixelFormat)frm->format != AV_PIX_FMT_NONE;
}
bool avfrmIsAudio(AVFrame* frm) {
    return frm->channels > 0 && frm->sample_rate > 0 && (AVSampleFormat)frm->format != AV_SAMPLE_FMT_NONE;
}

VnxVideo::IRawSample* CAvcodecRawSample::Dup() {
    return new CAvcodecRawSample(m_frame);
}
void CAvcodecRawSample::GetFormat(ERawMediaFormat &emf, int &x, int &y) {
    if (avfrmIsVideo(m_frame.get())) {
        emf = fromAVPixelFormat((AVPixelFormat)m_frame->format);
        x = m_frame->width;
        y = m_frame->height;
    }
    else if (avfrmIsAudio(m_frame.get())) {
        emf = fromAVSampleFormat((AVSampleFormat)m_frame->format);
        x = m_frame->nb_samples;
        y = m_frame->channels;
    }
}
void CAvcodecRawSample::GetData(int* strides, uint8_t** planes) {
    int nplanes=0;
    if (avfrmIsVideo(m_frame.get()))
        nplanes = nplanesByAVPixelFormat((AVPixelFormat)m_frame->format);
    else if (avfrmIsAudio(m_frame.get())) {
        if (isPlanarAudioFormat((AVSampleFormat)m_frame->format))
            nplanes = m_frame->channels;
        else
            nplanes = 1;
    }
    memcpy(strides, m_frame->linesize, nplanes * sizeof(int));
    memcpy(planes, m_frame->data, nplanes * sizeof(uint8_t*));
}
AVFrame* CAvcodecRawSample::GetAVFrame() {
    return m_frame.get();
}

std::map<VnxVideo::ECodecImpl, std::string> g_codecImplementations;
std::mutex g_codecImplementationsMutex;

void enumHwDevices() {
    g_codecImplementations.clear();

    const char* const hwDecoderEnv = getenv("VNX_HW_DECODER");
    const char* const hwEncoderEnv = getenv("VNX_HW_ENCODER");
    // don't even attempt to probe for hw accelerated codecs
    // if both of the above variables are set to 0.
    if (hwDecoderEnv != 0 && hwEncoderEnv != 0 && strncmp(hwDecoderEnv, "0", 1) == 0 && strncmp(hwEncoderEnv, "0", 1) == 0)
        return;

#if defined(_WIN64) || defined(__linux__)
    for (AVHWDeviceType t = av_hwdevice_iterate_types(AV_HWDEVICE_TYPE_NONE);
        t != AV_HWDEVICE_TYPE_NONE;
        t = av_hwdevice_iterate_types(t)) {
        AVBufferRef *hw = nullptr;
        int res = av_hwdevice_ctx_create(&hw, t, nullptr, nullptr, 0);
        if (res < 0)
            continue;
        else
            av_buffer_unref(&hw);
        VnxVideo::ECodecImpl eci;
        if (t == AV_HWDEVICE_TYPE_QSV)
            eci=VnxVideo::ECodecImpl::ECI_QSV;
        else if (t == AV_HWDEVICE_TYPE_CUDA)
            eci=VnxVideo::ECodecImpl::ECI_CUDA;
        else if (t == AV_HWDEVICE_TYPE_D3D11VA)
            eci=VnxVideo::ECodecImpl::ECI_D3D11VA;
        else if (t == AV_HWDEVICE_TYPE_VAAPI)
            eci=VnxVideo::ECodecImpl::ECI_VAAPI;
        else if (t == AV_HWDEVICE_TYPE_RKMPP)
            eci = VnxVideo::ECodecImpl::ECI_RKMPP;
        else 
            continue;
        g_codecImplementations[eci] = av_hwdevice_get_type_name(t);
    }
    std::stringstream ss;
    ss << "Supported video encoder/decoder devices: cpu";
    for (auto i : g_codecImplementations)
        ss << ", " << i.second;
    VNXVIDEO_LOG(VNXLOG_DEBUG, "vnxvideo") << ss.str();
#endif
}

bool isCodecImplSupported(VnxVideo::ECodecImpl eci) {
    std::unique_lock<std::mutex> lock(g_codecImplementationsMutex);
    static bool g_codecImplementationsInitialized = false;
    if (!g_codecImplementationsInitialized) {
        enumHwDevices();
        g_codecImplementationsInitialized = true;
    }
    return g_codecImplementations.find(eci) != g_codecImplementations.end();
}

void checkFramesContext(AVCodecContext& cc, int width, int height, AVPixelFormat hwpixfmt) {
    if (cc.hw_device_ctx == nullptr)
        return;
    if (cc.hw_frames_ctx != nullptr)
        return;
#if defined(_WIN64) || defined(__linux__)
    AVBufferRef *hw_frames_ref = av_hwframe_ctx_alloc(cc.hw_device_ctx);
    AVHWFramesContext* frames_ctx = (AVHWFramesContext*)hw_frames_ref->data;
    frames_ctx->format = hwpixfmt;
    frames_ctx->sw_format = AV_PIX_FMT_NV12; // AV_PIX_FMT_YUV420P; //
    frames_ctx->width = width;
    frames_ctx->height = height;
    frames_ctx->initial_pool_size = 16;
    int res = av_hwframe_ctx_init(hw_frames_ref);
    if (res != 0) {
        VNXVIDEO_LOG(VNXLOG_INFO, "ffmpeg") << "checkFramesContext: av_hwframe_ctx_init failed: " << res << ": " << fferr2str(res);
        av_buffer_unref(&hw_frames_ref);
    }
    else {
        cc.hw_frames_ctx = hw_frames_ref; // ownership transferred
    }
#endif
}
