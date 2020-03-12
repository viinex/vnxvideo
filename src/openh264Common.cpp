#include "openh264Common.h"

void openh264log(void*, int level, const char* msg) {
    VNXVIDEO_LOG(loglevel2vnx(level), "openh264engine") << msg;
}
