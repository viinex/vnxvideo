#pragma once

#include <string>
#include <map>
#include <vector>
#include <functional>
#include <memory>

#include "json.hpp"

#include "vnxvideo.h"

namespace VnxVideo
{
    class IBuffer {
    public:
        virtual ~IBuffer() {}
        virtual void GetData(uint8_t* &data, int& size) = 0;
        virtual IBuffer* Dup() = 0; // make a shallow copy, ie share the same underlying raw buffer
    };

    class IRawSample {
    public:
        virtual ~IRawSample() {}
        virtual void GetFormat(ERawMediaFormat &, int &, int &) = 0;
        // ^^ Width and height are returned in 2nd and 3rd parameters for video media formats,
        // and number of samples and number of audio channels are returned for audio media format(s).
        // See also comment for TOnFormatCallback.
        virtual void GetData(int* strides, uint8_t** planes) = 0;
        virtual IRawSample* Dup() = 0; // make a shallow copy, ie share the same underlying raw buffer
    };
    typedef std::shared_ptr<IRawSample> PRawSample;

    VNXVIDEO_DECLSPEC IRawSample* CopyRawToI420(IRawSample*);

    typedef typename std::function<void(ERawMediaFormat emf, int, int)> TOnFormatCallback;
    // ^^ A (new, as of 2022-10-01) convention on TOnFormatCallback.
    // For video media formats, arguments represent width and height of image,
    // in particular for that of luma plane in case of YUV formats with chroma planes
    // smaller than luma. This (video-related) behavior was not changed.
    // For audio format(s), arguments represent sample rate (2nd arg) and number of audio channels (3rd arg).
    typedef typename std::function<void(IRawSample*, uint64_t)> TOnFrameCallback;
    typedef typename std::function<void(IBuffer*, uint64_t)> TOnBufferCallback;
    typedef typename std::function<void(const std::string& json, uint64_t)> TOnJsonCallback;
    // A (new, as of 2020-04-25) convention for TOnFrameCallback and TOnBufferCallback: 
    // a call with (nullptr,_) means an "end of stream", -- maybe because of an unrecoverable error
    // (unrecoverable for the caller object). It means that a data source object is no longer going to 
    // produce any new data. It still should be Stopped and destroyed.
    // For now (2020-04-25) all known live sources do avoid this behavior though and attempt 
    // to implement recovery themselves, they don't send nullptrs. No contradiction here: they believe
    // there are not unrecoverable errors for them.
    // For one-shot sources, like archive playback sessions, reporting an end of stream is mandatory.

    class IVideoSource {
    public:
        virtual ~IVideoSource() {}
        virtual void Subscribe(TOnFormatCallback onFormat, TOnFrameCallback onFrame) = 0;
        virtual void Run() = 0;
        virtual void Stop() = 0;
    };
    typedef std::shared_ptr<IVideoSource> PVideoSource;

    typedef std::vector<std::string> TCapabilities;
    typedef std::string TFriendlyName;
    typedef std::string TUniqueName;
    typedef std::map<TUniqueName, std::pair<TFriendlyName, TCapabilities> > TDevices;

    class IVideoDeviceManager {
    public:
        virtual ~IVideoDeviceManager() {}
        virtual void EnumerateDevices(bool details, TDevices& dev) = 0;
        virtual IVideoSource* CreateVideoSource(const TUniqueName& path, const std::string& mode) = 0;
    };
    typedef std::shared_ptr<IVideoDeviceManager> PVideoDeviceManager;

    VNXVIDEO_DECLSPEC IVideoDeviceManager* CreateVideoDeviceManager_DirectShow();
    VNXVIDEO_DECLSPEC IVideoDeviceManager* CreateVideoDeviceManager_V4L();

    class IMediaDecoder {
    public:
        virtual ~IMediaDecoder() {}
        virtual void Subscribe(TOnFormatCallback onFormat, TOnFrameCallback onFrame) = 0;
        virtual void Decode(IBuffer* nalu, uint64_t timestamp) = 0;
        virtual void Flush() = 0;
    };
    typedef std::shared_ptr<IMediaDecoder> PMediaDecoder;

    enum ECodecImpl { 
        ECI_CPU = 0, 
        ECI_QSV = 1, 
        ECI_CUDA = 2, 
        ECI_VAAPI = 4,
        ECI_D3D11VA = 8,
    };

    VNXVIDEO_DECLSPEC IMediaDecoder* CreateVideoDecoder_FFmpegH264();
    VNXVIDEO_DECLSPEC IMediaDecoder* CreateVideoDecoder_FFmpegHEVC();
    VNXVIDEO_DECLSPEC IMediaDecoder* CreateVideoDecoder_OpenH264();

    class IRawProc {
    public:
        virtual ~IRawProc() {}
        virtual void SetFormat(ERawMediaFormat csp, int width, int height) = 0;
        virtual void Process(IRawSample* sample, uint64_t timestamp) = 0;
        virtual void Flush() = 0;
    };
    typedef std::shared_ptr<IRawProc> PRawProc;

    class IMediaEncoder: public IRawProc {
    public:
        virtual void Subscribe(TOnBufferCallback onBuffer) = 0;
    };
    typedef std::shared_ptr<IMediaEncoder> PMediaEncoder;

    VNXVIDEO_DECLSPEC IMediaEncoder* CreateVideoEncoder_x264(const char* profile, const char* preset, int fps, const char* quality);
    VNXVIDEO_DECLSPEC IMediaEncoder* CreateVideoEncoder_OpenH264(const char* profile, const char* preset, int fps, const char* quality);
    // create encoder of specific hw accelerator implementation
    VNXVIDEO_DECLSPEC IMediaEncoder* CreateVideoEncoder_FFmpeg(const char* profile, const char* preset, int fps, const char* quality, ECodecImpl eci);
    // automatically choose supported hw accelerator implementation
    VNXVIDEO_DECLSPEC IMediaEncoder* CreateVideoEncoder_FFmpeg_Auto(const char* profile, const char* preset, int fps, const char* quality);
    VNXVIDEO_DECLSPEC IMediaEncoder* CreateAsyncVideoEncoder(PMediaEncoder enc);

    class ITranscoder {
    public:
        virtual ~ITranscoder() {}
        virtual void Subscribe(TOnBufferCallback onBuffer) = 0;
        virtual void Process(IBuffer* buf, uint64_t timestamp) = 0;
    };
    VNXVIDEO_DECLSPEC ITranscoder* CreateAudioTranscoder(int channels, 
        EMediaSubtype input, const char* inputDetails,
        EMediaSubtype output, const char* outputDetails);

    class IMediaSource {
    public:
        virtual ~IMediaSource() {}
        virtual void SubscribeMedia(EMediaSubtype mediaSubtype, TOnBufferCallback onBuffer) = 0;
        virtual void SubscribeJson(TOnJsonCallback onJson) = 0;
        virtual void Run() = 0;
        virtual void Stop() = 0;
        virtual std::vector<EMediaSubtype> EnumMediatypes() = 0;
        virtual VnxVideo::IBuffer* GetExtradata(EMediaSubtype mediaSubtype) = 0; // may return nullptr
    };

    // to be deprecated
    class IH264VideoSource {
    public:
        virtual ~IH264VideoSource() {}
        virtual void Subscribe(TOnBufferCallback onBuffer) = 0;
        virtual void Run() = 0;
        virtual void Stop() = 0;
        virtual void Subscribe(TOnJsonCallback onJson) {}
    };
    // created by factory functions exposed from plugins

    VNXVIDEO_DECLSPEC IH264VideoSource* CreateH264VideoSourceFromMediaSource(IMediaSource*);
    VNXVIDEO_DECLSPEC IMediaSource* CreateMediaSourceFromH264VideoSource(IH264VideoSource*);

    class IRawTransform : public IRawProc {
    public:
        virtual void Subscribe(TOnFormatCallback onFormat, TOnFrameCallback onFrame) = 0;
    };
    typedef std::shared_ptr<IRawTransform> PRawTransform;

    VNXVIDEO_DECLSPEC IRawTransform* CreateRawTransform_DewarpProjective(const std::vector<double>& tform);
    VNXVIDEO_DECLSPEC IRawTransform* CreateRawTransform(const nlohmann::json& config);
    VNXVIDEO_DECLSPEC IRawTransform* CreateAsyncTransform(PRawTransform);



    class IComposer : public IRawProc {
    public:
        virtual void SetOverlay(IRawSample*) = 0;
    };
    typedef std::shared_ptr<IComposer> PComposer;

    VNXVIDEO_DECLSPEC IComposer* CreateComposer(uint8_t colorkey[4], int left, int top);
    VNXVIDEO_DECLSPEC IRawSample* ParseBMP(const uint8_t* buffer, int buffer_size);
    VNXVIDEO_DECLSPEC IRawSample* LoadBMP(const char* filename);

    class IAnalytics : public IRawProc {
    public:
        virtual void Subscribe(TOnJsonCallback onJson, TOnBufferCallback onBinary) = 0;
    };
    VNXVIDEO_DECLSPEC IAnalytics* CreateAnalytics_Basic(const std::vector<float>& roi, float framerate, 
        bool too_bright, bool too_dark, bool too_blurry, float motion, bool scene_change);


    class IImageAnalytics {
    public:
        virtual ~IImageAnalytics() {}
        virtual void SetFormat(ERawMediaFormat csp, int width, int height) = 0;
        virtual std::string Process(VnxVideo::IRawSample* sample) = 0;
    };

    struct Viewport {
        int input;

        bool border;
        uint8_t border_rgb[3];

        float src_left;
        float src_top;
        float src_right;
        float src_bottom;

        float dst_left;
        float dst_top;
        float dst_right;
        float dst_bottom;
    };
    typedef std::vector<VnxVideo::Viewport> TLayout;

    struct AudioInput {
        int input;
        float gain;
    };
    typedef std::vector<VnxVideo::AudioInput> TAudioLayout;

    class IRenderer: IVideoSource {
    public:
        virtual IRawProc* CreateInput(int index, VnxVideo::PRawTransform transform) = 0;
        virtual void UpdateLayout(int width, int height, uint8_t* backgroundColor, VnxVideo::IRawSample* backgroundImage, 
            VnxVideo::IRawSample* nosignalImage, const TLayout& layout) = 0;
        virtual void SetBackground(uint8_t* backgroundColor, VnxVideo::IRawSample* backgroundImage) =0;
        virtual void SetNosignal(VnxVideo::IRawSample* backgroundImage) = 0;
        virtual void UpdateAudioLayout(int sample_rate, int channels, const TAudioLayout& layout) = 0;
    };
    VNXVIDEO_DECLSPEC IRenderer* CreateRenderer(int refresh_rate);

    VNXVIDEO_DECLSPEC IVideoSource *CreateLocalVideoClient(const char* name);
    VNXVIDEO_DECLSPEC IRawProc *CreateLocalVideoProvider(const char* name, int maxSizeMB);

    VNXVIDEO_DECLSPEC IRawProc *CreateDisplay(int width, int height, const char* name, std::function<void(void)> onClose);

    VNXVIDEO_DECLSPEC void WithPreferredShmAllocator(const char* name, int maxSizeMB, std::function<void(void)> action);

    class CVmsChannelSelector;

    class IVmsPlugin {
    public:
        virtual ~IVmsPlugin() {}
        // syntax for channelSelector is described in section 2.1.14 of Viinex docs
        virtual IH264VideoSource* CreateLiveSource(const CVmsChannelSelector& channelSelector) = 0;
        virtual IH264VideoSource* CreateArchiveSource(const CVmsChannelSelector& channelSelector,
            uint64_t begin, uint64_t end, double speed = 1.0) = 0;
        virtual std::vector<std::pair<uint64_t, uint64_t>> GetArchiveTimeline(const CVmsChannelSelector& selector,
            uint64_t begin = 0, uint64_t end = -1) = 0;
        virtual IBuffer* GetSnapshot(const CVmsChannelSelector& selector, uint64_t timestamp) = 0;
    };

    class XHWDeviceNotSupported : public std::exception {};
}
