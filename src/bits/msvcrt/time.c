#include "crt.h"

#include <errno.h>
#include <time.h>

struct wtm {
    int sec, min, hour, mday, mon, year, wday, yday, isdst;
};

struct timeb64 {
    int64_t time;
    unsigned short millitm;
    short timezone;
    short dstflag;
};

struct timespec64 {
    int64_t sec;
    LONG nsec;
};

static LONG timezone_;
static int daylight_;
static char *tzname_[2];
static struct timespec start;

__attribute__((constructor)) static void clock_start(void)
{
    clock_gettime(CLOCK_MONOTONIC, &start);
    tzset();
    timezone_ = timezone;
    daylight_ = daylight;
    tzname_[0] = tzname[0];
    tzname_[1] = tzname[1];
}

static void to_win(const struct tm *h, struct wtm *w)
{
    *w = (struct wtm){ h->tm_sec, h->tm_min, h->tm_hour, h->tm_mday, h->tm_mon,
                       h->tm_year, h->tm_wday, h->tm_yday, h->tm_isdst };
}

static void to_host(const struct wtm *w, struct tm *h)
{
    memset(h, 0, sizeof *h);
    h->tm_sec = w->sec;
    h->tm_min = w->min;
    h->tm_hour = w->hour;
    h->tm_mday = w->mday;
    h->tm_mon = w->mon;
    h->tm_year = w->year;
    h->tm_wday = w->wday;
    h->tm_yday = w->yday;
    h->tm_isdst = w->isdst;
}

static int64_t WINAPI time64(int64_t *out)
{
    int64_t t = time(NULL);
    if (out)
        *out = t;
    return t;
}

static int32_t WINAPI time32(int32_t *out)
{
    int32_t t = (int32_t)time(NULL);
    if (out)
        *out = t;
    return t;
}

static LONG WINAPI clock_(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000;
}

static double WINAPI difftime_(int64_t a, int64_t b) { return (double)(a - b); }

static int64_t WINAPI mktime64(struct wtm *w)
{
    struct tm h;
    to_host(w, &h);
    time_t t = mktime(&h);
    to_win(&h, w);
    return t;
}

static int64_t WINAPI mkgmtime64(struct wtm *w)
{
    struct tm h;
    to_host(w, &h);
    time_t t = timegm(&h);
    to_win(&h, w);
    return t;
}

static __thread struct wtm tm_buf;

static struct wtm *WINAPI localtime64(const int64_t *t)
{
    time_t v = *t;
    struct tm h;
    if (!localtime_r(&v, &h))
        return NULL;
    to_win(&h, &tm_buf);
    return &tm_buf;
}

static struct wtm *WINAPI gmtime64(const int64_t *t)
{
    time_t v = *t;
    struct tm h;
    if (!gmtime_r(&v, &h))
        return NULL;
    to_win(&h, &tm_buf);
    return &tm_buf;
}

static struct wtm *WINAPI localtime32(const int32_t *t)
{
    int64_t v = *t;
    return localtime64(&v);
}

static struct wtm *WINAPI gmtime32(const int32_t *t)
{
    int64_t v = *t;
    return gmtime64(&v);
}

static int WINAPI localtime64_s(struct wtm *out, const int64_t *t)
{
    struct wtm *r = t ? localtime64(t) : NULL;
    if (!r)
        return EINVAL;
    *out = *r;
    return 0;
}

static int WINAPI gmtime64_s(struct wtm *out, const int64_t *t)
{
    struct wtm *r = t ? gmtime64(t) : NULL;
    if (!r)
        return EINVAL;
    *out = *r;
    return 0;
}

static __thread char text_buf[64];

static char *WINAPI asctime_(const struct wtm *w)
{
    struct tm h;
    to_host(w, &h);
    return asctime_r(&h, text_buf);
}

static char *WINAPI ctime64(const int64_t *t)
{
    time_t v = *t;
    return ctime_r(&v, text_buf);
}

static int WINAPI asctime_s(char *buf, size_t n, const struct wtm *w)
{
    char *s = asctime_(w);
    if (strlen(s) >= n)
        return ERANGE;
    strcpy(buf, s);
    return 0;
}

static int WINAPI ctime64_s(char *buf, size_t n, const int64_t *t)
{
    char *s = ctime64(t);
    if (strlen(s) >= n)
        return ERANGE;
    strcpy(buf, s);
    return 0;
}

static char *fix_format(const char *fmt)
{
    char *out = malloc(strlen(fmt) + 1), *o = out;
    for (const char *p = fmt; *p; p++) {
        *o++ = *p;
        if (*p == '%' && p[1] == '#')
            p++;
    }
    *o = 0;
    return out;
}

static size_t WINAPI strftime_(char *buf, size_t n, const char *fmt, const struct wtm *w)
{
    struct tm h;
    to_host(w, &h);
    char *f = fix_format(fmt);
    size_t r = strftime(buf, n, f, &h);
    free(f);
    return r;
}

static size_t WINAPI wcsftime_(WCHAR *buf, size_t n, const WCHAR *fmt, const struct wtm *w)
{
    char *f = wide_to_host(fmt);
    char tmp[1024];
    size_t r = strftime_(tmp, sizeof tmp, f, w);
    free(f);
    if (!r)
        return 0;
    int len = utf8_to_utf16(tmp, r, NULL, 0);
    if ((size_t)len >= n)
        return 0;
    utf8_to_utf16(tmp, r, buf, len);
    buf[len] = 0;
    return len;
}

static void WINAPI ftime64(struct timeb64 *tb)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    tb->time = ts.tv_sec;
    tb->millitm = ts.tv_nsec / 1000000;
    tb->timezone = timezone_ / 60;
    tb->dstflag = daylight_;
}

static int WINAPI ftime64_s(struct timeb64 *tb)
{
    ftime64(tb);
    return 0;
}

static int WINAPI timespec64_get(struct timespec64 *ts, int base)
{
    if (base != 1)
        return 0;
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    ts->sec = now.tv_sec;
    ts->nsec = now.tv_nsec;
    return base;
}

static void WINAPI _tzset(void) { clock_start(); }

static int WINAPI _get_timezone(LONG *out)
{
    *out = timezone_;
    return 0;
}

static int WINAPI _get_daylight(int *out)
{
    *out = daylight_;
    return 0;
}

static LONG *WINAPI __p__timezone(void) { return &timezone_; }
static int *WINAPI __p__daylight(void) { return &daylight_; }
static char **WINAPI __p__tzname(void) { return tzname_; }

static char *WINAPI _strdate(char *buf)
{
    time_t t = time(NULL);
    struct tm h;
    localtime_r(&t, &h);
    strftime(buf, 9, "%m/%d/%y", &h);
    return buf;
}

static char *WINAPI _strtime(char *buf)
{
    time_t t = time(NULL);
    struct tm h;
    localtime_r(&t, &h);
    strftime(buf, 9, "%H:%M:%S", &h);
    return buf;
}

BIT_TABLE(crt_time,
          BIT_AS("time", time64), BIT_AS("_time64", time64), BIT_AS("_time32", time32),
          BIT_AS("clock", clock_), BIT_AS("difftime", difftime_), BIT_AS("_difftime64", difftime_),
          BIT_AS("mktime", mktime64), BIT_AS("_mktime64", mktime64),
          BIT_AS("_mkgmtime", mkgmtime64), BIT_AS("_mkgmtime64", mkgmtime64),
          BIT_AS("localtime", localtime64), BIT_AS("_localtime64", localtime64),
          BIT_AS("_localtime32", localtime32),
          BIT_AS("gmtime", gmtime64), BIT_AS("_gmtime64", gmtime64), BIT_AS("_gmtime32", gmtime32),
          BIT_AS("localtime_s", localtime64_s), BIT_AS("_localtime64_s", localtime64_s),
          BIT_AS("gmtime_s", gmtime64_s), BIT_AS("_gmtime64_s", gmtime64_s),
          BIT_AS("asctime", asctime_), BIT_FN(asctime_s),
          BIT_AS("ctime", ctime64), BIT_AS("_ctime64", ctime64), BIT_AS("_ctime64_s", ctime64_s),
          BIT_AS("strftime", strftime_), BIT_AS("wcsftime", wcsftime_),
          BIT_AS("_ftime", ftime64), BIT_AS("_ftime64", ftime64), BIT_AS("_ftime64_s", ftime64_s),
          BIT_AS("_timespec64_get", timespec64_get), BIT_AS("timespec_get", timespec64_get),
          BIT_FN(_tzset), BIT_FN(_get_timezone), BIT_FN(_get_daylight),
          BIT_FN(__p__timezone), BIT_FN(__p__daylight), BIT_FN(__p__tzname),
          BIT_DATA("_timezone", timezone_), BIT_DATA("_daylight", daylight_), BIT_DATA("_tzname", tzname_),
          BIT_FN(_strdate), BIT_FN(_strtime));
