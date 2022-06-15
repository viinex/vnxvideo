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

std::shared_ptr<AVCodecContext> createAvDecoderContext(const AVCodec* codec, std::function<void(AVCodecContext&)> setup = [](...) {});
std::shared_ptr<AVCodecContext> createAvDecoderContext(const char* name, std::function<void(AVCodecContext&)> setup = [](...) {});
std::shared_ptr<AVCodecContext> createAvDecoderContext(AVCodecID codecId, std::function<void(AVCodecContext&)> setup = [](...) {});
std::shared_ptr<AVCodecContext> createAvEncoderContext(const AVCodec* codec, std::function<void(AVCodecContext&)> setup);
std::shared_ptr<AVCodecContext> createAvEncoderContext(const char* name, std::function<void(AVCodecContext&)> setup);
std::shared_ptr<AVCodecContext> createAvEncoderContext(AVCodecID codecId, std::function<void(AVCodecContext&)> setup);
std::shared_ptr<AVFrame> avframeAlloc();
std::shared_ptr<AVPacket> avpacketAlloc();

EColorspace fromAVPixelFormat(AVPixelFormat format);
AVPixelFormat toAVPixelFormat(EColorspace csp);
int nplanesByAVPixelFormat(AVPixelFormat format);


class CAvcodecRawSample : public VnxVideo::IRawSample {
public:
    CAvcodecRawSample();
    CAvcodecRawSample(const AVFrame* f);
    CAvcodecRawSample(std::shared_ptr<AVFrame> f);

    VnxVideo::IRawSample* Dup();
    virtual void GetFormat(EColorspace &csp, int &width, int &height);
    virtual void GetData(int* strides, uint8_t** planes);
    virtual void GetData(uint8_t* &data, int& size);
    AVFrame* GetAVFrame();
private:
    std::shared_ptr<AVFrame> m_frame;
};

bool isCodecImplSupported(VnxVideo::ECodecImpl eci);

void checkFramesContext(AVCodecContext& cc, int width, int height, AVPixelFormat hwpixfmt);