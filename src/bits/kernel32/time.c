#include "k32.h"

#include <screw/sync.h>


typedef struct {
    WORD year;
    WORD month;
    WORD day_of_week;
    WORD day;
    WORD hour;
    WORD minute;
    WORD second;
    WORD milliseconds;
} SYSTEMTIME;

typedef struct {
    LONG bias;
    WCHAR standard_name[32];
    SYSTEMTIME standard_date;
    LONG standard_bias;
    WCHAR daylight_name[32];
    SYSTEMTIME daylight_date;
    LONG daylight_bias;
} TIME_ZONE_INFORMATION;

#define TIME_ZONE_ID_UNKNOWN 0
#define TIME_ZONE_ID_STANDARD 1
#define TIME_ZONE_ID_DAYLIGHT 2

static uint64_t now_ticks(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ticks_from_timespec(&ts);
}

static DWORD WINAPI GetTickCount(void) { return (DWORD)monotonic_ms(); }
static ULONGLONG WINAPI GetTickCount64(void) { return monotonic_ms(); }

static BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER *out)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    out->quad = ts.tv_sec * 10000000ll + ts.tv_nsec / 100;
    return TRUE;
}

static BOOL WINAPI QueryPerformanceFrequency(LARGE_INTEGER *out)
{
    out->quad = 10000000;
    return TRUE;
}

static void WINAPI GetSystemTimeAsFileTime(FILETIME *ft) { *ft = filetime_from_ticks(now_ticks()); }
static void WINAPI GetSystemTimePreciseAsFileTime(FILETIME *ft) { *ft = filetime_from_ticks(now_ticks()); }

static void to_system_time(uint64_t ticks, bool local, SYSTEMTIME *st)
{
    time_t secs = (ticks - UNIX_EPOCH_TICKS) / 10000000ull;
    struct tm tm;
    if (local)
        localtime_r(&secs, &tm);
    else
        gmtime_r(&secs, &tm);
    st->year = tm.tm_year + 1900;
    st->month = tm.tm_mon + 1;
    st->day_of_week = tm.tm_wday;
    st->day = tm.tm_mday;
    st->hour = tm.tm_hour;
    st->minute = tm.tm_min;
    st->second = tm.tm_sec;
    st->milliseconds = (ticks / 10000) % 1000;
}

static uint64_t from_system_time(const SYSTEMTIME *st, bool local)
{
    struct tm tm = {
        .tm_year = st->year - 1900, .tm_mon = st->month - 1, .tm_mday = st->day,
        .tm_hour = st->hour, .tm_min = st->minute, .tm_sec = st->second, .tm_isdst = -1,
    };
    time_t secs = local ? mktime(&tm) : timegm(&tm);
    return UNIX_EPOCH_TICKS + secs * 10000000ull + st->milliseconds * 10000ull;
}

static void WINAPI GetSystemTime(SYSTEMTIME *st) { to_system_time(now_ticks(), false, st); }
static void WINAPI GetLocalTime(SYSTEMTIME *st) { to_system_time(now_ticks(), true, st); }

static BOOL WINAPI FileTimeToSystemTime(const FILETIME *ft, SYSTEMTIME *st)
{
    to_system_time(ticks_from_filetime(ft), false, st);
    return TRUE;
}

static BOOL WINAPI SystemTimeToFileTime(const SYSTEMTIME *st, FILETIME *ft)
{
    *ft = filetime_from_ticks(from_system_time(st, false));
    return TRUE;
}

static LONG utc_offset_seconds(uint64_t ticks)
{
    time_t secs = (ticks - UNIX_EPOCH_TICKS) / 10000000ull;
    struct tm tm;
    localtime_r(&secs, &tm);
    return tm.tm_gmtoff;
}

static BOOL WINAPI FileTimeToLocalFileTime(const FILETIME *in, FILETIME *out)
{
    uint64_t t = ticks_from_filetime(in);
    *out = filetime_from_ticks(t + (int64_t)utc_offset_seconds(t) * 10000000ll);
    return TRUE;
}

static BOOL WINAPI LocalFileTimeToFileTime(const FILETIME *in, FILETIME *out)
{
    uint64_t t = ticks_from_filetime(in);
    *out = filetime_from_ticks(t - (int64_t)utc_offset_seconds(t) * 10000000ll);
    return TRUE;
}

static BOOL WINAPI SystemTimeToTzSpecificLocalTime(void *tz, const SYSTEMTIME *utc, SYSTEMTIME *local)
{
    to_system_time(from_system_time(utc, false), true, local);
    return TRUE;
}

static BOOL WINAPI TzSpecificLocalTimeToSystemTime(void *tz, const SYSTEMTIME *local, SYSTEMTIME *utc)
{
    to_system_time(from_system_time(local, true), false, utc);
    return TRUE;
}

static LONG WINAPI CompareFileTime(const FILETIME *a, const FILETIME *b)
{
    uint64_t x = ticks_from_filetime(a), y = ticks_from_filetime(b);
    return x < y ? -1 : x > y;
}

static DWORD WINAPI GetTimeZoneInformation(TIME_ZONE_INFORMATION *tz)
{
    memset(tz, 0, sizeof *tz);
    tzset();
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    tz->bias = -(LONG)(tm.tm_gmtoff / 60) + (tm.tm_isdst > 0 ? 60 : 0);
    tz->daylight_bias = -60;
    utf8_to_utf16(tzname[0], -1, tz->standard_name, 31);
    utf8_to_utf16(tzname[1], -1, tz->daylight_name, 31);
    return tm.tm_isdst > 0 ? TIME_ZONE_ID_DAYLIGHT : TIME_ZONE_ID_STANDARD;
}

static DWORD WINAPI GetDynamicTimeZoneInformation(TIME_ZONE_INFORMATION *tz)
{
    return GetTimeZoneInformation(tz);
}

static BOOL WINAPI QueryUnbiasedInterruptTime(ULONGLONG *out)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    *out = ts.tv_sec * 10000000ull + ts.tv_nsec / 100;
    return TRUE;
}

BIT_TABLE(k32_time,
          BIT_FN(GetTickCount), BIT_FN(GetTickCount64),
          BIT_FN(QueryPerformanceCounter), BIT_FN(QueryPerformanceFrequency),
          BIT_FN(GetSystemTimeAsFileTime), BIT_FN(GetSystemTimePreciseAsFileTime),
          BIT_FN(GetSystemTime), BIT_FN(GetLocalTime),
          BIT_FN(FileTimeToSystemTime), BIT_FN(SystemTimeToFileTime),
          BIT_FN(FileTimeToLocalFileTime), BIT_FN(LocalFileTimeToFileTime),
          BIT_FN(SystemTimeToTzSpecificLocalTime), BIT_FN(TzSpecificLocalTimeToSystemTime),
          BIT_FN(CompareFileTime), BIT_FN(GetTimeZoneInformation), BIT_FN(GetDynamicTimeZoneInformation),
          BIT_FN(QueryUnbiasedInterruptTime));
