#pragma once

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

void openh264log(void*, int level, const char* msg);
