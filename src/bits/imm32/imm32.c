#include <screw/bit.h>
#include <screw/win.h>

typedef void *HIMC;

static HIMC WINAPI ImmGetContext(HWND wnd) { return NULL; }
static BOOL WINAPI ImmReleaseContext(HWND wnd, HIMC ctx) { return TRUE; }
static HIMC WINAPI ImmAssociateContext(HWND wnd, HIMC ctx) { return NULL; }
static BOOL WINAPI ImmAssociateContextEx(HWND wnd, HIMC ctx, DWORD flags) { return TRUE; }
static BOOL WINAPI ImmSetCompositionWindow(HIMC ctx, void *form) { return TRUE; }
static BOOL WINAPI ImmSetCandidateWindow(HIMC ctx, void *cand) { return TRUE; }
static LONG WINAPI ImmGetCompositionStringW(HIMC ctx, DWORD index, void *buf, DWORD len) { return 0; }
static LONG WINAPI ImmGetCompositionStringA(HIMC ctx, DWORD index, void *buf, DWORD len) { return 0; }
static BOOL WINAPI ImmNotifyIME(HIMC ctx, DWORD action, DWORD index, DWORD value) { return TRUE; }
static BOOL WINAPI ImmSetOpenStatus(HIMC ctx, BOOL open) { return TRUE; }
static BOOL WINAPI ImmGetOpenStatus(HIMC ctx) { return FALSE; }
static HWND WINAPI ImmGetDefaultIMEWnd(HWND wnd) { return NULL; }
static BOOL WINAPI ImmDisableIME(DWORD thread) { return TRUE; }
static HIMC WINAPI ImmCreateContext(void) { return NULL; }
static BOOL WINAPI ImmDestroyContext(HIMC ctx) { return TRUE; }
static DWORD WINAPI ImmGetProperty(HANDLE kl, DWORD index) { return 0; }
static BOOL WINAPI ImmSetCompositionFontW(HIMC ctx, void *font) { return TRUE; }
static UINT WINAPI ImmGetVirtualKey(HWND wnd) { return 0xFF; }

BIT_TABLE(imm32_exports,
          BIT_FN(ImmGetContext), BIT_FN(ImmReleaseContext), BIT_FN(ImmAssociateContext),
          BIT_FN(ImmAssociateContextEx), BIT_FN(ImmSetCompositionWindow), BIT_FN(ImmSetCandidateWindow),
          BIT_FN(ImmGetCompositionStringW), BIT_FN(ImmGetCompositionStringA), BIT_FN(ImmNotifyIME),
          BIT_FN(ImmSetOpenStatus), BIT_FN(ImmGetOpenStatus), BIT_FN(ImmGetDefaultIMEWnd),
          BIT_FN(ImmDisableIME), BIT_FN(ImmCreateContext), BIT_FN(ImmDestroyContext),
          BIT_FN(ImmGetProperty), BIT_FN(ImmSetCompositionFontW), BIT_FN(ImmGetVirtualKey));

BIT_DEFINE(bit_imm32, "imm32.dll", &imm32_exports);
