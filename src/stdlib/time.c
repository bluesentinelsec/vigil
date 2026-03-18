/* BASL standard library: time module.
 *
 * Provides date/time operations using portable C time functions.
 * All timestamps are Unix timestamps (seconds since 1970-01-01 UTC).
 */

/* Enable strptime on glibc */
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

/* Disable MSVC warnings for sscanf */
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "basl/native_module.h"
#include "basl/type.h"
#include "basl/value.h"
#include "basl/vm.h"

#include "internal/basl_nanbox.h"
#include "platform/platform.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

/* ── Helpers ─────────────────────────────────────────────────────── */

static int64_t get_i64_arg(basl_vm_t *vm, size_t base, size_t idx) {
    basl_value_t v = basl_vm_stack_get(vm, base + idx);
    return basl_nanbox_decode_int(v);
}

static int32_t get_i32_arg(basl_vm_t *vm, size_t base, size_t idx) {
    basl_value_t v = basl_vm_stack_get(vm, base + idx);
    return basl_nanbox_decode_i32(v);
}

static bool get_string_arg(basl_vm_t *vm, size_t base, size_t idx,
                           const char **out, size_t *out_len) {
    basl_value_t v = basl_vm_stack_get(vm, base + idx);
    if (!basl_nanbox_is_object(v)) return false;
    basl_object_t *obj = (basl_object_t *)basl_nanbox_decode_ptr(v);
    if (!obj || basl_object_type(obj) != BASL_OBJECT_STRING) return false;
    *out = basl_string_object_c_str(obj);
    *out_len = basl_string_object_length(obj);
    return true;
}

static basl_status_t push_i64(basl_vm_t *vm, int64_t val, basl_error_t *error) {
    basl_value_t v = basl_nanbox_encode_int(val);
    return basl_vm_stack_push(vm, &v, error);
}

static basl_status_t push_i32(basl_vm_t *vm, int32_t val, basl_error_t *error) {
    basl_value_t v = basl_nanbox_encode_i32(val);
    return basl_vm_stack_push(vm, &v, error);
}

static basl_status_t push_bool(basl_vm_t *vm, int val, basl_error_t *error) {
    basl_value_t v;
    basl_value_init_bool(&v, val);
    return basl_vm_stack_push(vm, &v, error);
}

static basl_status_t push_string(basl_vm_t *vm, const char *str, size_t len,
                                  basl_error_t *error) {
    basl_object_t *obj = NULL;
    basl_status_t s = basl_string_object_new(basl_vm_runtime(vm), str, len, &obj, error);
    if (s != BASL_STATUS_OK) return s;
    basl_value_t v;
    basl_value_init_object(&v, &obj);
    s = basl_vm_stack_push(vm, &v, error);
    basl_value_release(&v);
    return s;
}

/* ── time.now() -> i64 ───────────────────────────────────────────── */

static basl_status_t time_now(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    (void)arg_count;
    return push_i64(vm, (int64_t)time(NULL), error);
}

/* ── time.now_ms() -> i64 ────────────────────────────────────────── */

static basl_status_t time_now_ms(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    int64_t ms;
    (void)arg_count;
#ifdef _WIN32
    FILETIME ft;
    ULARGE_INTEGER uli;
    GetSystemTimeAsFileTime(&ft);
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    /* FILETIME is 100ns intervals since 1601-01-01 */
    ms = (int64_t)((uli.QuadPart - 116444736000000000ULL) / 10000);
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    ms = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif
    return push_i64(vm, ms, error);
}

/* ── time.now_ns() -> i64 ────────────────────────────────────────── */

static basl_status_t time_now_ns(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    int64_t ns;
    (void)arg_count;
#ifdef _WIN32
    FILETIME ft;
    ULARGE_INTEGER uli;
    GetSystemTimeAsFileTime(&ft);
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    ns = (int64_t)((uli.QuadPart - 116444736000000000ULL) * 100);
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    ns = (int64_t)tv.tv_sec * 1000000000LL + (int64_t)tv.tv_usec * 1000;
#endif
    return push_i64(vm, ns, error);
}

/* ── time.sleep(ms: i64) ─────────────────────────────────────────── */

static basl_status_t time_sleep(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t ms = get_i64_arg(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);
    if (ms > 0) {
        basl_platform_thread_sleep((uint64_t)ms);
    }
    (void)error;
    return BASL_STATUS_OK;
}

/* ── Component extraction helpers ────────────────────────────────── */

static struct tm *get_local_tm(int64_t ts, struct tm *storage) {
    time_t t = (time_t)ts;
#ifdef _WIN32
    if (localtime_s(storage, &t) != 0) return NULL;
    return storage;
#else
    (void)storage;
    return localtime(&t);
#endif
}

/* ── time.year(ts: i64) -> i32 ───────────────────────────────────── */

static basl_status_t time_year(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    struct tm storage, *tm;
    basl_vm_stack_pop_n(vm, arg_count);
    tm = get_local_tm(ts, &storage);
    return push_i32(vm, tm ? tm->tm_year + 1900 : 0, error);
}

/* ── time.month(ts: i64) -> i32 ──────────────────────────────────── */

static basl_status_t time_month(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    struct tm storage, *tm;
    basl_vm_stack_pop_n(vm, arg_count);
    tm = get_local_tm(ts, &storage);
    return push_i32(vm, tm ? tm->tm_mon + 1 : 0, error);
}

/* ── time.day(ts: i64) -> i32 ────────────────────────────────────── */

static basl_status_t time_day(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    struct tm storage, *tm;
    basl_vm_stack_pop_n(vm, arg_count);
    tm = get_local_tm(ts, &storage);
    return push_i32(vm, tm ? tm->tm_mday : 0, error);
}

/* ── time.hour(ts: i64) -> i32 ───────────────────────────────────── */

static basl_status_t time_hour(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    struct tm storage, *tm;
    basl_vm_stack_pop_n(vm, arg_count);
    tm = get_local_tm(ts, &storage);
    return push_i32(vm, tm ? tm->tm_hour : 0, error);
}

/* ── time.minute(ts: i64) -> i32 ─────────────────────────────────── */

static basl_status_t time_minute(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    struct tm storage, *tm;
    basl_vm_stack_pop_n(vm, arg_count);
    tm = get_local_tm(ts, &storage);
    return push_i32(vm, tm ? tm->tm_min : 0, error);
}

/* ── time.second(ts: i64) -> i32 ─────────────────────────────────── */

static basl_status_t time_second(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    struct tm storage, *tm;
    basl_vm_stack_pop_n(vm, arg_count);
    tm = get_local_tm(ts, &storage);
    return push_i32(vm, tm ? tm->tm_sec : 0, error);
}

/* ── time.weekday(ts: i64) -> i32 ────────────────────────────────── */

static basl_status_t time_weekday(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    struct tm storage, *tm;
    basl_vm_stack_pop_n(vm, arg_count);
    tm = get_local_tm(ts, &storage);
    return push_i32(vm, tm ? tm->tm_wday : 0, error);
}

/* ── time.yearday(ts: i64) -> i32 ────────────────────────────────── */

static basl_status_t time_yearday(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    struct tm storage, *tm;
    basl_vm_stack_pop_n(vm, arg_count);
    tm = get_local_tm(ts, &storage);
    return push_i32(vm, tm ? tm->tm_yday + 1 : 0, error);
}

/* ── time.is_dst(ts: i64) -> bool ────────────────────────────────── */

static basl_status_t time_is_dst(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    struct tm storage, *tm;
    basl_vm_stack_pop_n(vm, arg_count);
    tm = get_local_tm(ts, &storage);
    return push_bool(vm, tm && tm->tm_isdst > 0, error);
}

/* ── time.utc_offset() -> i32 ────────────────────────────────────── */

static basl_status_t time_utc_offset(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    time_t now = time(NULL);
    struct tm local_tm, utc_tm;
    int32_t offset;
    (void)arg_count;

#ifdef _WIN32
    localtime_s(&local_tm, &now);
    gmtime_s(&utc_tm, &now);
#else
    local_tm = *localtime(&now);
    utc_tm = *gmtime(&now);
#endif

    offset = (int32_t)((local_tm.tm_hour - utc_tm.tm_hour) * 3600 +
                       (local_tm.tm_min - utc_tm.tm_min) * 60);
    /* Handle day boundary */
    if (local_tm.tm_mday != utc_tm.tm_mday) {
        if (local_tm.tm_mday > utc_tm.tm_mday || 
            (local_tm.tm_mday == 1 && utc_tm.tm_mday > 1)) {
            offset += 86400;
        } else {
            offset -= 86400;
        }
    }
    return push_i32(vm, offset, error);
}

/* ── time.date(y, m, d, h, min, s) -> i64 ────────────────────────── */

static basl_status_t time_date(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int32_t y = get_i32_arg(vm, base, 0);
    int32_t m = get_i32_arg(vm, base, 1);
    int32_t d = get_i32_arg(vm, base, 2);
    int32_t h = get_i32_arg(vm, base, 3);
    int32_t min = get_i32_arg(vm, base, 4);
    int32_t s = get_i32_arg(vm, base, 5);
    struct tm tm_val;
    time_t result;

    basl_vm_stack_pop_n(vm, arg_count);

    memset(&tm_val, 0, sizeof(tm_val));
    tm_val.tm_year = y - 1900;
    tm_val.tm_mon = m - 1;
    tm_val.tm_mday = d;
    tm_val.tm_hour = h;
    tm_val.tm_min = min;
    tm_val.tm_sec = s;
    tm_val.tm_isdst = -1;

    result = mktime(&tm_val);
    return push_i64(vm, (int64_t)result, error);
}

/* ── time.format(ts: i64, fmt: string) -> string ─────────────────── */

static basl_status_t time_format(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    const char *fmt;
    size_t fmt_len;
    struct tm storage, *tm;
    char buf[256];
    char fmt_buf[128];
    size_t len;

    if (!get_string_arg(vm, base, 1, &fmt, &fmt_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_string(vm, "", 0, error);
    }
    basl_vm_stack_pop_n(vm, arg_count);

    tm = get_local_tm(ts, &storage);
    if (!tm) {
        return push_string(vm, "", 0, error);
    }

    /* Copy format to null-terminated buffer */
    if (fmt_len >= sizeof(fmt_buf)) fmt_len = sizeof(fmt_buf) - 1;
    memcpy(fmt_buf, fmt, fmt_len);
    fmt_buf[fmt_len] = '\0';

    len = strftime(buf, sizeof(buf), fmt_buf, tm);
    return push_string(vm, buf, len, error);
}

/* ── time.parse(s: string, fmt: string) -> i64 ───────────────────── */

#ifndef _WIN32
/* strptime is POSIX, not available on Windows */
static basl_status_t time_parse(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *str, *fmt;
    size_t str_len, fmt_len;
    struct tm tm_val;
    char str_buf[256], fmt_buf[128];
    time_t result;

    if (!get_string_arg(vm, base, 0, &str, &str_len) ||
        !get_string_arg(vm, base, 1, &fmt, &fmt_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_i64(vm, -1, error);
    }
    basl_vm_stack_pop_n(vm, arg_count);

    if (str_len >= sizeof(str_buf)) str_len = sizeof(str_buf) - 1;
    memcpy(str_buf, str, str_len);
    str_buf[str_len] = '\0';

    if (fmt_len >= sizeof(fmt_buf)) fmt_len = sizeof(fmt_buf) - 1;
    memcpy(fmt_buf, fmt, fmt_len);
    fmt_buf[fmt_len] = '\0';

    memset(&tm_val, 0, sizeof(tm_val));
    tm_val.tm_isdst = -1;

    if (strptime(str_buf, fmt_buf, &tm_val) == NULL) {
        return push_i64(vm, -1, error);
    }

    result = mktime(&tm_val);
    return push_i64(vm, (int64_t)result, error);
}
#else
/* Windows fallback: parse ISO 8601 format only */
static basl_status_t time_parse(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *str, *fmt;
    size_t str_len, fmt_len;
    struct tm tm_val;
    char str_buf[256];
    time_t result;
    int y, m, d, h, min, s;

    if (!get_string_arg(vm, base, 0, &str, &str_len) ||
        !get_string_arg(vm, base, 1, &fmt, &fmt_len)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_i64(vm, -1, error);
    }
    basl_vm_stack_pop_n(vm, arg_count);

    if (str_len >= sizeof(str_buf)) str_len = sizeof(str_buf) - 1;
    memcpy(str_buf, str, str_len);
    str_buf[str_len] = '\0';

    /* Try ISO 8601 format */
    if (sscanf(str_buf, "%d-%d-%dT%d:%d:%d", &y, &m, &d, &h, &min, &s) == 6 ||
        sscanf(str_buf, "%d-%d-%d %d:%d:%d", &y, &m, &d, &h, &min, &s) == 6) {
        memset(&tm_val, 0, sizeof(tm_val));
        tm_val.tm_year = y - 1900;
        tm_val.tm_mon = m - 1;
        tm_val.tm_mday = d;
        tm_val.tm_hour = h;
        tm_val.tm_min = min;
        tm_val.tm_sec = s;
        tm_val.tm_isdst = -1;
        result = mktime(&tm_val);
        return push_i64(vm, (int64_t)result, error);
    }

    /* Try date only */
    if (sscanf(str_buf, "%d-%d-%d", &y, &m, &d) == 3) {
        memset(&tm_val, 0, sizeof(tm_val));
        tm_val.tm_year = y - 1900;
        tm_val.tm_mon = m - 1;
        tm_val.tm_mday = d;
        tm_val.tm_isdst = -1;
        result = mktime(&tm_val);
        return push_i64(vm, (int64_t)result, error);
    }

    return push_i64(vm, -1, error);
}
#endif

/* ── Arithmetic functions ────────────────────────────────────────── */

static basl_status_t time_add_days(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    int32_t n = get_i32_arg(vm, base, 1);
    basl_vm_stack_pop_n(vm, arg_count);
    return push_i64(vm, ts + (int64_t)n * 86400, error);
}

static basl_status_t time_add_hours(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    int32_t n = get_i32_arg(vm, base, 1);
    basl_vm_stack_pop_n(vm, arg_count);
    return push_i64(vm, ts + (int64_t)n * 3600, error);
}

static basl_status_t time_add_minutes(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    int32_t n = get_i32_arg(vm, base, 1);
    basl_vm_stack_pop_n(vm, arg_count);
    return push_i64(vm, ts + (int64_t)n * 60, error);
}

static basl_status_t time_add_seconds(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t ts = get_i64_arg(vm, base, 0);
    int64_t n = get_i64_arg(vm, base, 1);
    basl_vm_stack_pop_n(vm, arg_count);
    return push_i64(vm, ts + n, error);
}

static basl_status_t time_diff_days(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t a = get_i64_arg(vm, base, 0);
    int64_t b = get_i64_arg(vm, base, 1);
    basl_vm_stack_pop_n(vm, arg_count);
    return push_i64(vm, (a - b) / 86400, error);
}

/* ── Module definition ───────────────────────────────────────────── */

static const int i64_param[] = {BASL_TYPE_I64};
static const int i64_i32_param[] = {BASL_TYPE_I64, BASL_TYPE_I32};
static const int i64_i64_param[] = {BASL_TYPE_I64, BASL_TYPE_I64};
static const int i64_str_param[] = {BASL_TYPE_I64, BASL_TYPE_STRING};
static const int str_str_param[] = {BASL_TYPE_STRING, BASL_TYPE_STRING};
static const int date_params[] = {BASL_TYPE_I32, BASL_TYPE_I32, BASL_TYPE_I32,
                                   BASL_TYPE_I32, BASL_TYPE_I32, BASL_TYPE_I32};

static const basl_native_module_function_t time_functions[] = {
    {"now", 3U, time_now, 0U, NULL, BASL_TYPE_I64, 1U, NULL, 0, NULL, NULL},
    {"now_ms", 6U, time_now_ms, 0U, NULL, BASL_TYPE_I64, 1U, NULL, 0, NULL, NULL},
    {"now_ns", 6U, time_now_ns, 0U, NULL, BASL_TYPE_I64, 1U, NULL, 0, NULL, NULL},
    {"sleep", 5U, time_sleep, 1U, i64_param, BASL_TYPE_VOID, 0U, NULL, 0, NULL, NULL},
    {"year", 4U, time_year, 1U, i64_param, BASL_TYPE_I32, 1U, NULL, 0, NULL, NULL},
    {"month", 5U, time_month, 1U, i64_param, BASL_TYPE_I32, 1U, NULL, 0, NULL, NULL},
    {"day", 3U, time_day, 1U, i64_param, BASL_TYPE_I32, 1U, NULL, 0, NULL, NULL},
    {"hour", 4U, time_hour, 1U, i64_param, BASL_TYPE_I32, 1U, NULL, 0, NULL, NULL},
    {"minute", 6U, time_minute, 1U, i64_param, BASL_TYPE_I32, 1U, NULL, 0, NULL, NULL},
    {"second", 6U, time_second, 1U, i64_param, BASL_TYPE_I32, 1U, NULL, 0, NULL, NULL},
    {"weekday", 7U, time_weekday, 1U, i64_param, BASL_TYPE_I32, 1U, NULL, 0, NULL, NULL},
    {"yearday", 7U, time_yearday, 1U, i64_param, BASL_TYPE_I32, 1U, NULL, 0, NULL, NULL},
    {"is_dst", 6U, time_is_dst, 1U, i64_param, BASL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    {"utc_offset", 10U, time_utc_offset, 0U, NULL, BASL_TYPE_I32, 1U, NULL, 0, NULL, NULL},
    {"date", 4U, time_date, 6U, date_params, BASL_TYPE_I64, 1U, NULL, 0, NULL, NULL},
    {"format", 6U, time_format, 2U, i64_str_param, BASL_TYPE_STRING, 1U, NULL, 0, NULL, NULL},
    {"parse", 5U, time_parse, 2U, str_str_param, BASL_TYPE_I64, 1U, NULL, 0, NULL, NULL},
    {"add_days", 8U, time_add_days, 2U, i64_i32_param, BASL_TYPE_I64, 1U, NULL, 0, NULL, NULL},
    {"add_hours", 9U, time_add_hours, 2U, i64_i32_param, BASL_TYPE_I64, 1U, NULL, 0, NULL, NULL},
    {"add_minutes", 11U, time_add_minutes, 2U, i64_i32_param, BASL_TYPE_I64, 1U, NULL, 0, NULL, NULL},
    {"add_seconds", 11U, time_add_seconds, 2U, i64_i64_param, BASL_TYPE_I64, 1U, NULL, 0, NULL, NULL},
    {"diff_days", 9U, time_diff_days, 2U, i64_i64_param, BASL_TYPE_I64, 1U, NULL, 0, NULL, NULL},
};

#define TIME_FUNCTION_COUNT (sizeof(time_functions) / sizeof(time_functions[0]))

BASL_API const basl_native_module_t basl_stdlib_time = {
    "time", 4U,
    time_functions, TIME_FUNCTION_COUNT,
    NULL, 0U
};
