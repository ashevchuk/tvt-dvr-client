#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

// raylib's TraceLogLevel::LOG_ALL collides with an SDK enumerator.
#define LOG_ALL RAYLIB_LOG_ALL
#include <raylib.h>
#undef LOG_ALL

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/log.h>
#include <libswscale/swscale.h>
}

#include "DVR_NET_SDK.h"

static const int MAX_TILES = 16;

static const int MAX_PLAYBACK_TILES = 4;

static const int SECONDS_PER_DAY = 86400;
