#pragma once
#include <functional>

extern "C" {
#include "libavcodec/avcodec.h"
}

#include "vnxvideoimpl.h"
#include "vnxvideologimpl.h"

AVCodecID codecIdFromSubtype(EMediaSubtype subtype);

ELogLevel ffmpeg2loglevel(int level);
int loglevel2ffmpeg(ELogLevel level);

void ffmpeglog(void*, int level, const char* format, va_list args);

std::string fferr2str(int errnum);

void vnxvideo_init_ffmpeg(ELogLevel level);

typedef enum AVPixelFormat(*FAVCCGetPixelFormat)(struct AVCodecContext *s, const enum AVPixelFormat * fmt);

std::tuple<enum AVHWDeviceType, AVPixelFormat, FAVCCGetPixelFormat> fromHwDeviceType(VnxVideo::ECodecImpl vnxHwCodecImpl);

std::shared_ptr<AVCodecContext> createAvDecoderContext(const AVCodec* codec, std::function<void(AVCodecContext&)> setup = [](...) {});
//std::shared_ptr<AVCodecContext> createAvDecoderContext(const char* name, std::function<void(AVCodecContext&)> setup = [](...) {});
std::shared_ptr<AVCodecContext> createAvDecoderContext(AVCodecID codecId, AVHWDeviceType hwDeviceType, std::function<void(AVCodecContext&)> setup = [](...) {});
std::shared_ptr<AVCodecContext> createAvEncoderContext(const AVCodec* codec, std::function<void(AVCodecContext&)> setup);
std::shared_ptr<AVCodecContext> createAvEncoderContext(const char* name, std::function<void(AVCodecContext&)> setup);
std::shared_ptr<AVCodecContext> createAvEncoderContext(AVCodecID codecId, std::function<void(AVCodecContext&)> setup);
std::shared_ptr<AVFrame> avframeAlloc();
std::shared_ptr<AVPacket> avpacketAlloc();

ERawMediaFormat fromAVPixelFormat(AVPixelFormat format);
ERawMediaFormat fromAVSampleFormat(AVSampleFormat format);
AVPixelFormat toAVPixelFormat(ERawMediaFormat emf);
AVSampleFormat toAVSampleFormat(ERawMediaFormat emf);
int toAVFormat(ERawMediaFormat csp);
int nplanesByAVPixelFormat(AVPixelFormat format);
int bitsPerSampleByAVSampleFormat(AVSampleFormat format);
bool isPlanarAudioFormat(AVSampleFormat format);
bool avfrmIsVideo(AVFrame* frm);
bool avfrmIsAudio(AVFrame* frm);


class CAvcodecRawSample : public VnxVideo::IRawSample {
public:
    CAvcodecRawSample();
    CAvcodecRawSample(const AVFrame* f);
    CAvcodecRawSample(std::shared_ptr<AVFrame> f);

    VnxVideo::IRawSample* Dup();
    virtual void GetFormat(ERawMediaFormat &, int &, int &);
    virtual void GetData(int* strides, uint8_t** planes);
    AVFrame* GetAVFrame();
private:
    std::shared_ptr<AVFrame> m_frame;
};

bool isCodecImplSupported(VnxVideo::ECodecImpl eci);

void checkFramesContext(AVCodecContext& cc, int width, int height, AVPixelFormat hwpixfmt);

#ifndef HAS_FF_RKMPP // stubs to compile with vanilla ffmpeg
#define AV_HWDEVICE_TYPE_RKMPP ((AVHWDeviceType)(AV_HWDEVICE_TYPE_VULKAN+1))

#endif