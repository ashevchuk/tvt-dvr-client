#include "session.h"

std::unique_ptr<dvr::Device> g_device;

time_t day_start_of(int year, int month, int day)
{
    tm local;
    memset(&local, 0, sizeof(local));
    local.tm_year = year - 1900;
    local.tm_mon = month - 1;
    local.tm_mday = day;
    local.tm_isdst = -1;
    return mktime(&local);
}

std::string format_clock(time_t t)
{
    tm local;
    localtime_r(&t, &local);
    char buf[16];
    strftime(buf, sizeof(buf), "%H:%M:%S", &local);
    return buf;
}
