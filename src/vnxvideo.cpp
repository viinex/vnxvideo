#include <sstream>
#include <vector>
#include <sstream>
#include <codecvt>
#include <locale>
#include "json.hpp"
#include "jget.h"
#include <ipp.h>

using json = nlohmann::json;

#include "vnxvideo.h"

#include "vnxvideoimpl.h"
#include "vnxvideologimpl.h"

#include "RawSample.h"
#include "BufferImpl.h"
#include "VmsChannelSelector.h"

namespace NVnxVideoLogImpl {
    vnxvideo_log_t g_logHandler = nullptr;
    void* g_logUsrptr = 0;
    ELogLevel g_maxLogLevel = (ELogLevel)0;
}

void vnxvideo_init_ffmpeg(ELogLevel level);

void vnx_atexit() {
}

int vnxvideo_init(vnxvideo_log_t log_handler, void* usrptr, ELogLevel max_level) {

    if (nullptr != NVnxVideoLogImpl::g_logHandler) {
        return vnxvideo_err_invalid_parameter;
    }
    NVnxVideoLogImpl::g_logHandler = log_handler;
    NVnxVideoLogImpl::g_logUsrptr = usrptr;
    NVnxVideoLogImpl::g_maxLogLevel = max_level;

#ifndef __aarch64__
    IppStatus ipps=ippInit();
    if (ipps != ippStsNoErr && ipps != ippStsNonIntelCpu) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Failed to initialize IPP libraries: " << ipps;
        return vnxvideo_err_external_api;
    }
#endif

    vnxvideo_init_ffmpeg(max_level);

#ifdef _DEBUG
    atexit(vnx_atexit);
#endif

    return vnxvideo_err_ok;
}

int vnxvideo_manager_dshow_create(vnxvideo_manager_t* mgr) {
#if _WIN32
    VnxVideo::IVideoDeviceManager* dm = VnxVideo::CreateVideoDeviceManager_DirectShow();
    mgr->ptr = dm;
    return vnxvideo_err_ok;
#else
    return vnxvideo_err_not_implemented;
#endif
}
int vnxvideo_manager_v4l_create(vnxvideo_manager_t* mgr) {
#if _WIN32
    return vnxvideo_err_not_implemented;
#else
    VnxVideo::IVideoDeviceManager* dm = VnxVideo::CreateVideoDeviceManager_V4L();
    mgr->ptr = dm;
    return vnxvideo_err_ok;
#endif
}
void vnxvideo_manager_free(vnxvideo_manager_t mgr) {
    auto m = reinterpret_cast<VnxVideo::IVideoDeviceManager*>(mgr.ptr);
    delete m;
}

int vnxvideo_enumerate_video_sources(vnxvideo_manager_t mgr, int details, char* json_buffer, int buffer_size) {
    try {
        auto m = reinterpret_cast<VnxVideo::IVideoDeviceManager*>(mgr.ptr);
        VnxVideo::TDevices dev;
        m->EnumerateDevices(!!details, dev);
        std::stringstream wss;
        std::vector<json> res;
        std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
        for (auto& d : dev) {
            json j = {
                { "address", d.first },
                { "name", d.second.first }
            };

            if (!!details) {
                std::vector<json> caps;
                for (auto& c : d.second.second) {
                    json jc;
                    std::stringstream ss(c);
                    ss >> jc;
                    caps.push_back(jc);
                }
                j["capabilities"] = caps;
            }

            res.emplace_back(j);
        }

        wss << res << std::ends;
        if (0 == json_buffer || 0 == buffer_size) {
            return (int)wss.str().size();
        }
        if (wss.str().size() >= (size_t)buffer_size) {
            return vnxvideo_err_invalid_parameter;
        }
        memcpy(json_buffer, wss.str().c_str(), wss.str().size());
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_enumerate_video_sources: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}

int vnxvideo_video_source_create(vnxvideo_manager_t mgr, const char* address, const char* mode, vnxvideo_videosource_t* src) {
    try {
        auto m = reinterpret_cast<VnxVideo::IVideoDeviceManager*>(mgr.ptr);
        src->ptr = m->CreateVideoSource(address, mode);
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_video_source_create: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}
void vnxvideo_video_source_free(vnxvideo_videosource_t src) {
    auto s = reinterpret_cast<VnxVideo::IVideoSource*>(src.ptr);
    delete s;
}

template<typename CPPType, typename CType>
int vnxvideo_template_rawvideo_subscribe(CType src,
    vnxvideo_on_frame_format_t handle_format, void* usrptr_format,
    vnxvideo_on_raw_sample_t handle_sample, void* usrptr_data) {
    try {
        auto s = reinterpret_cast<CPPType*>(src.ptr);
        if (handle_format && handle_sample) {
            auto hd = [=](VnxVideo::IRawSample* s, uint64_t ts) { handle_sample(usrptr_data, { s }, ts); };
            auto hf = [=](EColorspace csp, int w, int h) { handle_format(usrptr_format, csp, w, h); };
            s->Subscribe(hf, hd);
        }
        else
            s->Subscribe([](EColorspace csp, int w, int h) {},
                [](VnxVideo::IRawSample* s, uint64_t ts) {});
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_template_rawvideo_subscribe: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}

int vnxvideo_video_source_subscribe(vnxvideo_videosource_t src, 
    vnxvideo_on_frame_format_t handle_format, void* usrptr_format,
    vnxvideo_on_raw_sample_t handle_sample, void* usrptr_data) {
    return vnxvideo_template_rawvideo_subscribe<VnxVideo::IVideoSource>(src, 
        handle_format, usrptr_format, handle_sample, usrptr_data);
}
int vnxvideo_video_source_start(vnxvideo_videosource_t src) {
    try {
        auto s = reinterpret_cast<VnxVideo::IVideoSource*>(src.ptr);
        s->Run();
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_video_source_start: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}
int vnxvideo_video_source_stop(vnxvideo_videosource_t src) {
    try {
        auto s = reinterpret_cast<VnxVideo::IVideoSource*>(src.ptr);
        s->Stop();
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_video_source_stop: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}

int vnxvideo_raw_sample_dup(vnxvideo_raw_sample_t src, vnxvideo_raw_sample_t* dst) {
    auto s = reinterpret_cast<VnxVideo::IRawSample*>(src.ptr);
    dst->ptr = s->Dup();
    return vnxvideo_err_ok;
}
int vnxvideo_raw_sample_copy(vnxvideo_raw_sample_t src, vnxvideo_raw_sample_t* dst) {
    try {
        auto s = reinterpret_cast<VnxVideo::IRawSample*>(src.ptr);
        EColorspace csp;
        int w, h;
        int strides[4];
        uint8_t* planes[4];
        s->GetFormat(csp, w, h);
        s->GetData(strides, planes);
        dst->ptr = new CRawSample(csp, w, h, strides, planes, true);
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_raw_sample_copy: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}

int vnxvideo_raw_sample_allocate(EColorspace csp, int width, int height, vnxvideo_raw_sample_t* dst) {
    try {
        dst->ptr = new CRawSample(csp, width, height, nullptr, nullptr, true);
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_raw_sample_allocate: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}

int vnxvideo_raw_sample_wrap(EColorspace csp, int width, int height,
    int* strides, uint8_t **planes, vnxvideo_raw_sample_t* dst) {
    try {
        dst->ptr = new CRawSample(csp, width, height, strides, planes, false);
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_raw_sample_wrap: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}

void vnxvideo_raw_sample_free(vnxvideo_raw_sample_t sample) {
    auto s = reinterpret_cast<VnxVideo::IRawSample*>(sample.ptr);
    delete s;
}
int vnxvideo_raw_sample_get_data(vnxvideo_raw_sample_t sample, int* strides, uint8_t **planes) {
    auto s = reinterpret_cast<VnxVideo::IRawSample*>(sample.ptr);
    try {
        s->GetData(strides, planes);
        return vnxvideo_err_ok;
    }
    catch(const std::runtime_error& e){
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_raw_sample_get_data: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}
int vnxvideo_raw_sample_get_format(vnxvideo_raw_sample_t sample, EColorspace *csp, int *width, int *height) {
    auto s = reinterpret_cast<VnxVideo::IRawSample*>(sample.ptr);
    s->GetFormat(*csp, *width, *height);
    return vnxvideo_err_ok;
}

int vnxvideo_buffer_dup(vnxvideo_buffer_t src, vnxvideo_buffer_t* dst) {
    auto s = reinterpret_cast<VnxVideo::IBuffer*>(src.ptr);
    dst->ptr = s->Dup();
    return vnxvideo_err_ok;
}
void vnxvideo_buffer_free(vnxvideo_buffer_t sample) {
    auto s = reinterpret_cast<VnxVideo::IBuffer*>(sample.ptr);
    delete s;
}
int vnxvideo_buffer_get_data(vnxvideo_buffer_t sample, uint8_t* *data, int* size) {
    auto s = reinterpret_cast<VnxVideo::IBuffer*>(sample.ptr);
    s->GetData(*data, *size);
    return vnxvideo_err_ok;
}

void vnxvideo_rawproc_free(vnxvideo_rawproc_t proc) {
    auto p = reinterpret_cast<VnxVideo::IRawProc*>(proc.ptr);
    delete p;
}
int vnxvideo_rawproc_set_format(vnxvideo_rawproc_t proc, EColorspace csp, int width, int height) {
    auto p = reinterpret_cast<VnxVideo::IRawProc*>(proc.ptr);
    try {
        p->SetFormat(csp, width, height);
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_rawproc_set_format: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}
int vnxvideo_rawproc_process(vnxvideo_rawproc_t proc, vnxvideo_raw_sample_t sample, uint64_t timestamp) {
    auto p = reinterpret_cast<VnxVideo::IRawProc*>(proc.ptr);
    try {
        auto s = reinterpret_cast<VnxVideo::IRawSample*>(sample.ptr);
        p->Process(s, timestamp);
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on rawproc_process: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}
int vnxvideo_rawproc_flush(vnxvideo_rawproc_t proc) {
    auto p = reinterpret_cast<VnxVideo::IRawProc*>(proc.ptr);
    try {
        p->Flush();
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_rawproc_flush: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}

namespace {
    class CRawProcFromCallbacks: public virtual VnxVideo::IRawProc {
    public:
        CRawProcFromCallbacks(vnxvideo_on_frame_format_t onFormat, void* usrptrOnFormat,
            vnxvideo_on_raw_sample_t onSample, void* usrptrOnSample)
            : m_onFormat(onFormat)
            , m_usrptrOnFormat(usrptrOnFormat)
            , m_onSample(onSample)
            , m_usrptrOnSample(usrptrOnSample)
        {
        }
        virtual void SetFormat(ERawMediaFormat csp, int width, int height) {
            m_onFormat(m_usrptrOnFormat, csp, width, height);
        }
        virtual void Process(VnxVideo::IRawSample* sample, uint64_t timestamp) {
            m_onSample(m_usrptrOnSample, { sample }, timestamp);
        }
        virtual void Flush() {}
    private:
        vnxvideo_on_frame_format_t const m_onFormat;
        void* const m_usrptrOnFormat;
        vnxvideo_on_raw_sample_t const m_onSample;
        void* const m_usrptrOnSample;
    };
}
int vnxvideo_rawproc_create_from_callbacks(
    vnxvideo_on_frame_format_t onFormat, void* usrptrOnFormat,
    vnxvideo_on_raw_sample_t onSample, void* usrptrOnSample,
    vnxvideo_rawproc_t* proc) {
    try {
        proc->ptr = static_cast<VnxVideo::IRawProc*>(new CRawProcFromCallbacks(onFormat, usrptrOnFormat, onSample, usrptrOnSample));
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_rawproc_create_from_callbacks: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}


int vnxvideo_h264_encoder_create(const char* json_config, vnxvideo_encoder_t* encoder) {
    try {
        json j;
        std::string s(json_config);
        std::stringstream ss(s);
        ss >> j;
        std::string profile(jget<std::string>(j, "profile"));
        std::string preset(jget<std::string>(j,"preset"));
        int fps(jget<int>(j,"framerate", 25));
        std::string quality(jget<std::string>(j, "quality", "normal"));
        std::string type(jget<std::string>(j, "type", "auto"));
        if (type == "cpu") {
            VnxVideo::PMediaEncoder enc(VnxVideo::CreateVideoEncoder_OpenH264(profile.c_str(), preset.c_str(), fps, quality.c_str()));
            encoder->ptr = VnxVideo::CreateAsyncVideoEncoder(enc);
            return vnxvideo_err_ok;
        }
        else if (type == "qsv") {
            encoder->ptr = VnxVideo::CreateVideoEncoder_FFmpeg(profile.c_str(), preset.c_str(), fps, quality.c_str(), VnxVideo::ECodecImpl::ECI_QSV);
            return vnxvideo_err_ok;
        }
        else if (type == "cuda") {
            encoder->ptr = VnxVideo::CreateVideoEncoder_FFmpeg(profile.c_str(), preset.c_str(), fps, quality.c_str(), VnxVideo::ECodecImpl::ECI_CUDA);
            return vnxvideo_err_ok;
        }
#if defined(__aarch64__)
        else if (type == "rkmpp") {
            encoder->ptr = VnxVideo::CreateVideoEncoder_FFmpeg(profile.c_str(), preset.c_str(), fps, quality.c_str(), VnxVideo::ECodecImpl::ECI_RKMPP);
            return vnxvideo_err_ok;
        }
#endif
        else if (type == "vaapi") {
            encoder->ptr = VnxVideo::CreateVideoEncoder_FFmpeg(profile.c_str(), preset.c_str(), fps, quality.c_str(), VnxVideo::ECodecImpl::ECI_VAAPI);
            return vnxvideo_err_ok;
        }
        else if (type == "auto") {
            try {
                encoder->ptr = VnxVideo::CreateVideoEncoder_FFmpeg_Auto(profile.c_str(), preset.c_str(), fps, quality.c_str());
                if (encoder->ptr == nullptr) {
                    VnxVideo::PMediaEncoder enc(VnxVideo::CreateVideoEncoder_OpenH264(profile.c_str(), preset.c_str(), fps, quality.c_str()));
                    encoder->ptr = VnxVideo::CreateAsyncVideoEncoder(enc);
                }
            }
            catch (const VnxVideo::XHWDeviceNotSupported&) {
                VNXVIDEO_LOG(VNXLOG_WARNING, "vnxvideo") << "Failed to create hw accelerated video encoder, CPU encoder is going to be used";
                VnxVideo::PMediaEncoder enc(VnxVideo::CreateVideoEncoder_OpenH264(profile.c_str(), preset.c_str(), fps, quality.c_str()));
                encoder->ptr = VnxVideo::CreateAsyncVideoEncoder(enc);
            }
            return vnxvideo_err_ok;
        }
        else
            throw std::runtime_error("unsupported encoder type");
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_h264_encoder_create: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}
vnxvideo_rawproc_t vnxvideo_encoder_to_rawproc(vnxvideo_h264_encoder_t e) {
    return vnxvideo_rawproc_t{
        static_cast<VnxVideo::IRawProc*>(reinterpret_cast<VnxVideo::IMediaEncoder*>(e.ptr))
    };
}
int vnxvideo_encoder_subscribe(vnxvideo_encoder_t encoder,
    vnxvideo_on_buffer_t handle_data, void* usrptr) {
    try {
        auto e = reinterpret_cast<VnxVideo::IMediaEncoder*>(encoder.ptr);
        if (handle_data != nullptr)
            e->Subscribe([=](VnxVideo::IBuffer* b, uint64_t ts) { handle_data(usrptr, vnxvideo_buffer_t{ b }, ts); });
        else
            e->Subscribe([](VnxVideo::IBuffer* b, uint64_t ts) {});
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_h264_encoder_subscribe: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}


int vnxvideo_h264_source_subscribe(vnxvideo_h264_source_t source,
    vnxvideo_on_buffer_t handle_data, void* usrptr) {
    try {
        auto s = reinterpret_cast<VnxVideo::IH264VideoSource*>(source.ptr);
        if (handle_data != nullptr)
            s->Subscribe([=](VnxVideo::IBuffer* b, uint64_t ts) { handle_data(usrptr, vnxvideo_buffer_t{ b }, ts); });
        else
            s->Subscribe([](VnxVideo::IBuffer* b, uint64_t ts) {});
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_h264_source_subscribe: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}
int vnxvideo_h264_source_events_subscribe(vnxvideo_h264_source_t source,
    vnxvideo_on_json_t handle_event, void* usrptr) {
    try {
        auto s = reinterpret_cast<VnxVideo::IH264VideoSource*>(source.ptr);
        if (handle_event != nullptr)
            s->Subscribe([=](const std::string& json, uint64_t ts) { 
                handle_event(usrptr, json.c_str(), (int)json.size(), ts);
            });
        else
            s->Subscribe([](const std::string&, uint64_t) {});
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_h264_source_events_subscribe: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}
void vnxvideo_h264_source_free(vnxvideo_h264_source_t source) {
    auto s = reinterpret_cast<VnxVideo::IH264VideoSource*>(source.ptr);
    delete s;
}
int vnxvideo_h264_source_start(vnxvideo_h264_source_t source) {
    try {
        auto s = reinterpret_cast<VnxVideo::IH264VideoSource*>(source.ptr);
        s->Run();
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_h264_source_start: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}
int vnxvideo_h264_source_stop(vnxvideo_h264_source_t source) {
    try {
        auto s = reinterpret_cast<VnxVideo::IH264VideoSource*>(source.ptr);
        s->Stop();
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_h264_source_stop: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}

int vnxvideo_media_source_subscribe(vnxvideo_media_source_t source,
    EMediaSubtype media_subtype, vnxvideo_on_buffer_t handle_data, void* usrptr) {
    try {
        auto s = reinterpret_cast<VnxVideo::IMediaSource*>(source.ptr);
        if (handle_data != nullptr)
            s->SubscribeMedia(media_subtype, [=](VnxVideo::IBuffer* b, uint64_t ts) { handle_data(usrptr, vnxvideo_buffer_t{ b }, ts); });
        else
            s->SubscribeMedia(media_subtype, [](VnxVideo::IBuffer* b, uint64_t ts) {});
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_media_source_subscribe: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}
int vnxvideo_media_source_events_subscribe(vnxvideo_media_source_t source,
    vnxvideo_on_json_t handle_event, void* usrptr) {
    try {
        auto s = reinterpret_cast<VnxVideo::IMediaSource*>(source.ptr);
        if (handle_event != nullptr)
            s->SubscribeJson([=](const std::string& json, uint64_t ts) {
            handle_event(usrptr, json.c_str(), (int)json.size(), ts);
                });
        else
            s->SubscribeJson([](const std::string&, uint64_t) {});
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_media_source_events_subscribe: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}
void vnxvideo_media_source_free(vnxvideo_media_source_t source) {
    auto s = reinterpret_cast<VnxVideo::IMediaSource*>(source.ptr);
    delete s;
}
int vnxvideo_media_source_start(vnxvideo_media_source_t source) {
    try {
        auto s = reinterpret_cast<VnxVideo::IMediaSource*>(source.ptr);
        s->Run();
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_media_source_start: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}
int vnxvideo_media_source_stop(vnxvideo_media_source_t source) {
    try {
        auto s = reinterpret_cast<VnxVideo::IMediaSource*>(source.ptr);
        s->Stop();
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_media_source_stop: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}
int vnxvideo_media_source_enum_mediatypes(vnxvideo_media_source_t source,
    EMediaSubtype* buffer, int buffer_size_bytes, int* count) {
    try {
        auto s = reinterpret_cast<VnxVideo::IMediaSource*>(source.ptr);
        auto res = s->EnumMediatypes();
        if (buffer_size_bytes < res.size() * sizeof(EMediaSubtype)) {
            return (int)res.size() * sizeof(EMediaSubtype);
        }
        *count = (int)res.size();
        std::copy(res.begin(), res.end(), buffer);
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_media_source_stop: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}
int vnxvideo_media_source_get_extradata(vnxvideo_media_source_t source,
    EMediaSubtype media_subtype, vnxvideo_buffer_t* buffer) {
    try {
        auto s = reinterpret_cast<VnxVideo::IMediaSource*>(source.ptr);
        buffer->ptr = s->GetExtradata(media_subtype);
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_media_source_stop: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}


vnxvideo_h264_source_t vnxvideo_media_source_to_h264(vnxvideo_media_source_t src) {
    vnxvideo_h264_source_t res;
    res.ptr = VnxVideo::CreateH264VideoSourceFromMediaSource(reinterpret_cast<VnxVideo::IMediaSource*>(src.ptr));
    return res;
}
vnxvideo_media_source_t vnxvideo_h264_source_to_media(vnxvideo_h264_source_t src) {
    vnxvideo_media_source_t res;
    res.ptr = VnxVideo::CreateMediaSourceFromH264VideoSource(reinterpret_cast<VnxVideo::IH264VideoSource*>(src.ptr));
    return res;
}



int vnxvideo_composer_create(const char* json_config, vnxvideo_composer_t* composer) {
    try {
        json j;
        std::string s(json_config);
        std::stringstream ss(s);
        ss >> j;
        int left(jget<int>(j, "left", 0));
        int top(jget<int>(j, "top", 0));
        std::vector<uint8_t> colorkey(jget<std::vector<uint8_t> >(j, "colorkey"));
        if (colorkey.size() < 4)
            colorkey.resize(4, 0);
        composer->ptr = VnxVideo::CreateComposer(&colorkey[0], left, top);
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_composer_create: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}
vnxvideo_rawproc_t vnxvideo_composer_to_rawproc(vnxvideo_composer_t c) {
    return vnxvideo_rawproc_t{
        static_cast<VnxVideo::IRawProc*>(reinterpret_cast<VnxVideo::IComposer*>(c.ptr))
    };
}
int vnxvideo_composer_set_overlay(vnxvideo_composer_t c, vnxvideo_raw_sample_t s) {
    try {
        auto composer = reinterpret_cast<VnxVideo::IComposer*>(c.ptr);
        auto sample = reinterpret_cast<VnxVideo::IRawSample*>(s.ptr);
        composer->SetOverlay(sample);
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_composer_set_overlay: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}

int vnxvideo_raw_sample_from_bmp(const uint8_t* data, int size, vnxvideo_raw_sample_t* sample) {
    try {
        sample->ptr = VnxVideo::ParseBMP(data, size);
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_raw_sample_from_bmp: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}


int vnxvideo_h264_decoder_create(vnxvideo_decoder_t* decoder) {
    try {
        const char* const hwDecoderEnv = getenv("VNX_HW_DECODER");
        if (hwDecoderEnv != 0 && strncmp(hwDecoderEnv, "1", 1) == 0)
            decoder->ptr = VnxVideo::CreateVideoDecoder_FFmpegH264(false);
        else {
            const char* const swDecoderEnv = getenv("VNX_SW_DECODER");
            if (swDecoderEnv != 0 && strcmp(swDecoderEnv, "ffmpeg") == 0)
                decoder->ptr = VnxVideo::CreateVideoDecoder_FFmpegH264(true);
            else
                decoder->ptr = VnxVideo::CreateVideoDecoder_OpenH264();
        }
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_h264_decoder_create: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}
int vnxvideo_h264_sw_decoder_create(vnxvideo_decoder_t* decoder) {
    try {
        const char* const swDecoderEnv = getenv("VNX_SW_DECODER");
        if (swDecoderEnv != 0 && strcmp(swDecoderEnv, "ffmpeg") == 0)
            decoder->ptr = VnxVideo::CreateVideoDecoder_FFmpegH264(true);
        else
            decoder->ptr = VnxVideo::CreateVideoDecoder_OpenH264();
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_h264_sw_decoder_create: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}

int vnxvideo_hevc_decoder_create(vnxvideo_decoder_t* decoder) {
    try {
        decoder->ptr = VnxVideo::CreateVideoDecoder_FFmpegHEVC(false);
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_hevc_decoder_create: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}
int vnxvideo_hevc_sw_decoder_create(vnxvideo_decoder_t* decoder) {
    try {
        decoder->ptr = VnxVideo::CreateVideoDecoder_FFmpegHEVC(true);
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_sw_hevc_decoder_create: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}

void vnxvideo_decoder_free(vnxvideo_decoder_t decoder) {
    auto p = reinterpret_cast<VnxVideo::IMediaDecoder*>(decoder.ptr);
    delete p;
}
int vnxvideo_decoder_subscribe(vnxvideo_decoder_t decoder,
    vnxvideo_on_frame_format_t handle_format, void* usrptr_format,
    vnxvideo_on_raw_sample_t handle_sample, void* usrptr_data) {
    return vnxvideo_template_rawvideo_subscribe<VnxVideo::IMediaDecoder>(decoder, 
        handle_format, usrptr_format, handle_sample, usrptr_data);
}
int vnxvideo_decoder_decode(vnxvideo_decoder_t decoder, vnxvideo_buffer_t buffer, uint64_t timestamp) {
    try {
        auto d = reinterpret_cast<VnxVideo::IMediaDecoder*>(decoder.ptr);
        auto b = reinterpret_cast<VnxVideo::IBuffer*>(buffer.ptr);
        d->Decode(b, timestamp);
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_decoder_decode: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}
int vnxvideo_decoder_flush(vnxvideo_decoder_t decoder) {
    try {
        auto d = reinterpret_cast<VnxVideo::IMediaDecoder*>(decoder.ptr);
        d->Flush();
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_decoder_flush: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}

int vnxvideo_analytics_create(const char* json_config, vnxvideo_analytics_t* analytics) {
    try {
        json j;
        std::string s(json_config);
        std::stringstream ss(s);
        ss >> j;
        std::string type(jget<std::string>(j, "type"));
        std::vector<float> roi(jget<std::vector<float> >(j, "roi", { 0.0f,0.0f,1.0f,1.0f })); // L,T,R,B

        if (roi[0]<0 || roi[1]<0 || roi[0]>1.0 || roi[1]>1.0 ||
            roi[2]<0 || roi[3]<0 || roi[2]>1.0 || roi[3]>1.0 ||
            roi[0] >= roi[2] || roi[1] >= roi[3]) {
            throw std::runtime_error("incorrect ROI specified");
        }
        float framerate(jget<float>(j, "framerate", 0));
#ifndef __aarch64__
        if (type == "basic") {
            bool too_bright(jget<bool>(j, "too_bright"));
            bool too_dark(jget<bool>(j, "too_dark"));
            bool too_blurry(jget<bool>(j, "too_blurry"));
            float motion(jget<float>(j, "motion"));
            bool scene_change(jget<bool>(j, "scene_change"));
            analytics->ptr = VnxVideo::CreateAnalytics_Basic(roi, framerate, too_bright, too_dark, too_blurry, motion, scene_change);
        }
        else
#endif
            throw std::runtime_error("unknown analytics type: " + type);
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_analytics_create: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
    return vnxvideo_err_ok;
}
vnxvideo_rawproc_t vnxvideo_analytics_to_rawproc(vnxvideo_analytics_t analytics) {
    return vnxvideo_rawproc_t{
        static_cast<VnxVideo::IRawProc*>(reinterpret_cast<VnxVideo::IAnalytics*>(analytics.ptr))
    };
}
int vnxvideo_analytics_subscribe(vnxvideo_analytics_t analytics,
    vnxvideo_on_json_t handle_json, void* usrptr_json, // json for rare events
    vnxvideo_on_buffer_t handle_binary, void* usrptr_binary) {
    try {
        auto a = reinterpret_cast<VnxVideo::IAnalytics*>(analytics.ptr);
        if (handle_json && handle_binary) {
            auto hj = [=](const std::string& s, uint64_t ts) { handle_json(usrptr_json, s.c_str(), (int)s.size(), ts); };
            auto hb = [=](VnxVideo::IBuffer* b, uint64_t ts) { handle_binary(usrptr_binary, vnxvideo_buffer_t{ b }, ts); };
            a->Subscribe(hj, hb);
        }
        else
            a->Subscribe([](const std::string& s, uint64_t ts) {},
                [](VnxVideo::IBuffer* b, uint64_t ts) {});
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_analytics_subscribe: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}

int vnxvideo_rawproc_chain_create(vnxvideo_rawproc_chain_t* chain) {
    try {
        chain->ptr = VnxVideo::CreateRawProcChain();
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_rawproc_chain_create: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
    return vnxvideo_err_ok;
}
VNXVIDEO_DECLSPEC vnxvideo_rawproc_t vnxvideo_rawproc_chain_to_rawproc(vnxvideo_rawproc_chain_t chain) {
    return vnxvideo_rawproc_t{
        static_cast<VnxVideo::IRawProc*>(reinterpret_cast<VnxVideo::IRawProcChain*>(chain.ptr))
    };
}
VNXVIDEO_DECLSPEC int vnxvideo_rawproc_chain_link(vnxvideo_rawproc_chain_t chain, vnxvideo_rawproc_t link) {
    try {
        auto c = reinterpret_cast<VnxVideo::IRawProcChain*>(chain.ptr);
        auto l = reinterpret_cast<VnxVideo::IRawProc*>(link.ptr);
        c->Link(l);
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_rawproc_chain_link: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
    return vnxvideo_err_ok;
}

int vnxvideo_rawtransform_create(const char* json_config, vnxvideo_rawtransform_t* transform) {
    try {
        json j;
        std::string s(json_config);
        std::stringstream ss(s);
        ss >> j;
#ifndef __aarch64__
        transform->ptr = VnxVideo::CreateRawTransform(j);
#else
	throw std::runtime_error("Raw transforms not implemented on aarch64");
#endif
        return 0;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_rawtransform_create: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
    return vnxvideo_err_ok;
}
vnxvideo_rawproc_t vnxvideo_rawtransform_to_rawproc(vnxvideo_rawtransform_t e) {
    return vnxvideo_rawproc_t{
        static_cast<VnxVideo::IRawProc*>(reinterpret_cast<VnxVideo::IRawTransform*>(e.ptr))
    };
}
int vnxvideo_rawtransform_subscribe(vnxvideo_rawtransform_t transform,
    vnxvideo_on_frame_format_t handle_format, void* usrptr_format,
    vnxvideo_on_raw_sample_t handle_sample, void* usrptr_data) {
    return vnxvideo_template_rawvideo_subscribe<VnxVideo::IRawTransform>(transform, 
        handle_format, usrptr_format, handle_sample, usrptr_data);
}

int vnxvideo_imganalytics_create(const char* json_config, vnxvideo_imganalytics_t *ian) {
    return vnxvideo_err_not_implemented; // its all in vnxcv library. and it requires "authentication"
}
void vnxvideo_imganalytics_free(vnxvideo_imganalytics_t ian) {
    auto a = reinterpret_cast<VnxVideo::IImageAnalytics*>(ian.ptr);
    delete a;
}
int vnxvideo_imganalytics_set_format(vnxvideo_imganalytics_t ian, EColorspace csp, int width, int height) {
    auto a = reinterpret_cast<VnxVideo::IImageAnalytics*>(ian.ptr);
    try {
        a->SetFormat(csp, width, height);
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_imganalytics_set_format: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}
int vnxvideo_imganalytics_process(vnxvideo_imganalytics_t ian, vnxvideo_raw_sample_t sample,
    char* /*out*/json_buffer, int* /*inout*/ buffer_size) {
    if (buffer_size == nullptr || json_buffer == nullptr)
        return vnxvideo_err_invalid_parameter;
    auto a = reinterpret_cast<VnxVideo::IImageAnalytics*>(ian.ptr);
    try {
        auto s = reinterpret_cast<VnxVideo::IRawSample*>(sample.ptr);
        std::string r(a->Process(s));
        if (*buffer_size <= r.size()) {
            VNXVIDEO_LOG(VNXLOG_DEBUG, "vnxvideo") << "Error in vnxvideo_imganalytics_process: out buffer to small to hold the result";
        }
        else {
            memcpy(json_buffer, r.c_str(), r.size());
        }
        *buffer_size = (int)r.size();
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_imganalytics_process: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}


int vnxvideo_renderer_create(int refresh_rate, vnxvideo_renderer_t* renderer) {
    try {
        renderer->ptr = VnxVideo::CreateRenderer(refresh_rate);
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_renderer_create: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}
VNXVIDEO_DECLSPEC vnxvideo_videosource_t vnxvideo_renderer_to_videosource(vnxvideo_renderer_t renderer) {
    vnxvideo_videosource_t res;
    res.ptr = reinterpret_cast<VnxVideo::IRenderer*>(renderer.ptr);
    return res;
}

VNXVIDEO_DECLSPEC int vnxvideo_renderer_create_input(vnxvideo_renderer_t renderer, 
                        int index, const char* transform_json, vnxvideo_rawproc_t* input) {
    VnxVideo::IRenderer* r(reinterpret_cast<VnxVideo::IRenderer*>(renderer.ptr));
    try {
        VnxVideo::PRawTransform transform;
        if (nullptr != transform_json && 0 != strlen(transform_json)) {
            json j;
            std::string s(transform_json);
            std::stringstream ss(s);
            ss >> j;
#ifndef __aarch64__
	    transform.reset(VnxVideo::CreateRawTransform(j));
#else
	    throw std::runtime_error("Raw transforms not supported on aarch64");
#endif
        }
        input->ptr = r->CreateInput(index, transform);
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_renderer_create_input: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}

static void deserializeLayout(const char* json_layout, VnxVideo::TLayout& layout) {
    json j;
    std::string s(json_layout);
    std::stringstream ss(s);
    ss >> j;
    if (!j.is_array())
        throw std::runtime_error("deserializeLayout(): top-level value should be a json array of viewport descriptions");
    for (auto jj : j) {
        VnxVideo::Viewport v;
        v.input = jget<int>(jj, "input", -1);
        std::vector<uint8_t> b;
        if (mjget<std::vector<uint8_t> >(jj, "border", b) && b.size() == 3) {
            v.border = true;
            for (int k = 0; k < 3; ++k)
                v.border_rgb[k] = b[k];
        }
        else
            v.border = false;
        v.src_left = jget<float>(jj, "src_left", 0.0f);
        v.src_top = jget<float>(jj, "src_top", 0.0f);
        v.src_right = jget<float>(jj, "src_right", 1.0f);
        v.src_bottom = jget<float>(jj, "src_bottom", 1.0f);
        v.dst_left = jget<float>(jj, "dst_left");
        v.dst_top = jget<float>(jj, "dst_top");
        v.dst_right = jget<float>(jj, "dst_right");
        v.dst_bottom = jget<float>(jj, "dst_bottom");
        layout.push_back(v);
    }
}

static void deserializeAudioLayout(const char* json_layout, VnxVideo::TAudioLayout& layout) {
    json j;
    std::string s(json_layout);
    std::stringstream ss(s);
    ss >> j;
    if (!j.is_array())
        throw std::runtime_error("deserializeAudioLayout(): top-level value should be a json array of audio channel descriptions");
    for (auto jj : j) {
        VnxVideo::AudioInput a;
        a.input = jget<int>(jj, "input", -1);
        a.gain = jget<float>(jj, "gain", 1.0f);
        layout.push_back(a);
    }
}

VNXVIDEO_DECLSPEC int vnxvideo_renderer_update_layout(vnxvideo_renderer_t renderer,
    int width, int height, uint8_t* backgroundColor, vnxvideo_raw_sample_t backgroundImage, 
    vnxvideo_raw_sample_t nosignalImage, const char* layout) {
    VnxVideo::IRenderer* r(reinterpret_cast<VnxVideo::IRenderer*>(renderer.ptr));
    try {
        VnxVideo::IRawSample* bi = reinterpret_cast<VnxVideo::IRawSample*>(backgroundImage.ptr);
        VnxVideo::IRawSample* nsi = reinterpret_cast<VnxVideo::IRawSample*>(nosignalImage.ptr);
        VnxVideo::TLayout l;
        deserializeLayout(layout, l);
        r->UpdateLayout(width, height, backgroundColor, bi, nsi, l);
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_renderer_update_layout: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}

VNXVIDEO_DECLSPEC int vnxvideo_renderer_update_audio_layout(vnxvideo_renderer_t renderer,
    int sample_rate, int channels, const char* layout) {
    VnxVideo::IRenderer* r(reinterpret_cast<VnxVideo::IRenderer*>(renderer.ptr));
    try {
        VnxVideo::TAudioLayout l;
        deserializeAudioLayout(layout, l);
        r->UpdateAudioLayout(sample_rate, channels, l);
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_renderer_update_audio_layout: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}

VNXVIDEO_DECLSPEC int vnxvideo_renderer_set_background(vnxvideo_renderer_t renderer, 
    uint8_t* backgroundColor, vnxvideo_raw_sample_t backgroundImage) 
{
    VnxVideo::IRenderer* r(reinterpret_cast<VnxVideo::IRenderer*>(renderer.ptr));
    try {
        VnxVideo::IRawSample* bi = reinterpret_cast<VnxVideo::IRawSample*>(backgroundImage.ptr);
        r->SetBackground(backgroundColor, bi);
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_renderer_set_background_image: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}
VNXVIDEO_DECLSPEC int vnxvideo_renderer_set_nosignal(vnxvideo_renderer_t renderer,
    vnxvideo_raw_sample_t nosignalImage)
{
    VnxVideo::IRenderer* r(reinterpret_cast<VnxVideo::IRenderer*>(renderer.ptr));
    try {
        VnxVideo::IRawSample* nsi = reinterpret_cast<VnxVideo::IRawSample*>(nosignalImage.ptr);
        r->SetNosignal(nsi);
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_renderer_set_background_image: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}


VNXVIDEO_DECLSPEC int vnxvideo_with_shm_allocator_str(const char* name, int maxSizeMB, vnxvideo_action_t action, void* usrptr) {
    try {
        VnxVideo::WithPreferredShmAllocator(name, maxSizeMB, std::bind(action, usrptr));
        return 0;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_with_shm_allocator: " << e.what();
        return vnxvideo_err_external_api;
    }
}

VNXVIDEO_DECLSPEC int vnxvideo_with_shm_allocator_ptr(vnxvideo_allocator_t allocator, vnxvideo_action_t action, void* usrptr) {
    try {
        IShmAllocator* allocatorPtr = reinterpret_cast<IShmAllocator*>(allocator.ptr);
        WithPreferredShmAllocator(allocatorPtr, std::bind(action, usrptr));
        return 0;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_with_shm_allocator: " << e.what();
        return vnxvideo_err_external_api;
    }

}

VNXVIDEO_DECLSPEC void vnxvideo_shm_allocator_duplicate(vnxvideo_allocator_t* out) {
    IShmAllocator *allocator = GetPreferredShmAllocator();
    if (allocator!=nullptr)
        allocator = allocator->Dup();
    out->ptr = allocator;
}
VNXVIDEO_DECLSPEC void vnxvideo_shm_allocator_free(vnxvideo_allocator_t allocator) {
    delete reinterpret_cast<IShmAllocator*>(allocator.ptr);
}


VNXVIDEO_DECLSPEC int vnxvideo_local_client_create(const char* name, vnxvideo_videosource_t* out) {
    try {
        out->ptr = VnxVideo::CreateLocalVideoClient(name);
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_local_transport_client_create: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}
VNXVIDEO_DECLSPEC int vnxvideo_local_server_create(const char* name, int maxSizeMB, vnxvideo_rawproc_t* out) {
    try {
        out->ptr = VnxVideo::CreateLocalVideoProvider(name, maxSizeMB);
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_local_server_create: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}

nlohmann::json parseSelector(const std::string& channel_selector) {
    nlohmann::json js;
    std::string s(channel_selector);
    std::stringstream ss(s);
    ss >> js;
    return js;
}

VNXVIDEO_DECLSPEC int vnxvideo_vmsplugin_free(vnxvideo_vmsplugin_t vmsplugin) {
    VnxVideo::IVmsPlugin* vp(reinterpret_cast<VnxVideo::IVmsPlugin*>(vmsplugin.ptr));
    delete vp;
    return vnxvideo_err_ok;
}
VNXVIDEO_DECLSPEC int vnxvideo_vmsplugin_h264_source_create_live(vnxvideo_vmsplugin_t vmsplugin,
    const char* channel_selector, vnxvideo_h264_source_t* source) {
    VnxVideo::IVmsPlugin* vp(reinterpret_cast<VnxVideo::IVmsPlugin*>(vmsplugin.ptr));
    try {
        VNXVIDEO_LOG(VNXLOG_DEBUG, "vnxvideo") << "About to vnxvideo_vmsplugin_h264_source_create_live for channel selector " << channel_selector;
        VnxVideo::CVmsChannelSelector sel(parseSelector(channel_selector));
        source->ptr=vp->CreateLiveSource(sel);
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_vmsplugin_h264_source_create_live: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}
VNXVIDEO_DECLSPEC int vnxvideo_vmsplugin_h264_source_create_archive(vnxvideo_vmsplugin_t vmsplugin,
    const char* channel_selector, uint64_t begin, uint64_t end, double speed,
    vnxvideo_h264_source_t* source) {
    VnxVideo::IVmsPlugin* vp(reinterpret_cast<VnxVideo::IVmsPlugin*>(vmsplugin.ptr));
    try {
        VnxVideo::CVmsChannelSelector sel(parseSelector(channel_selector));
        source->ptr = vp->CreateArchiveSource(sel, begin, end, speed);
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_vmsplugin_h264_source_create_archive: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}
VNXVIDEO_DECLSPEC int vnxvideo_vmsplugin_get_archive_timeline(vnxvideo_vmsplugin_t vmsplugin,
    const char* channel_selector, uint64_t begin, uint64_t end,
    vnxvideo_buffer_t* intervals) {
    VnxVideo::IVmsPlugin* vp(reinterpret_cast<VnxVideo::IVmsPlugin*>(vmsplugin.ptr));
    try {
        VnxVideo::CVmsChannelSelector sel(parseSelector(channel_selector));
        std::vector<std::pair<uint64_t, uint64_t>> timeline(vp->GetArchiveTimeline(sel, begin, end));
        std::vector<uint64_t> timelinePlain(timeline.size() * 2);
        for (std::size_t k = 0; k < timeline.size(); ++k) {
            timelinePlain[k * 2] = timeline[k].first;
            timelinePlain[k * 2 + 1] = timeline[k].second;
        }
        auto ptr = timelinePlain.empty() ? nullptr : (const uint8_t*)&timelinePlain[0];
        return vnxvideo_buffer_copy_wrap(ptr, int(timelinePlain.size()*sizeof(uint64_t)), intervals);
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_vmsplugin_get_archive_timeline: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}
VNXVIDEO_DECLSPEC int vnxvideo_vmsplugin_get_snapshot(vnxvideo_vmsplugin_t vmsplugin,
    const char* channel_selector, uint64_t timestamp, vnxvideo_buffer_t* jpeg) {
    VnxVideo::IVmsPlugin* vp(reinterpret_cast<VnxVideo::IVmsPlugin*>(vmsplugin.ptr));
    try {
        VnxVideo::CVmsChannelSelector sel(parseSelector(channel_selector));
        jpeg->ptr = vp->GetSnapshot(sel, timestamp);
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_vmsplugin_get_snapshot: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }

}

VNXVIDEO_DECLSPEC int vnxvideo_audio_encoder_create(EMediaSubtype output, const char* json_config, vnxvideo_encoder_t* encoder) {
    try {
        encoder->ptr = VnxVideo::CreateAudioEncoder(output);
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_audio_encoder_create: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}
VNXVIDEO_DECLSPEC int vnxvideo_audio_decoder_create(EMediaSubtype input,
    int channels, const uint8_t *extradata, int extradata_length,
    vnxvideo_decoder_t* decoder) {
    try {
        std::vector<uint8_t> vextradata;
        if (extradata && extradata_length) {
            vextradata.insert(vextradata.end(), extradata, extradata + extradata_length);
        }
        decoder->ptr = VnxVideo::CreateAudioDecoder(input, channels, vextradata);
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_audio_decoder_create: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}


VNXVIDEO_DECLSPEC int vnxvideo_audio_transcoder_create(EMediaSubtype output,
    EMediaSubtype input, int channels, const uint8_t *extradata, int extradata_length,
    vnxvideo_transcoder_t* transcoder) {
    try {
        json jextradata;
        if (extradata && extradata_length) {
            std::string s(extradata, extradata + extradata_length);
            std::stringstream ss(s);
            ss >> jextradata;
        }
        transcoder->ptr = VnxVideo::CreateAudioTranscoder(output, input, channels, jextradata);
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_audio_transcoder_create: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}
VNXVIDEO_DECLSPEC void vnxvideo_transcoder_free(vnxvideo_transcoder_t transcoder) {
    delete reinterpret_cast<VnxVideo::ITranscoder*>(transcoder.ptr);
}
VNXVIDEO_DECLSPEC int vnxvideo_transcoder_subscribe(vnxvideo_transcoder_t transcoder, vnxvideo_on_buffer_t handle_data, void* usrptr) {
    try {
        VnxVideo::ITranscoder* p = reinterpret_cast<VnxVideo::ITranscoder*>(transcoder.ptr);
        if (handle_data != nullptr)
            p->Subscribe([=](VnxVideo::IBuffer* buf, uint64_t ts) { handle_data(usrptr, vnxvideo_buffer_t{ buf }, ts); });
        else
            p->Subscribe([](VnxVideo::IBuffer* buf, uint64_t ts) {});
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_transcoder_subscribe: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }
}
VNXVIDEO_DECLSPEC int vnxvideo_transcoder_process(vnxvideo_transcoder_t transcoder, vnxvideo_buffer_t buffer, uint64_t timestamp) {
    try {
        VnxVideo::ITranscoder* p = reinterpret_cast<VnxVideo::ITranscoder*>(transcoder.ptr);
        p->Process(reinterpret_cast<VnxVideo::IBuffer*>(buffer.ptr), timestamp);
        return vnxvideo_err_ok;
    }
    catch (const std::exception& e) {
        VNXVIDEO_LOG(VNXLOG_ERROR, "vnxvideo") << "Exception on vnxvideo_transcoder_process: " << e.what();
        return vnxvideo_err_invalid_parameter;
    }

}

VNXVIDEO_DECLSPEC bool vnxvideo_emf_is_video(ERawMediaFormat emf) {
    return emf < EMF_AUDIO;
}
VNXVIDEO_DECLSPEC bool vnxvideo_emf_is_audio(ERawMediaFormat emf) {
    return emf > EMF_AUDIO;
}
