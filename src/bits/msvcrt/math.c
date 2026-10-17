#include "crt.h"
#include "wrap.h"

#include <fenv.h>
#include <math.h>

#define MCW_RC 0x300u
#define RC_NEAR 0x000u
#define RC_DOWN 0x100u
#define RC_UP 0x200u
#define RC_CHOP 0x300u
#define CW_DEFAULT 0x0009001Fu

#define D1(f) WRAP1(double, f, double)
#define D2(f) WRAP2(double, f, double, double)
#define F1(f) WRAP1(float, f, float)
#define F2(f) WRAP2(float, f, float, float)

D1(sin) D1(cos) D1(tan) D1(asin) D1(acos) D1(atan) D2(atan2)
D1(sinh) D1(cosh) D1(tanh) D1(asinh) D1(acosh) D1(atanh)
D1(exp) D1(exp2) D1(expm1) D1(log) D1(log10) D1(log2) D1(log1p) D1(logb)
D2(pow) D1(sqrt) D1(cbrt) D2(hypot)
D1(ceil) D1(floor) D1(fabs) D2(fmod) D1(round) D1(trunc) D1(rint) D1(nearbyint)
D2(fmin) D2(fmax) D2(fdim) D2(copysign) D2(remainder) D2(nextafter)
D1(erf) D1(erfc) D1(tgamma) D1(lgamma)

F1(sinf) F1(cosf) F1(tanf) F1(asinf) F1(acosf) F1(atanf) F2(atan2f)
F1(sinhf) F1(coshf) F1(tanhf)
F1(expf) F1(exp2f) F1(logf) F1(log10f) F1(log2f)
F2(powf) F1(sqrtf) F1(cbrtf) F2(hypotf)
F1(ceilf) F1(floorf) F1(fabsf) F2(fmodf) F1(roundf) F1(truncf) F1(rintf)
F2(fminf) F2(fmaxf) F2(copysignf)

static double WINAPI ldexp_(double x, int e) { return ldexp(x, e); }
static float WINAPI ldexpf_(float x, int e) { return ldexpf(x, e); }
static double WINAPI frexp_(double x, int *e) { return frexp(x, e); }
static float WINAPI frexpf_(float x, int *e) { return frexpf(x, e); }
static double WINAPI modf_(double x, double *i) { return modf(x, i); }
static float WINAPI modff_(float x, float *i) { return modff(x, i); }
static double WINAPI scalbn_(double x, int n) { return scalbn(x, n); }
static double WINAPI fma_(double a, double b, double c) { return fma(a, b, c); }
static float WINAPI fmaf_(float a, float b, float c) { return fmaf(a, b, c); }
static double WINAPI nan_(const char *s) { return nan(s); }
static float WINAPI nanf_(const char *s) { return nanf(s); }
static int WINAPI ilogb_(double x) { return ilogb(x); }

static LONG WINAPI lround_(double x) { return (LONG)lround(x); }
static LONG WINAPI lroundf_(float x) { return (LONG)lroundf(x); }
static int64_t WINAPI llround_(double x) { return llround(x); }
static int64_t WINAPI llroundf_(float x) { return llroundf(x); }
static LONG WINAPI lrint_(double x) { return (LONG)lrint(x); }
static LONG WINAPI lrintf_(float x) { return (LONG)lrintf(x); }
static int64_t WINAPI llrint_(double x) { return llrint(x); }

static int WINAPI _isnan(double x) { return isnan(x); }
static int WINAPI _isnanf(float x) { return isnan(x); }
static int WINAPI _finite(double x) { return isfinite(x); }
static int WINAPI _finitef(float x) { return isfinite(x); }
static double WINAPI _chgsign(double x) { return -x; }
static double WINAPI _scalb(double x, LONG e) { return scalbn(x, e); }

static short WINAPI _dclass(double x)
{
    switch (fpclassify(x)) {
    case FP_NAN: return 2;
    case FP_INFINITE: return 1;
    case FP_ZERO: return 0;
    case FP_SUBNORMAL: return -2;
    default: return -1;
    }
}

static short WINAPI _fdclass(float x) { return _dclass(x); }
static int WINAPI _dsign(double x) { return signbit(x) ? 0x8000 : 0; }
static int WINAPI _fdsign(float x) { return signbit(x) ? 0x8000 : 0; }

static int WINAPI _fpclass(double x)
{
    bool neg = signbit(x);
    switch (fpclassify(x)) {
    case FP_NAN: return 0x0002;
    case FP_INFINITE: return neg ? 0x0004 : 0x0200;
    case FP_ZERO: return neg ? 0x0020 : 0x0040;
    case FP_SUBNORMAL: return neg ? 0x0010 : 0x0080;
    default: return neg ? 0x0008 : 0x0100;
    }
}

static unsigned current_control(void)
{
    unsigned rc;
    switch (fegetround()) {
    case FE_DOWNWARD: rc = RC_DOWN; break;
    case FE_UPWARD: rc = RC_UP; break;
    case FE_TOWARDZERO: rc = RC_CHOP; break;
    default: rc = RC_NEAR; break;
    }
    return (CW_DEFAULT & ~MCW_RC) | rc;
}

static unsigned WINAPI _controlfp(unsigned value, unsigned mask)
{
    if (mask & MCW_RC) {
        switch (value & MCW_RC) {
        case RC_DOWN: fesetround(FE_DOWNWARD); break;
        case RC_UP: fesetround(FE_UPWARD); break;
        case RC_CHOP: fesetround(FE_TOWARDZERO); break;
        default: fesetround(FE_TONEAREST); break;
        }
    }
    return current_control();
}

static int WINAPI _controlfp_s(unsigned *current, unsigned value, unsigned mask)
{
    unsigned r = _controlfp(value, mask);
    if (current)
        *current = r;
    return 0;
}

static unsigned WINAPI _clearfp(void)
{
    int flags = fetestexcept(FE_ALL_EXCEPT);
    feclearexcept(FE_ALL_EXCEPT);
    return (flags & FE_INVALID ? 0x10 : 0) | (flags & FE_DIVBYZERO ? 0x08 : 0) |
           (flags & FE_OVERFLOW ? 0x04 : 0) | (flags & FE_UNDERFLOW ? 0x02 : 0) |
           (flags & FE_INEXACT ? 0x01 : 0);
}

static unsigned WINAPI _statusfp(void)
{
    int flags = fetestexcept(FE_ALL_EXCEPT);
    return (flags & FE_INVALID ? 0x10 : 0) | (flags & FE_DIVBYZERO ? 0x08 : 0) |
           (flags & FE_OVERFLOW ? 0x04 : 0) | (flags & FE_UNDERFLOW ? 0x02 : 0) |
           (flags & FE_INEXACT ? 0x01 : 0);
}

static void WINAPI _fpreset(void)
{
    feclearexcept(FE_ALL_EXCEPT);
    fesetround(FE_TONEAREST);
}

static int WINAPI fegetround_(void) { return current_control() & MCW_RC; }

static int WINAPI fesetround_(int r)
{
    if (r & ~(int)MCW_RC)
        return 1;
    _controlfp(r, MCW_RC);
    return 0;
}

BIT_TABLE(crt_math,
          EXP(sin), EXP(cos), EXP(tan), EXP(asin), EXP(acos), EXP(atan), EXP(atan2),
          EXP(sinh), EXP(cosh), EXP(tanh), EXP(asinh), EXP(acosh), EXP(atanh),
          EXP(exp), EXP(exp2), EXP(expm1), EXP(log), EXP(log10), EXP(log2), EXP(log1p), EXP(logb),
          BIT_AS("_logb", logb_),
          EXP(pow), EXP(sqrt), EXP(cbrt), EXP(hypot), BIT_AS("_hypot", hypot_),
          EXP(ceil), EXP(floor), EXP(fabs), EXP(fmod), EXP(round), EXP(trunc), EXP(rint), EXP(nearbyint),
          EXP(fmin), EXP(fmax), EXP(fdim), EXP(copysign), BIT_AS("_copysign", copysign_),
          EXP(remainder), EXP(nextafter), BIT_AS("_nextafter", nextafter_),
          EXP(erf), EXP(erfc), EXP(tgamma), EXP(lgamma),
          EXP(sinf), EXP(cosf), EXP(tanf), EXP(asinf), EXP(acosf), EXP(atanf), EXP(atan2f),
          EXP(sinhf), EXP(coshf), EXP(tanhf),
          EXP(expf), EXP(exp2f), EXP(logf), EXP(log10f), EXP(log2f),
          EXP(powf), EXP(sqrtf), EXP(cbrtf), EXP(hypotf), BIT_AS("_hypotf", hypotf_),
          EXP(ceilf), EXP(floorf), EXP(fabsf), EXP(fmodf), EXP(roundf), EXP(truncf), EXP(rintf),
          EXP(fminf), EXP(fmaxf), EXP(copysignf), BIT_AS("_copysignf", copysignf_),
          EXP(ldexp), EXP(ldexpf), EXP(frexp), EXP(frexpf), EXP(modf), EXP(modff),
          EXP(scalbn), EXP(fma), EXP(fmaf), EXP(nan), EXP(nanf), EXP(ilogb),
          EXP(lround), EXP(lroundf), EXP(llround), EXP(llroundf), EXP(lrint), EXP(lrintf), EXP(llrint),
          BIT_FN(_isnan), BIT_FN(_isnanf), BIT_FN(_finite), BIT_FN(_finitef), BIT_FN(_chgsign),
          BIT_FN(_scalb), BIT_FN(_dclass), BIT_FN(_fdclass), BIT_FN(_dsign), BIT_FN(_fdsign),
          BIT_FN(_fpclass),
          BIT_FN(_controlfp), BIT_AS("_control87", _controlfp), BIT_FN(_controlfp_s),
          BIT_FN(_clearfp), BIT_FN(_statusfp), BIT_FN(_fpreset),
          EXP(fegetround), EXP(fesetround));
