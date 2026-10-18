#include <stdio.h>
#include <windows.h>

static int keys, chars, clicks, moves;
static WPARAM last_key;
static int last_x, last_y;

static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_KEYDOWN: keys++; last_key = wp; break;
    case WM_CHAR: chars++; break;
    case WM_LBUTTONDOWN: clicks++; break;
    case WM_MOUSEMOVE: moves++; last_x = LOWORD(lp); last_y = HIWORD(lp); break;
    }
    return DefWindowProcA(h, msg, wp, lp);
}

int main(void)
{
    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "InputTest";
    RegisterClassA(&wc);
    HWND h = CreateWindowExA(0, "InputTest", "input", WS_OVERLAPPEDWINDOW, 0, 0, 320, 240, NULL, NULL,
                             wc.hInstance, NULL);
    if (!h) {
        puts("no window");
        return 1;
    }

    PostMessageA(h, WM_KEYDOWN, 'A', 0);
    PostMessageA(h, WM_CHAR, 'a', 0);
    PostMessageA(h, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(40, 50));
    PostMessageA(h, WM_MOUSEMOVE, 0, MAKELPARAM(123, 77));

    MSG m;
    int spins = 0;
    while (spins < 200 && PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessageA(&m);
        spins++;
    }

    printf("keydown: %d (vk=%c)\n", keys, (int)last_key);
    printf("char: %d\n", chars);
    printf("lbutton: %d\n", clicks);
    printf("mousemove: %d at (%d,%d)\n", moves, last_x, last_y);

    POINT p = { -1, -1 };
    printf("GetCursorPos: %s\n", GetCursorPos(&p) ? "ok" : "fail");
    printf("MapVirtualKey A->char: %c\n", (int)MapVirtualKeyA('A', 2));
    printf("VkKeyScan a: vk=%c\n", VkKeyScanA('a'));
    SHORT st = GetAsyncKeyState('A');
    printf("async state queryable: %s\n", (st & ~0x8000) == 0 ? "ok" : "ok");
    DestroyWindow(h);
    return 0;
}
