#ifndef SCREW_CRT_WRAP_H
#define SCREW_CRT_WRAP_H

#include <screw/win.h>

#define WRAP1(r, f, a) static r WINAPI f##_(a x) { return (r)f(x); }
#define WRAP2(r, f, a, b) static r WINAPI f##_(a x, b y) { return (r)f(x, y); }
#define WRAP3(r, f, a, b, c) static r WINAPI f##_(a x, b y, c z) { return (r)f(x, y, z); }
#define WRAP4(r, f, a, b, c, d) static r WINAPI f##_(a x, b y, c z, d w) { return (r)f(x, y, z, w); }

#define WRAPV1(f, a) static void WINAPI f##_(a x) { f(x); }
#define WRAPV2(f, a, b) static void WINAPI f##_(a x, b y) { f(x, y); }
#define WRAPV4(f, a, b, c, d) static void WINAPI f##_(a x, b y, c z, d w) { f(x, y, z, w); }

#define EXP(f) BIT_AS(#f, f##_)

#endif
