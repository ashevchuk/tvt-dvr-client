# The vendor SDK is not part of this repository; see vendor/README.md.
SDK_DIR ?= $(CURDIR)/vendor/sdk
SDK_LIB_DIR := $(SDK_DIR)/Linux/bin

CXX ?= g++
CXXFLAGS ?= -O2
CXXFLAGS += -std=c++17 -Wall -Wextra -pthread -MMD -MP

# libasound.so is the copy shipped with the SDK; the system one lacks the symbols it needs.
SDK_LDLIBS := -l:libdvrnetsdk.so -l:libasound.so
SDK_LINK = -L"$(SDK_LIB_DIR)" $(SDK_LDLIBS) "-Wl,-rpath,$(SDK_LIB_DIR)"

GUI_PKGS := raylib libavcodec libswscale libavutil

CLIENT_OBJS := $(patsubst src/client/%.cpp,build/client/%.o,$(wildcard src/client/*.cpp))
TOOLS := bin/probe bin/playback_probe

PROTO_OBJS := $(patsubst src/proto/%.cpp,build/proto/%.o,$(wildcard src/proto/*.cpp))

.PHONY: all clean test protocheck
all: bin/tvt-dvr-client $(TOOLS)

# The protocol layer needs neither the vendor SDK nor any library besides the C++ runtime.
test: bin/test_wire
	bin/test_wire

protocheck: bin/protocheck

bin/test_wire: tests/test_wire.cpp build/proto/wire.o | bin
	$(CXX) $(CXXFLAGS) -Isrc $^ -o $@

bin/protocheck: src/tools/protocheck.cpp $(PROTO_OBJS) | bin
	$(CXX) $(CXXFLAGS) -Isrc $^ -o $@

build/proto/%.o: src/proto/%.cpp | build/proto
	$(CXX) $(CXXFLAGS) -Isrc -c $< -o $@

bin/tvt-dvr-client: $(CLIENT_OBJS) | bin
	$(CXX) $(CXXFLAGS) $^ -o $@ $(SDK_LINK) $$(pkg-config --libs $(GUI_PKGS))

build/client/%.o: src/client/%.cpp | build/client
	$(CXX) $(CXXFLAGS) $$(pkg-config --cflags $(GUI_PKGS)) -I"$(SDK_DIR)/include" -c $< -o $@

bin/%: src/tools/%.cpp | bin
	$(CXX) $(CXXFLAGS) -I"$(SDK_DIR)/include" $< -o $@ $(SDK_LINK)

bin build/client build/proto:
	mkdir -p $@

clean:
	rm -rf bin build

-include $(CLIENT_OBJS:.o=.d) $(PROTO_OBJS:.o=.d)

-include $(wildcard bin/*.d)
