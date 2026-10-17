#ifndef SCREW_GDI32_H
#define SCREW_GDI32_H

#include <screw/bit.h>
#include <screw/gdi.h>
#include <screw/teb.h>
#include <screw/win.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef void *HDC;
typedef void *HGDIOBJ;
typedef void *HBITMAP;

enum gdi_kind { GDI_FREE, GDI_DC, GDI_BITMAP };

struct bitmap {
    int width;
    int height;
    int bpp;
    int stride;
    uint8_t *bits;
    bool owns_bits;
};

struct dc {
    HWND window;
    struct bitmap *selected;
    HBITMAP selected_handle;
    HBITMAP default_bitmap;
};

struct gdi_object {
    enum gdi_kind kind;
    union {
        struct dc dc;
        struct bitmap bitmap;
    };
};

HGDIOBJ gdi_new(enum gdi_kind kind, struct gdi_object **out);
struct gdi_object *gdi_get(HGDIOBJ h, enum gdi_kind kind);
void gdi_free(HGDIOBJ h);

HBITMAP bitmap_create(int width, int height, int bpp, const void *init);

extern const struct bit_table gdi_objects;
extern const struct bit_table gdi_blit;
extern const struct bit_table gdi_kmt;

#endif
