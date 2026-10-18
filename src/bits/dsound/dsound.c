#include <screw/bit.h>
#include <screw/win.h>

#define DS_OK 0
#define DSERR_NODRIVER 0x88780078u
#define CLASS_E_CLASSNOTAVAILABLE 0x80040111u

typedef long HRESULT;

static HRESULT WINAPI DirectSoundCreate(const void *guid, void **out, void *outer)
{
    if (out)
        *out = NULL;
    return (HRESULT)DSERR_NODRIVER;
}

static HRESULT WINAPI DirectSoundCreate8(const void *guid, void **out, void *outer)
{
    if (out)
        *out = NULL;
    return (HRESULT)DSERR_NODRIVER;
}

static HRESULT WINAPI DirectSoundEnumerateA(void *cb, void *ctx) { return DS_OK; }
static HRESULT WINAPI DirectSoundEnumerateW(void *cb, void *ctx) { return DS_OK; }

static HRESULT WINAPI DirectSoundCaptureCreate(const void *guid, void **out, void *outer)
{
    if (out)
        *out = NULL;
    return (HRESULT)DSERR_NODRIVER;
}

static HRESULT WINAPI DirectSoundCaptureCreate8(const void *guid, void **out, void *outer)
{
    if (out)
        *out = NULL;
    return (HRESULT)DSERR_NODRIVER;
}

static HRESULT WINAPI DirectSoundCaptureEnumerateA(void *cb, void *ctx) { return DS_OK; }
static HRESULT WINAPI DirectSoundCaptureEnumerateW(void *cb, void *ctx) { return DS_OK; }
static HRESULT WINAPI GetDeviceID(const void *src, void *dst) { return (HRESULT)DSERR_NODRIVER; }
static HRESULT WINAPI DllGetClassObject(const void *clsid, const void *iid, void **out)
{
    if (out)
        *out = NULL;
    return (HRESULT)CLASS_E_CLASSNOTAVAILABLE;
}
static HRESULT WINAPI DllCanUnloadNow(void) { return DS_OK; }

BIT_TABLE(dsound_exports,
          BIT_FN(DirectSoundCreate), BIT_FN(DirectSoundCreate8),
          BIT_FN(DirectSoundEnumerateA), BIT_FN(DirectSoundEnumerateW),
          BIT_FN(DirectSoundCaptureCreate), BIT_FN(DirectSoundCaptureCreate8),
          BIT_FN(DirectSoundCaptureEnumerateA), BIT_FN(DirectSoundCaptureEnumerateW),
          BIT_FN(GetDeviceID), BIT_FN(DllGetClassObject), BIT_FN(DllCanUnloadNow));

BIT_DEFINE(bit_dsound, "dsound.dll", &dsound_exports);
