TARGET = libvnxvideo.so

SOURCES = $(wildcard src/*.cpp)

OBJECTS = $(SOURCES:.cpp=.o)
DEPS = $(OBJECTS:.o=.d)

ifdef DEBUG
CXXFLAGS = -O0 -g -DDEBUG
LDFLAGS += -g
else
CXXFLAGS = -O2
endif

ARCH = $(shell uname -m)
UNAME_OS = $(shell uname)

ifeq ($(ARCH),aarch64)
IPPLIBS =
else
IPPLIBS = -l:libippcc.a -l:libippcv.a -l:libippi.a -l:libipps.a -l:libippcore.a
endif

FFMPEGLIBS = -lavcodec$(FFMPEG_SUFFIX) -lavutil$(FFMPEG_SUFFIX) -lavformat$(FFMPEG_SUFFIX) -lswscale$(FFMPEG_SUFFIX) -lswresample$(FFMPEG_SUFFIX)

CXXFLAGS += -MMD -Iinclude -Iinclude/vnxvideo -I$(FFMPEG_HOME)/include -I$(IPP_HOME)/include -I$(OPENH264_HOME)/include -DVNXVIDEO_EXPORTS -fPIC

LDFLAGS += -L$(FFMPEG_HOME)/lib -L$(OPENH264_HOME)/lib -L$(IPP_HOME)/lib -L$(IPP_HOME)/lib/intel64_lin

ifeq ($(UNAME_OS), Darwin)
LDLIBS = -lopenh264 $(FFMPEGLIBS) $(IPPLIBS) -lpthread -lz -ldl -lboost_system

$(TARGET): $(OBJECTS)
	c++ -shared $(LDFLAGS) -o $(TARGET) $(OBJECTS) $(LDLIBS)
else
LDLIBS = -l:libopenh264.a $(FFMPEGLIBS) $(IPPLIBS) -lpthread -lz -ldl -lrt -lboost_system

$(TARGET): $(OBJECTS)
	c++ -shared $(LDFLAGS) -Wl,-Bsymbolic -z defs -o $(TARGET) $(OBJECTS) $(LDLIBS)
endif

clean:
	rm -f $(OBJECTS) $(DEPS) $(TARGET)

install:
	sudo cp $(TARGET) /usr/local/lib

-include $(DEPS)
