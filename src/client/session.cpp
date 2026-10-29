#include "session.h"

LONG g_uid = -1;
std::mutex g_sdk_mutex;

DD_TIME to_dd_time(time_t t)
{
    tm local;
    localtime_r(&t, &local);
    DD_TIME d;
    memset(&d, 0, sizeof(d));
    d.year = local.tm_year;
    d.month = local.tm_mon;
    d.mday = local.tm_mday;
    d.hour = local.tm_hour;
    d.minute = local.tm_min;
    d.second = local.tm_sec;
    return d;
}

time_t from_dd_time(const DD_TIME &d)
{
    tm local;
    memset(&local, 0, sizeof(local));
    local.tm_year = d.year;
    local.tm_mon = d.month;
    local.tm_mday = d.mday;
    local.tm_hour = d.hour;
    local.tm_min = d.minute;
    local.tm_sec = d.second;
    local.tm_isdst = -1;
    return mktime(&local);
}

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
