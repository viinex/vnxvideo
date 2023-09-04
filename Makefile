TARGET = libvnxvideo.so

SOURCES = $(wildcard src/*.cpp)

OBJECTS = $(SOURCES:.cpp=.o)
DEPS = $(OBJECTS:.o=.d)

ifdef DEBUG
CXXFLAGS = -O0 -g -DDEBUG
LDFLAGS += -g
else
CXXFLAGS = -O2 -std=c++20
endif

ARCH = $(shell uname -m)
UNAME_OS = $(shell uname)

ifeq ($(ARCH),aarch64)
IPPLIBS =
else
IPPLIBS = -lippcc -lippcv -lippi -lipps -lippcore
endif

CXXFLAGS += -MMD -Iinclude -Iinclude/vnxvideo -I$(FFMPEG_HOME)/include -I$(IPP_HOME)/include -I$(OPENH264_HOME)/include -DVNXVIDEO_EXPORTS -fPIC

LDFLAGS += -L$(FFMPEG_HOME)/lib -L$(OPENH264_HOME)/lib -L$(IPP_HOME)/lib -L$(IPP_HOME)/lib

ifeq ($(UNAME_OS), Darwin)
LDLIBS = -lopenh264 -lavcodec -lavutil -lavformat -lswscale $(IPPLIBS) -lpthread -lz -ldl -lswresample -lboost_system

$(TARGET): $(OBJECTS)
	c++ -shared $(LDFLAGS) -o $(TARGET) $(OBJECTS) $(LDLIBS)
else
LDLIBS = -lopenh264 -lavcodec -lavutil -lavformat -lswscale $(IPPLIBS) -lpthread -lz -ldl -lrt -lswresample -lboost_system

$(TARGET): $(OBJECTS)
	c++ -shared $(LDFLAGS) -Wl,-Bsymbolic -z defs -o $(TARGET) $(OBJECTS) $(LDLIBS)
endif

clean:
	rm -f $(OBJECTS) $(DEPS) $(TARGET)

install:
	sudo cp $(TARGET) /usr/local/lib

-include $(DEPS)
