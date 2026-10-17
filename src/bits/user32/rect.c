#include "u32.h"

static BOOL WINAPI SetRect(RECT *r, int left, int top, int right, int bottom)
{
    if (!r)
        return FALSE;
    *r = (RECT){ left, top, right, bottom };
    return TRUE;
}

static BOOL WINAPI SetRectEmpty(RECT *r) { return SetRect(r, 0, 0, 0, 0); }

static BOOL WINAPI CopyRect(RECT *dst, const RECT *src)
{
    if (!dst || !src)
        return FALSE;
    *dst = *src;
    return TRUE;
}

static BOOL WINAPI IsRectEmpty(const RECT *r) { return !r || r->right <= r->left || r->bottom <= r->top; }

static BOOL WINAPI EqualRect(const RECT *a, const RECT *b)
{
    return a && b && a->left == b->left && a->top == b->top && a->right == b->right && a->bottom == b->bottom;
}

static BOOL WINAPI OffsetRect(RECT *r, int dx, int dy)
{
    if (!r)
        return FALSE;
    r->left += dx;
    r->right += dx;
    r->top += dy;
    r->bottom += dy;
    return TRUE;
}

static BOOL WINAPI InflateRect(RECT *r, int dx, int dy)
{
    if (!r)
        return FALSE;
    r->left -= dx;
    r->right += dx;
    r->top -= dy;
    r->bottom += dy;
    return TRUE;
}

static BOOL WINAPI PtInRect(const RECT *r, POINT p)
{
    return r && p.x >= r->left && p.x < r->right && p.y >= r->top && p.y < r->bottom;
}

static BOOL WINAPI IntersectRect(RECT *dst, const RECT *a, const RECT *b)
{
    if (!dst || !a || !b)
        return FALSE;
    RECT r = { a->left > b->left ? a->left : b->left, a->top > b->top ? a->top : b->top,
               a->right < b->right ? a->right : b->right, a->bottom < b->bottom ? a->bottom : b->bottom };
    if (IsRectEmpty(&r)) {
        SetRectEmpty(dst);
        return FALSE;
    }
    *dst = r;
    return TRUE;
}

static BOOL WINAPI UnionRect(RECT *dst, const RECT *a, const RECT *b)
{
    if (!dst || !a || !b)
        return FALSE;
    if (IsRectEmpty(a))
        return IsRectEmpty(b) ? (SetRectEmpty(dst), FALSE) : CopyRect(dst, b);
    if (IsRectEmpty(b))
        return CopyRect(dst, a);
    *dst = (RECT){ a->left < b->left ? a->left : b->left, a->top < b->top ? a->top : b->top,
                   a->right > b->right ? a->right : b->right, a->bottom > b->bottom ? a->bottom : b->bottom };
    return TRUE;
}

static BOOL WINAPI SubtractRect(RECT *dst, const RECT *a, const RECT *b)
{
    if (!dst || !a || !b)
        return FALSE;
    RECT r = *a, cut;
    if (IntersectRect(&cut, a, b)) {
        if (cut.top == r.top && cut.bottom == r.bottom) {
            if (cut.left == r.left)
                r.left = cut.right;
            else if (cut.right == r.right)
                r.right = cut.left;
        } else if (cut.left == r.left && cut.right == r.right) {
            if (cut.top == r.top)
                r.top = cut.bottom;
            else if (cut.bottom == r.bottom)
                r.bottom = cut.top;
        }
    }
    *dst = r;
    return !IsRectEmpty(&r);
}

BIT_TABLE(u32_rect,
          BIT_FN(SetRect), BIT_FN(SetRectEmpty), BIT_FN(CopyRect), BIT_FN(IsRectEmpty), BIT_FN(EqualRect),
          BIT_FN(OffsetRect), BIT_FN(InflateRect), BIT_FN(PtInRect), BIT_FN(IntersectRect), BIT_FN(UnionRect),
          BIT_FN(SubtractRect));
