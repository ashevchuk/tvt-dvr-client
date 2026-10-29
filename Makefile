CXX ?= g++
CXXFLAGS ?= -O2
override CXXFLAGS += -std=c++17 -Wall -Wextra -pthread -MMD -MP -Isrc

PKG_CONFIG_FLAGS ?=
MEDIA_PKGS := libavformat libavcodec libswscale libavutil
GUI_PKGS := raylib $(MEDIA_PKGS)

OBJ := build

PROTO_OBJS := $(OBJ)/proto/wire.o $(OBJ)/proto/connection.o
MEDIA_OBJS := $(OBJ)/media/ima_decoder.o
DEVICE_OBJS := $(OBJ)/device/native_device.o $(OBJ)/device/recording_writer.o
CLIENT_OBJS := $(patsubst src/client/%.cpp,$(OBJ)/client/%.o,$(wildcard src/client/*.cpp))
BINARIES := bin/tvt-dvr-client bin/devicecheck bin/protocheck

.PHONY: all test clean
all: $(BINARIES)

test: bin/test_wire bin/test_ima bin/test_text_buffer bin/test_device_store bin/test_tile_expansion
	bin/test_wire
	bin/test_ima
	bin/test_text_buffer
	bin/test_device_store
	bin/test_tile_expansion

bin/tvt-dvr-client: $(CLIENT_OBJS) $(DEVICE_OBJS) $(PROTO_OBJS) $(MEDIA_OBJS) | bin
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS) $$(pkg-config $(PKG_CONFIG_FLAGS) --libs $(GUI_PKGS)) $(LDLIBS)

bin/devicecheck: src/tools/devicecheck.cpp $(DEVICE_OBJS) $(PROTO_OBJS) $(MEDIA_OBJS) | bin
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS) $$(pkg-config $(PKG_CONFIG_FLAGS) --cflags --libs $(MEDIA_PKGS))

bin/protocheck: src/tools/protocheck.cpp $(PROTO_OBJS) | bin
	$(CXX) $(CXXFLAGS) $^ -o $@

bin/test_wire: tests/test_wire.cpp $(OBJ)/proto/wire.o | bin
	$(CXX) $(CXXFLAGS) $^ -o $@

bin/test_ima: tests/test_ima.cpp $(MEDIA_OBJS) | bin
	$(CXX) $(CXXFLAGS) $^ -o $@ $$(pkg-config --cflags --libs libavcodec libavutil)

bin/test_text_buffer: tests/test_text_buffer.cpp | bin
	$(CXX) $(CXXFLAGS) $^ -o $@

bin/test_device_store: tests/test_device_store.cpp $(OBJ)/client/device_store.o | bin
	$(CXX) $(CXXFLAGS) $^ -o $@

bin/test_tile_expansion: tests/test_tile_expansion.cpp | bin
	$(CXX) $(CXXFLAGS) $^ -o $@

$(OBJ)/%.o: src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $$(pkg-config --cflags $(GUI_PKGS)) -c $< -o $@

bin:
	mkdir -p $@

clean:
	rm -rf bin build

-include $(shell find build -name '*.d' 2>/dev/null)
