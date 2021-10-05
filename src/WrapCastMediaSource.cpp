#include "../include/vnxvideo/vnxvideoimpl.h"

class CMediaSourceToH264 : public VnxVideo::IH264VideoSource {
    std::unique_ptr<VnxVideo::IMediaSource> m_impl;
public:
    CMediaSourceToH264(VnxVideo::IMediaSource* impl): m_impl(impl) {}
    virtual void Subscribe(VnxVideo::TOnBufferCallback onBuffer) {
        m_impl->SubscribeMedia(EMST_H264, onBuffer);
    }
    virtual void Run() {
        m_impl->Run();
    }
    virtual void Stop() {
        m_impl->Stop();
    }
    virtual void Subscribe(VnxVideo::TOnJsonCallback onJson) {
        m_impl->SubscribeJson(onJson);
    }
};

class CMediaSourceFromH264 : public VnxVideo::IMediaSource {
    std::unique_ptr<VnxVideo::IH264VideoSource> m_impl;
public:
    CMediaSourceFromH264(VnxVideo::IH264VideoSource* impl) : m_impl(impl) {}
    virtual void SubscribeMedia(EMediaSubtype mediaSubtype, VnxVideo::TOnBufferCallback onBuffer) {
        if (EMST_H264 == mediaSubtype)
            m_impl->Subscribe(onBuffer);
    }
    virtual void SubscribeJson(VnxVideo::TOnJsonCallback onJson) {
        m_impl->Subscribe(onJson);
    }
    virtual void Run() {
        m_impl->Run();
    }
    virtual void Stop() {
        m_impl->Stop();
    }
};

namespace VnxVideo {
    IH264VideoSource* CreateH264VideoSourceFromMediaSource(IMediaSource* src) {
        return new CMediaSourceToH264(src);
    }
    IMediaSource* CreateMediaSourceFromH264VideoSource(IH264VideoSource* src) {
        return new CMediaSourceFromH264(src);
    }

}
