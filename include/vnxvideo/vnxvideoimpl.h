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

    class IRawSample: public IBuffer {
    public:
        virtual void GetFormat(EColorspace &csp, int &width, int &height) = 0;
        virtual void GetData(int* strides, uint8_t** planes) = 0;
        virtual IRawSample* Dup() = 0; // make a shallow copy, ie share the same underlying raw buffer
    };
    typedef std::shared_ptr<IRawSample> PRawSample;

    VNXVIDEO_DECLSPEC IRawSample* CopyRawToI420(IRawSample*);

    typedef typename std::function<void(EColorspace csp, int width, int height)> TOnFormatCallback;
    typedef typename std::function<void(IRawSample*, uint64_t)> TOnFrameCallback;
    typedef typename std::function<void(IBuffer*, uint64_t)> TOnBufferCallback;
    typedef typename std::function<void(const std::string& json, uint64_t)> TOnJsonCallback;

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

    class IVideoDecoder {
    public:
        virtual ~IVideoDecoder() {}
        virtual void Subscribe(TOnFormatCallback onFormat, TOnFrameCallback onFrame) = 0;
        virtual void Decode(IBuffer* nalu, uint64_t timestamp) = 0;
        virtual void Flush() = 0;
    };
    typedef std::shared_ptr<IVideoDecoder> PVideoDecoder;

    VNXVIDEO_DECLSPEC IVideoDecoder* CreateVideoDecoder_FFmpegH264();
    VNXVIDEO_DECLSPEC IVideoDecoder* CreateVideoDecoder_OpenH264();

    class IRawProc {
    public:
        virtual ~IRawProc() {}
        virtual void SetFormat(EColorspace csp, int width, int height) = 0;
        virtual void Process(IRawSample* sample, uint64_t timestamp) = 0;
        virtual void Flush() = 0;
    };
    typedef std::shared_ptr<IRawProc> PRawProc;

    class IVideoEncoder: public IRawProc {
    public:
        virtual void Subscribe(TOnBufferCallback onBuffer) = 0;
    };
    typedef std::shared_ptr<IVideoEncoder> PVideoEncoder;

    VNXVIDEO_DECLSPEC IVideoEncoder* CreateVideoEncoder_x264(const char* profile, const char* preset, int fps, const char* quality);
    VNXVIDEO_DECLSPEC IVideoEncoder* CreateVideoEncoder_OpenH264(const char* profile, const char* preset, int fps, const char* quality);
    VNXVIDEO_DECLSPEC IVideoEncoder* CreateAsyncVideoEncoder(PVideoEncoder enc);

    class IH264VideoSource {
    public:
        virtual ~IH264VideoSource() {}
        virtual void Subscribe(TOnBufferCallback onBuffer) = 0;
        virtual void Run() = 0;
        virtual void Stop() = 0;
        virtual void Subscribe(TOnJsonCallback onJson) {}
    };
    // created by factory functions exposed from plugins

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
        virtual void SetFormat(EColorspace csp, int width, int height) = 0;
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

    class IRenderer: IVideoSource {
    public:
        virtual IRawProc* CreateInput(int index, VnxVideo::PRawTransform transform) = 0;
        virtual void UpdateLayout(int width, int height, uint8_t* backgroundColor, VnxVideo::IRawSample* backgroundImage, 
            VnxVideo::IRawSample* nosignalImage, const TLayout& layout) = 0;
        virtual void SetBackground(uint8_t* backgroundColor, VnxVideo::IRawSample* backgroundImage) =0;
        virtual void SetNosignal(VnxVideo::IRawSample* backgroundImage) = 0;
    };
    VNXVIDEO_DECLSPEC IRenderer* CreateRenderer(int refresh_rate);

    VNXVIDEO_DECLSPEC IVideoSource *CreateLocalVideoClient(const char* name);
    VNXVIDEO_DECLSPEC IRawProc *CreateLocalVideoProvider(const char* name);

    VNXVIDEO_DECLSPEC IRawProc *CreateDisplay(int width, int height, const char* name, std::function<void(void)> onClose);

    VNXVIDEO_DECLSPEC void WithPreferredShmAllocator(const char* name, std::function<void(void)> action);

}
