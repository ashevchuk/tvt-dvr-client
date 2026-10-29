#pragma once

#include "common.h"
#include "device/device.h"

// The device the client is connected to.
extern std::unique_ptr<dvr::Device> g_device;

time_t day_start_of(int year, int month, int day);
std::string format_clock(time_t t);
