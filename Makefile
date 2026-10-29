# The default build needs no proprietary software. SDK=1 adds a second backend on top of the
# vendor SDK, which is not part of this repository; see vendor/README.md.
SDK ?= 0
SDK_DIR ?= $(CURDIR)/vendor/sdk
SDK_LIB_DIR := $(SDK_DIR)/Linux/bin

CXX ?= g++
CXXFLAGS ?= -O2
override CXXFLAGS += -std=c++17 -Wall -Wextra -pthread -MMD -MP -Isrc

PKG_CONFIG_FLAGS ?=
MEDIA_PKGS := libavformat libavcodec libswscale libavutil
GUI_PKGS := raylib $(MEDIA_PKGS)

OBJ := build/sdk$(SDK)

PROTO_OBJS := $(OBJ)/proto/wire.o $(OBJ)/proto/connection.o
MEDIA_OBJS := $(OBJ)/media/ima_decoder.o
DEVICE_OBJS := $(OBJ)/device/native_device.o $(OBJ)/device/recording_writer.o
CLIENT_OBJS := $(patsubst src/client/%.cpp,$(OBJ)/client/%.o,$(wildcard src/client/*.cpp))
BINARIES := bin/tvt-dvr-client bin/devicecheck bin/protocheck

ifeq ($(SDK),1)
override CXXFLAGS += -DDVR_WITH_SDK -I"$(SDK_DIR)/include"
SDK_LINK := -L"$(SDK_LIB_DIR)" -l:libdvrnetsdk.so -l:libasound.so "-Wl,-rpath,$(SDK_LIB_DIR)"
DEVICE_OBJS += $(OBJ)/device/sdk_device.o
BINARIES += bin/probe bin/playback_probe
endif

.PHONY: all test clean
all: $(BINARIES)

test: bin/test_wire bin/test_ima
	bin/test_wire
	bin/test_ima

bin/tvt-dvr-client: $(CLIENT_OBJS) $(DEVICE_OBJS) $(PROTO_OBJS) $(MEDIA_OBJS) | bin
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS) $(SDK_LINK) $$(pkg-config $(PKG_CONFIG_FLAGS) --libs $(GUI_PKGS)) $(LDLIBS)

bin/devicecheck: src/tools/devicecheck.cpp $(DEVICE_OBJS) $(PROTO_OBJS) $(MEDIA_OBJS) | bin
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS) $(SDK_LINK) $$(pkg-config $(PKG_CONFIG_FLAGS) --cflags --libs $(MEDIA_PKGS))

bin/protocheck: src/tools/protocheck.cpp $(PROTO_OBJS) | bin
	$(CXX) $(CXXFLAGS) $^ -o $@

bin/test_wire: tests/test_wire.cpp $(OBJ)/proto/wire.o | bin
	$(CXX) $(CXXFLAGS) $^ -o $@

bin/test_ima: tests/test_ima.cpp $(MEDIA_OBJS) | bin
	$(CXX) $(CXXFLAGS) $^ -o $@ $$(pkg-config --cflags --libs libavcodec libavutil)

# The experiments that talk to the SDK directly (only with SDK=1).
bin/probe bin/playback_probe: bin/%: src/tools/%.cpp | bin
	$(CXX) $(CXXFLAGS) $< -o $@ $(SDK_LINK)

$(OBJ)/%.o: src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $$(pkg-config --cflags $(GUI_PKGS)) -c $< -o $@

bin:
	mkdir -p $@

clean:
	rm -rf bin build

-include $(shell find build -name '*.d' 2>/dev/null)
