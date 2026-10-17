#include "k32.h"

#include <ctype.h>
#include <stdio.h>
#include <strings.h>
#include <wctype.h>

#define CP_ACP 0
#define CP_OEMCP 1
#define CP_UTF8 65001
#define CP_LATIN1 28591
#define CP_1252 1252
#define CP_ASCII 20127

#define NORM_IGNORECASE 0x1u
#define LINGUISTIC_IGNORECASE 0x10u
#define LCMAP_LOWERCASE 0x100u
#define LCMAP_UPPERCASE 0x200u

#define CT_CTYPE1 1
#define C1_UPPER 0x001
#define C1_LOWER 0x002
#define C1_DIGIT 0x004
#define C1_SPACE 0x008
#define C1_PUNCT 0x010
#define C1_CNTRL 0x020
#define C1_BLANK 0x040
#define C1_XDIGIT 0x080
#define C1_ALPHA 0x100
#define C1_DEFINED 0x200

#define LOCALE_NAME_MAX 85

typedef struct {
    UINT max_char_size;
    BYTE default_char[2];
    BYTE lead_byte[12];
} CPINFO;

typedef struct {
    UINT max_char_size;
    BYTE default_char[2];
    BYTE lead_byte[12];
    WCHAR unicode_default;
    UINT code_page;
    char name[260];
} CPINFOEXA;

typedef struct {
    UINT max_char_size;
    BYTE default_char[2];
    BYTE lead_byte[12];
    WCHAR unicode_default;
    UINT code_page;
    WCHAR name[260];
} CPINFOEXW;

struct locale {
    const char *lang;
    const char *name;
    WORD lcid;
    const char *english;
};

static const struct locale locales[] = {
    { "en", "en-US", 0x0409, "English" },
    { "ru", "ru-RU", 0x0419, "Russian" },
    { "uk", "uk-UA", 0x0422, "Ukrainian" },
    { "de", "de-DE", 0x0407, "German" },
    { "fr", "fr-FR", 0x040C, "French" },
    { "es", "es-ES", 0x0C0A, "Spanish" },
    { "it", "it-IT", 0x0410, "Italian" },
    { "pt", "pt-BR", 0x0416, "Portuguese" },
    { "pl", "pl-PL", 0x0415, "Polish" },
    { "tr", "tr-TR", 0x041F, "Turkish" },
    { "zh", "zh-CN", 0x0804, "Chinese" },
    { "ja", "ja-JP", 0x0411, "Japanese" },
    { "ko", "ko-KR", 0x0412, "Korean" },
    { "kk", "kk-KZ", 0x043F, "Kazakh" },
    { "uz", "uz-Latn-UZ", 0x0443, "Uzbek" },
};

static const struct locale *current_locale(void)
{
    const char *lang = getenv("LC_ALL");
    if (!lang || !*lang)
        lang = getenv("LANG");
    if (lang)
        for (size_t i = 0; i < sizeof locales / sizeof locales[0]; i++)
            if (!strncmp(lang, locales[i].lang, 2))
                return &locales[i];
    return &locales[0];
}

static bool is_utf8(UINT cp) { return cp == CP_ACP || cp == CP_OEMCP || cp == CP_UTF8 || cp == 3; }

#define MB_ERR_INVALID_CHARS 0x8

static bool utf8_valid(const unsigned char *s, int len)
{
    for (int i = 0; i < len;) {
        unsigned c = s[i];
        int extra = c < 0x80 ? 0 : c >= 0xC2 && c < 0xE0 ? 1 : c >= 0xE0 && c < 0xF0 ? 2 : c >= 0xF0 && c < 0xF5 ? 3 : -1;
        if (extra < 0 || i + extra >= len + (extra ? 0 : 1))
            return false;
        uint32_t cp = extra ? c & (0x3F >> extra) : c;
        for (int k = 1; k <= extra; k++) {
            if ((s[i + k] & 0xC0) != 0x80)
                return false;
            cp = (cp << 6) | (s[i + k] & 0x3F);
        }
        static const uint32_t min[] = { 0, 0x80, 0x800, 0x10000 };
        if (cp < min[extra] || cp > 0x10FFFF || (cp >= 0xD800 && cp < 0xE000))
            return false;
        i += extra + 1;
    }
    return true;
}

static int WINAPI MultiByteToWideChar(UINT cp, DWORD flags, LPCSTR src, int src_len, LPWSTR dst, int dst_len)
{
    if (!src || src_len == 0)
        return fail(ERROR_INVALID_PARAMETER);
    if (src_len < 0)
        src_len = strlen(src) + 1;
    if (is_utf8(cp)) {
        if ((flags & MB_ERR_INVALID_CHARS) && !utf8_valid((const unsigned char *)src, src_len))
            return fail(ERROR_NO_UNICODE_TRANSLATION);
        int n = utf8_to_utf16(src, src_len, dst_len ? dst : NULL, dst_len);
        return n < 0 ? fail(ERROR_INSUFFICIENT_BUFFER) : n;
    }
    if (!dst_len)
        return src_len;
    if (dst_len < src_len)
        return fail(ERROR_INSUFFICIENT_BUFFER);
    for (int i = 0; i < src_len; i++)
        dst[i] = (unsigned char)src[i];
    return src_len;
}

static int WINAPI WideCharToMultiByte(UINT cp, DWORD flags, LPCWSTR src, int src_len, LPSTR dst, int dst_len,
                                      LPCSTR default_char, BOOL *used_default)
{
    if (!src || src_len == 0)
        return fail(ERROR_INVALID_PARAMETER);
    if (src_len < 0)
        src_len = wstr_len(src) + 1;
    if (used_default)
        *used_default = FALSE;
    if (is_utf8(cp)) {
        int n = utf16_to_utf8(src, src_len, dst_len ? dst : NULL, dst_len);
        return n < 0 ? fail(ERROR_INSUFFICIENT_BUFFER) : n;
    }
    if (!dst_len)
        return src_len;
    if (dst_len < src_len)
        return fail(ERROR_INSUFFICIENT_BUFFER);
    char def = default_char ? *default_char : '?';
    for (int i = 0; i < src_len; i++) {
        bool fits = cp == CP_ASCII ? src[i] < 0x80 : src[i] < 0x100;
        dst[i] = fits ? (char)src[i] : def;
        if (!fits && used_default)
            *used_default = TRUE;
    }
    return src_len;
}

static UINT WINAPI GetACP(void) { return CP_UTF8; }
static UINT WINAPI GetOEMCP(void) { return CP_UTF8; }

static BOOL WINAPI IsValidCodePage(UINT cp)
{
    return is_utf8(cp) || cp == CP_LATIN1 || cp == CP_1252 || cp == CP_ASCII || cp == 437 || cp == 1200;
}

static BOOL WINAPI GetCPInfo(UINT cp, CPINFO *info)
{
    if (!IsValidCodePage(cp))
        return fail(ERROR_INVALID_PARAMETER);
    memset(info, 0, sizeof *info);
    info->max_char_size = is_utf8(cp) ? 4 : 1;
    info->default_char[0] = '?';
    return TRUE;
}

static BOOL WINAPI GetCPInfoExA(UINT cp, DWORD flags, CPINFOEXA *info)
{
    if (!GetCPInfo(cp, (CPINFO *)info))
        return FALSE;
    info->unicode_default = '?';
    info->code_page = is_utf8(cp) ? CP_UTF8 : cp;
    snprintf(info->name, sizeof info->name, "%u", info->code_page);
    return TRUE;
}

static BOOL WINAPI GetCPInfoExW(UINT cp, DWORD flags, CPINFOEXW *info)
{
    if (!GetCPInfo(cp, (CPINFO *)info))
        return FALSE;
    info->unicode_default = '?';
    info->code_page = is_utf8(cp) ? CP_UTF8 : cp;
    char name[16];
    snprintf(name, sizeof name, "%u", info->code_page);
    utf8_to_utf16(name, -1, info->name, 260);
    return TRUE;
}

static BOOL WINAPI IsDBCSLeadByte(BYTE c) { return FALSE; }
static BOOL WINAPI IsDBCSLeadByteEx(UINT cp, BYTE c) { return FALSE; }

static int WINAPI lstrlenA(LPCSTR s) { return s ? strlen(s) : 0; }
static int WINAPI lstrlenW(LPCWSTR s) { return s ? wstr_len(s) : 0; }
static LPSTR WINAPI lstrcpyA(LPSTR d, LPCSTR s) { return strcpy(d, s); }
static LPSTR WINAPI lstrcatA(LPSTR d, LPCSTR s) { return strcat(d, s); }

static LPWSTR WINAPI lstrcpyW(LPWSTR d, LPCWSTR s)
{
    memcpy(d, s, (wstr_len(s) + 1) * sizeof(WCHAR));
    return d;
}

static LPWSTR WINAPI lstrcatW(LPWSTR d, LPCWSTR s)
{
    lstrcpyW(d + wstr_len(d), s);
    return d;
}

static LPSTR WINAPI lstrcpynA(LPSTR d, LPCSTR s, int n)
{
    if (n <= 0)
        return d;
    int i = 0;
    for (; i < n - 1 && s[i]; i++)
        d[i] = s[i];
    d[i] = 0;
    return d;
}

static LPWSTR WINAPI lstrcpynW(LPWSTR d, LPCWSTR s, int n)
{
    if (n <= 0)
        return d;
    int i = 0;
    for (; i < n - 1 && s[i]; i++)
        d[i] = s[i];
    d[i] = 0;
    return d;
}

static WCHAR fold(WCHAR c) { return c < 0xD800 ? (WCHAR)towlower(c) : c; }
static WCHAR upper(WCHAR c) { return c < 0xD800 ? (WCHAR)towupper(c) : c; }

static int compare_w(LPCWSTR a, int na, LPCWSTR b, int nb, bool ignore_case)
{
    if (na < 0)
        na = wstr_len(a);
    if (nb < 0)
        nb = wstr_len(b);
    int n = na < nb ? na : nb;
    for (int i = 0; i < n; i++) {
        WCHAR x = ignore_case ? fold(a[i]) : a[i];
        WCHAR y = ignore_case ? fold(b[i]) : b[i];
        if (x != y)
            return x < y ? 1 : 3;
    }
    return na < nb ? 1 : na > nb ? 3 : 2;
}

static int WINAPI lstrcmpA(LPCSTR a, LPCSTR b)
{
    int r = strcmp(a, b);
    return r < 0 ? -1 : r > 0;
}

static int WINAPI lstrcmpiA(LPCSTR a, LPCSTR b)
{
    int r = strcasecmp(a, b);
    return r < 0 ? -1 : r > 0;
}

static int WINAPI lstrcmpW(LPCWSTR a, LPCWSTR b) { return compare_w(a, -1, b, -1, false) - 2; }
static int WINAPI lstrcmpiW(LPCWSTR a, LPCWSTR b) { return compare_w(a, -1, b, -1, true) - 2; }

static int WINAPI CompareStringW(DWORD lcid, DWORD flags, LPCWSTR a, int na, LPCWSTR b, int nb)
{
    return compare_w(a, na, b, nb, flags & (NORM_IGNORECASE | LINGUISTIC_IGNORECASE));
}

static int WINAPI CompareStringEx(LPCWSTR locale, DWORD flags, LPCWSTR a, int na, LPCWSTR b, int nb,
                                  void *v, void *r, LONG_PTR lparam)
{
    return CompareStringW(0, flags, a, na, b, nb);
}

static int WINAPI CompareStringOrdinal(LPCWSTR a, int na, LPCWSTR b, int nb, BOOL ignore_case)
{
    return compare_w(a, na, b, nb, ignore_case);
}

static int WINAPI CompareStringA(DWORD lcid, DWORD flags, LPCSTR a, int na, LPCSTR b, int nb)
{
    WCHAR *x = host_to_wide(a), *y = host_to_wide(b);
    int r = CompareStringW(lcid, flags, x, na < 0 ? -1 : (int)wstr_len(x), y, nb < 0 ? -1 : (int)wstr_len(y));
    free(x);
    free(y);
    return r;
}

static WORD ctype1(WCHAR c)
{
    WORD t = C1_DEFINED;
    if (iswupper(c))
        t |= C1_UPPER | C1_ALPHA;
    if (iswlower(c))
        t |= C1_LOWER | C1_ALPHA;
    if (iswalpha(c))
        t |= C1_ALPHA;
    if (iswdigit(c))
        t |= C1_DIGIT;
    if (iswspace(c))
        t |= C1_SPACE;
    if (iswpunct(c))
        t |= C1_PUNCT;
    if (iswcntrl(c))
        t |= C1_CNTRL;
    if (c == ' ' || c == '\t')
        t |= C1_BLANK;
    if (iswxdigit(c))
        t |= C1_XDIGIT;
    return t;
}

static BOOL WINAPI GetStringTypeW(DWORD type, LPCWSTR s, int n, WORD *out)
{
    if (n < 0)
        n = wstr_len(s) + 1;
    for (int i = 0; i < n; i++)
        out[i] = type == CT_CTYPE1 ? ctype1(s[i]) : 0;
    return TRUE;
}

static BOOL WINAPI GetStringTypeExW(DWORD lcid, DWORD type, LPCWSTR s, int n, WORD *out)
{
    return GetStringTypeW(type, s, n, out);
}

static BOOL WINAPI GetStringTypeA(DWORD lcid, DWORD type, LPCSTR s, int n, WORD *out)
{
    if (n < 0)
        n = strlen(s) + 1;
    for (int i = 0; i < n; i++)
        out[i] = type == CT_CTYPE1 ? ctype1((unsigned char)s[i]) : 0;
    return TRUE;
}

static int WINAPI LCMapStringW(DWORD lcid, DWORD flags, LPCWSTR src, int n, LPWSTR dst, int dst_len)
{
    if (n < 0)
        n = wstr_len(src) + 1;
    if (!dst_len)
        return n;
    if (dst_len < n)
        return fail(ERROR_INSUFFICIENT_BUFFER);
    for (int i = 0; i < n; i++)
        dst[i] = flags & LCMAP_LOWERCASE ? fold(src[i]) : flags & LCMAP_UPPERCASE ? upper(src[i]) : src[i];
    return n;
}

static int WINAPI LCMapStringEx(LPCWSTR locale, DWORD flags, LPCWSTR src, int n, LPWSTR dst, int dst_len,
                                void *v, void *r, LONG_PTR lparam)
{
    return LCMapStringW(0, flags, src, n, dst, dst_len);
}

static int WINAPI LCMapStringA(DWORD lcid, DWORD flags, LPCSTR src, int n, LPSTR dst, int dst_len)
{
    if (n < 0)
        n = strlen(src) + 1;
    if (!dst_len)
        return n;
    if (dst_len < n)
        return fail(ERROR_INSUFFICIENT_BUFFER);
    for (int i = 0; i < n; i++)
        dst[i] = flags & LCMAP_LOWERCASE ? tolower((unsigned char)src[i])
                 : flags & LCMAP_UPPERCASE ? toupper((unsigned char)src[i]) : src[i];
    return n;
}

static DWORD WINAPI GetUserDefaultLCID(void) { return current_locale()->lcid; }
static DWORD WINAPI GetSystemDefaultLCID(void) { return current_locale()->lcid; }
static DWORD WINAPI GetThreadLocale(void) { return current_locale()->lcid; }
static BOOL WINAPI SetThreadLocale(DWORD lcid) { return TRUE; }
static WORD WINAPI GetUserDefaultLangID(void) { return current_locale()->lcid; }
static WORD WINAPI GetSystemDefaultLangID(void) { return current_locale()->lcid; }
static WORD WINAPI GetUserDefaultUILanguage(void) { return current_locale()->lcid; }
static WORD WINAPI GetSystemDefaultUILanguage(void) { return current_locale()->lcid; }
static BOOL WINAPI IsValidLocale(DWORD lcid, DWORD flags) { return TRUE; }
static BOOL WINAPI IsValidLocaleName(LPCWSTR name) { return TRUE; }

static int WINAPI GetUserDefaultLocaleName(LPWSTR out, int size)
{
    return copy_out_w(current_locale()->name, out, size) + 1;
}

static int WINAPI GetSystemDefaultLocaleName(LPWSTR out, int size) { return GetUserDefaultLocaleName(out, size); }

static DWORD WINAPI LocaleNameToLCID(LPCWSTR name, DWORD flags)
{
    if (!name)
        return current_locale()->lcid;
    char *n = wide_to_host(name);
    DWORD id = 0x0409;
    for (size_t i = 0; i < sizeof locales / sizeof locales[0]; i++)
        if (!strncasecmp(n, locales[i].lang, 2))
            id = locales[i].lcid;
    free(n);
    return id;
}

static int WINAPI LCIDToLocaleName(DWORD lcid, LPWSTR out, int size, DWORD flags)
{
    const char *name = current_locale()->name;
    for (size_t i = 0; i < sizeof locales / sizeof locales[0]; i++)
        if (locales[i].lcid == (lcid & 0xFFFF))
            name = locales[i].name;
    return copy_out_w(name, out, size) + 1;
}

static const char *locale_info(DWORD type)
{
    static char buf[32];
    const struct locale *l = current_locale();
    switch (type & 0xFFFF) {
    case 0x0E: return ".";
    case 0x0F: return ",";
    case 0x10: return "3;0";
    case 0x14: return "$";
    case 0x1F: return "M/d/yyyy";
    case 0x20: return "dddd, MMMM d, yyyy";
    case 0x1003: return "1";
    case 0x1004: return "65001";
    case 0x0B: return "65001";
    case 0x1001: return l->english;
    case 0x02: return l->english;
    case 0x59: return l->lang;
    case 0x5A: return strchr(l->name, '-') + 1;
    case 0x5C: return l->name;
    case 0x01:
        snprintf(buf, sizeof buf, "%04x", l->lcid);
        return buf;
    default: return NULL;
    }
}

static int WINAPI GetLocaleInfoA(DWORD lcid, DWORD type, LPSTR out, int size)
{
    const char *v = locale_info(type);
    if (!v)
        return fail(ERROR_INVALID_PARAMETER);
    if (!size)
        return strlen(v) + 1;
    DWORD r = copy_out_a(v, out, size);
    return r >= (DWORD)size ? 0 : (int)r + 1;
}

static int WINAPI GetLocaleInfoW(DWORD lcid, DWORD type, LPWSTR out, int size)
{
    const char *v = locale_info(type);
    if (!v)
        return fail(ERROR_INVALID_PARAMETER);
    if (!size)
        return utf8_to_utf16(v, -1, NULL, 0);
    DWORD r = copy_out_w(v, out, size);
    return r >= (DWORD)size ? 0 : (int)r + 1;
}

static int WINAPI GetLocaleInfoEx(LPCWSTR name, DWORD type, LPWSTR out, int size)
{
    return GetLocaleInfoW(0, type, out, size);
}

BIT_TABLE(k32_string,
          BIT_FN(MultiByteToWideChar), BIT_FN(WideCharToMultiByte),
          BIT_FN(GetACP), BIT_FN(GetOEMCP), BIT_FN(IsValidCodePage),
          BIT_FN(GetCPInfo), BIT_FN(GetCPInfoExA), BIT_FN(GetCPInfoExW),
          BIT_FN(IsDBCSLeadByte), BIT_FN(IsDBCSLeadByteEx),
          BIT_FN(lstrlenA), BIT_FN(lstrlenW), BIT_FN(lstrcpyA), BIT_FN(lstrcpyW),
          BIT_FN(lstrcpynA), BIT_FN(lstrcpynW), BIT_FN(lstrcatA), BIT_FN(lstrcatW),
          BIT_FN(lstrcmpA), BIT_FN(lstrcmpW), BIT_FN(lstrcmpiA), BIT_FN(lstrcmpiW),
          BIT_FN(CompareStringA), BIT_FN(CompareStringW), BIT_FN(CompareStringEx),
          BIT_FN(CompareStringOrdinal),
          BIT_FN(GetStringTypeA), BIT_FN(GetStringTypeW), BIT_FN(GetStringTypeExW),
          BIT_FN(LCMapStringA), BIT_FN(LCMapStringW), BIT_FN(LCMapStringEx),
          BIT_FN(GetUserDefaultLCID), BIT_FN(GetSystemDefaultLCID),
          BIT_FN(GetThreadLocale), BIT_FN(SetThreadLocale),
          BIT_FN(GetUserDefaultLangID), BIT_FN(GetSystemDefaultLangID),
          BIT_FN(GetUserDefaultUILanguage), BIT_FN(GetSystemDefaultUILanguage),
          BIT_FN(IsValidLocale), BIT_FN(IsValidLocaleName),
          BIT_FN(GetUserDefaultLocaleName), BIT_FN(GetSystemDefaultLocaleName),
          BIT_FN(LocaleNameToLCID), BIT_FN(LCIDToLocaleName),
          BIT_FN(GetLocaleInfoA), BIT_FN(GetLocaleInfoW), BIT_FN(GetLocaleInfoEx));
