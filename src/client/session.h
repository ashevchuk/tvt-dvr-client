#pragma once

#include "common.h"

extern LONG g_uid;
// The SDK is not documented as thread safe; every control call goes through this lock.
extern std::mutex g_sdk_mutex;

static const int FIND_RETRY_MS = 50;

static const int FIND_TIMEOUT_MS = 10000;

DD_TIME to_dd_time(time_t t);
time_t from_dd_time(const DD_TIME &d);
time_t day_start_of(int year, int month, int day);
std::string format_clock(time_t t);
