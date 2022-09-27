TARGET = libvnxvideo.so

SOURCES = $(wildcard src/*.cpp)
HEADERS = $(wildcard src/*.h) $(widcard include/vnxvideo/*.h)

OBJECTS = $(SOURCES:.cpp=.o)

ifdef DEBUG
CXXFLAGS = -O0 -g -DDEBUG
LDFLAGS += -g
else
CXXFLAGS = -O2
endif

ARCH = $(shell uname -m)

ifeq ($(ARCH),aarch64)
IPPLIBS = 
else
	IPPLIBS = -l:libippcc.a -l:libippcv.a -l:libippi.a -l:libipps.a -l:libippcore.a
endif

CXXFLAGS += -Iinclude -Iinclude/vnxvideo -I$(FFMPEG_HOME)/include -I$(IPP_HOME)/include -I$(OPENH264_HOME)/include -DVNXVIDEO_EXPORTS -fPIC

LDFLAGS += -L$(FFMPEG_HOME)/lib -L$(OPENH264_HOME)/lib -L$(IPP_HOME)/lib/intel64 -L$(IPP_HOME)/lib/intel64_lin

LDLIBS = -l:libopenh264.a -lavcodec -lavutil -lavformat -lswscale $(IPPLIBS) -lpthread -lz -ldl -lrt -lswresample -l:libboost_system.a

$(TARGET): $(OBJECTS)
	c++ -shared $(LDFLAGS) -Wl,-Bsymbolic -z defs -o $(TARGET) $(OBJECTS) $(LDLIBS)

clean:
	rm -f $(OBJECTS) $(TARGET)
