/* Switch time service backed by the host clock. */
#include "hle.h"
#include <errno.h>
#include <time.h>

typedef struct { int64_t seconds, nanoseconds; } ss_timespec;
_Static_assert(sizeof(ss_timespec) == 16, "Switch timespec ABI");

HLE_FORCE("clock_gettime", "Map Switch POSIX clocks to host clocks", int, ss_clock_gettime,
          int clock_id, ss_timespec *out) {
    clockid_t host_clock;
    if (clock_id == 0) host_clock = CLOCK_REALTIME;
    else if (clock_id == 1) host_clock = CLOCK_MONOTONIC;
    else {
        int *(*guest_errno)(void) = guest_sym("__errno_location");
        // The guest uses Horizon errno values; EOPNOTSUPP is 95, not Darwin 45.
        if (guest_errno) *guest_errno() = clock_id >= 2 && clock_id <= 11 ? 95 : 22;
        return -1;
    }
    struct timespec value;
    if (!out || clock_gettime(host_clock, &value)) {
        int error = out ? errno : EFAULT;
        int *(*guest_errno)(void) = guest_sym("__errno_location");
        if (guest_errno) *guest_errno() = error;
        return -1;
    }
    dm_guest_write_prepare(out, sizeof(*out));
    *out = (ss_timespec){value.tv_sec, value.tv_nsec};
    return 0;
}

typedef struct { int16_t year; int8_t month, day, hour, minute, second, pad; } ss_calendar;
typedef struct { uint32_t wday, yday; char zone[8]; uint32_t dst; int32_t offset; } ss_calendar_info;
_Static_assert(sizeof(ss_calendar) == 8, "Calendar ABI");
_Static_assert(sizeof(ss_calendar_info) == 24, "Calendar info ABI");

HLE_FORCE("_ZN2nn4time10InitializeEv", "Use the host clock", Result, ss_time_init, void) { return RES_OK; }
HLE_FORCE("_ZN2nn4time13IsInitializedEv", "Host clock is available", int, ss_time_ready, void) { return 1; }
HLE_FORCE("_ZN2nn4time23StandardUserSystemClock14GetCurrentTimeEPNS0_9PosixTimeE", "Read the host clock", Result, ss_time_get, int64_t *out) {
    *out = (int64_t)time(NULL); return RES_OK;
}
HLE_FORCE("_ZN2nn4time23StandardUserSystemClock3nowEv", "Read the host clock", int64_t, ss_time_now, void) { return (int64_t)time(NULL); }
HLE_FORCE("_ZN2nn4time23StandardUserSystemClock9to_time_tERKNSt3__16chrono10time_pointINS0_17SystemClockTraitsENS3_8durationIlNS2_5ratioILl1ELl1EEEEEEE",
    "Convert the system clock value", int64_t, ss_time_seconds, const int64_t *value) { return *value; }
HLE_FORCE("_ZN2nn4time14ToCalendarTimeEPNS0_12CalendarTimeEPNS0_22CalendarAdditionalInfoERKNS0_9PosixTimeE",
    "Use the host time zone", Result, ss_time_calendar, ss_calendar *out, ss_calendar_info *info, const int64_t *value) {
    time_t seconds = *value;
    struct tm result;
    if (!localtime_r(&seconds, &result)) return RES_FAIL;
    if (out) *out = (ss_calendar){result.tm_year + 1900, result.tm_mon + 1,
        result.tm_mday, result.tm_hour, result.tm_min, result.tm_sec, 0};
    if (info) {
        *info = (ss_calendar_info){.wday = result.tm_wday, .yday = result.tm_yday,
            .dst = result.tm_isdst > 0, .offset = result.tm_gmtoff};
        strlcpy(info->zone, result.tm_zone ? result.tm_zone : "UTC", sizeof(info->zone));
    }
    return RES_OK;
}
