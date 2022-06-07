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

std::shared_ptr<AVCodecContext> createAvDecoderContext(AVCodecID codecId, std::function<void(AVCodecContext&)> setup = [](...) {});
std::shared_ptr<AVCodecContext> createAvEncoderContext(AVCodecID codecId, std::function<void(AVCodecContext&)> setup = [](...) {});
std::shared_ptr<AVFrame> avframeAlloc();
std::shared_ptr<AVPacket> avpacketAlloc();
