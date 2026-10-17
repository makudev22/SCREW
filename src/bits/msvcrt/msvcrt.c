#include "crt.h"

BIT_DEFINE(bit_msvcrt, "msvcrt.dll",
           &crt_startup, &crt_stdio, &crt_format, &crt_scan, &crt_string, &crt_wide,
           &crt_stdlib, &crt_math, &crt_time, &crt_io, &crt_misc, &crt_wstream, &crt_jump, &crt_eh);
