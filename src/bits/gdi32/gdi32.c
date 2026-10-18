#include "gdi.h"

BIT_DEFINE(bit_gdi32, "gdi32.dll", &gdi_objects, &gdi_blit, &gdi_dib, &gdi_kmt);
