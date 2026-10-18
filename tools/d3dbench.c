#define COBJMACROS
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#define WIDTH 640
#define HEIGHT 360

static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(h, msg, wp, lp);
}

static double now_ms(void)
{
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return 1000.0 * (double)c.QuadPart / (double)f.QuadPart;
}

int main(int argc, char **argv)
{
    int frames = argc > 1 ? atoi(argv[1]) : 600;

    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "ScrewBench";
    RegisterClassA(&wc);
    HWND win = CreateWindowExA(0, "ScrewBench", "Screw D3D11 benchmark", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                               CW_USEDEFAULT, CW_USEDEFAULT, WIDTH, HEIGHT, NULL, NULL, wc.hInstance, NULL);
    if (!win) {
        puts("window failed");
        return 1;
    }

    DXGI_SWAP_CHAIN_DESC sd = { 0 };
    sd.BufferCount = 2;
    sd.BufferDesc.Width = WIDTH;
    sd.BufferDesc.Height = HEIGHT;
    sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = win;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL want = D3D_FEATURE_LEVEL_11_0, got = 0;
    IDXGISwapChain *swap = NULL;
    ID3D11Device *dev = NULL;
    ID3D11DeviceContext *ctx = NULL;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, &want, 1, D3D11_SDK_VERSION,
                                               &sd, &swap, &dev, &got, &ctx);
    printf("device+swapchain: hr=0x%08lx level=0x%x\n", (unsigned long)hr, got);
    if (FAILED(hr))
        return 1;

    ID3D11Texture2D *back = NULL;
    IDXGISwapChain_GetBuffer(swap, 0, &IID_ID3D11Texture2D, (void **)&back);
    ID3D11RenderTargetView *rtv = NULL;
    ID3D11Device_CreateRenderTargetView(dev, (ID3D11Resource *)back, NULL, &rtv);
    printf("render target: %s\n", rtv ? "ok" : "null");

    double start = now_ms();
    int done = 0;
    MSG m;
    for (int i = 0; i < frames; i++) {
        while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageA(&m);
            if (m.message == WM_QUIT)
                goto finish;
        }
        float t = i / 60.0f;
        float color[4] = { 0.5f + 0.5f * (float)__builtin_sinf(t), 0.3f,
                           0.5f + 0.5f * (float)__builtin_cosf(t), 1.0f };
        ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv, color);
        hr = IDXGISwapChain_Present(swap, 0, 0);
        if (FAILED(hr)) {
            printf("present failed at frame %d: hr=0x%08lx\n", i, (unsigned long)hr);
            break;
        }
        done++;
    }
finish:;
    double elapsed = now_ms() - start;
    double fps = done > 0 && elapsed > 0 ? done * 1000.0 / elapsed : 0;
    printf("rendered %d frames in %.1f ms\n", done, elapsed);
    printf("average FPS: %.1f\n", fps);

    if (rtv)
        ID3D11RenderTargetView_Release(rtv);
    if (back)
        ID3D11Texture2D_Release(back);
    if (swap)
        IDXGISwapChain_Release(swap);
    if (ctx)
        ID3D11DeviceContext_Release(ctx);
    if (dev)
        ID3D11Device_Release(dev);
    return done > 0 ? 0 : 1;
}
