#pragma once

#include <ostream>
#include <sstream>
#include "vnxvideo.h"

namespace NVnxVideoLogImpl {
    extern VNXVIDEO_DECLSPEC vnxvideo_log_t g_logHandler;
    extern VNXVIDEO_DECLSPEC void* g_logUsrptr;
    extern VNXVIDEO_DECLSPEC ELogLevel g_maxLogLevel;

    class log_ostream : public std::stringstream {
        ELogLevel const m_level;
        const char* const m_subsystem;
    public:
        log_ostream(ELogLevel level, const char* subsystem)
            :m_level(level)
            ,m_subsystem(subsystem)
        {}
        ~log_ostream() {
            if(nullptr != g_logHandler)
                g_logHandler(g_logUsrptr, m_level, m_subsystem, this->str().c_str());
        }
    };

    inline char* removecrlf(int c, char*s) {
        --c;
        while (c >= 0 && (s[c] == 0 || s[c] == '\r' || s[c] == '\n')) {
            s[c] = 0;
            --c;
        }
        return s;
    }
}

#if defined(__GNUC__) && (__GNUC__ >= 11)
#define VNXVIDEO_LOG(level, subsystem) ((level)>NVnxVideoLogImpl::g_maxLogLevel) ? static_cast<std::stringstream &&>(*(std::ostream*)nullptr) : static_cast<std::stringstream &&>(NVnxVideoLogImpl::log_ostream((level), (subsystem)))
#else
#define VNXVIDEO_LOG(level, subsystem) ((level)>NVnxVideoLogImpl::g_maxLogLevel) ? (*(std::ostream*)nullptr) : NVnxVideoLogImpl::log_ostream((level), (subsystem))
#endif

