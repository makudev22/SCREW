#include "gdi.h"

#include <screw/log.h>

#define STATUS_SUCCESS 0
#define STATUS_NOT_IMPLEMENTED ((NTSTATUS)0xC0000002u)
#define STATUS_NOT_SUPPORTED ((NTSTATUS)0xC00000BBu)

static NTSTATUS unsupported(const char *name)
{
    trace("gdi32: %s is a kernel display call Screw does not provide; DXVK falls back to its own path", name);
    return STATUS_NOT_SUPPORTED;
}

#define KMT_STUB(fn)                                 \
    static NTSTATUS WINAPI fn(void *arg)             \
    {                                                \
        return unsupported(#fn);                     \
    }

KMT_STUB(D3DKMTCreateDevice)
KMT_STUB(D3DKMTDestroyDevice)
KMT_STUB(D3DKMTEscape)
KMT_STUB(D3DKMTCloseAdapter)
KMT_STUB(D3DKMTOpenAdapterFromLuid)
KMT_STUB(D3DKMTOpenAdapterFromGdiDisplayName)
KMT_STUB(D3DKMTQueryAdapterInfo)
KMT_STUB(D3DKMTCreateAllocation)
KMT_STUB(D3DKMTDestroyAllocation)
KMT_STUB(D3DKMTOpenResource)
KMT_STUB(D3DKMTOpenResource2)
KMT_STUB(D3DKMTOpenResourceFromNtHandle)
KMT_STUB(D3DKMTQueryResourceInfo)
KMT_STUB(D3DKMTQueryResourceInfoFromNtHandle)
KMT_STUB(D3DKMTShareObjects)
KMT_STUB(D3DKMTCreateKeyedMutex)
KMT_STUB(D3DKMTCreateKeyedMutex2)
KMT_STUB(D3DKMTOpenKeyedMutex)
KMT_STUB(D3DKMTOpenKeyedMutex2)
KMT_STUB(D3DKMTDestroyKeyedMutex)
KMT_STUB(D3DKMTCreateSynchronizationObject)
KMT_STUB(D3DKMTOpenSynchronizationObject)
KMT_STUB(D3DKMTOpenSyncObjectFromNtHandle)
KMT_STUB(D3DKMTOpenSyncObjectFromNtHandle2)
KMT_STUB(D3DKMTDestroySynchronizationObject)
KMT_STUB(D3DKMTCreateDCFromMemory)
KMT_STUB(D3DKMTDestroyDCFromMemory)
KMT_STUB(D3DKMTSetVidPnSourceOwner)
KMT_STUB(D3DKMTWaitForVerticalBlankEvent)

BIT_TABLE(gdi_kmt,
          BIT_FN(D3DKMTCreateDevice), BIT_FN(D3DKMTDestroyDevice), BIT_FN(D3DKMTEscape),
          BIT_FN(D3DKMTCloseAdapter), BIT_FN(D3DKMTOpenAdapterFromLuid),
          BIT_FN(D3DKMTOpenAdapterFromGdiDisplayName), BIT_FN(D3DKMTQueryAdapterInfo),
          BIT_FN(D3DKMTCreateAllocation), BIT_FN(D3DKMTDestroyAllocation),
          BIT_FN(D3DKMTOpenResource), BIT_FN(D3DKMTOpenResource2), BIT_FN(D3DKMTOpenResourceFromNtHandle),
          BIT_FN(D3DKMTQueryResourceInfo), BIT_FN(D3DKMTQueryResourceInfoFromNtHandle),
          BIT_FN(D3DKMTShareObjects),
          BIT_FN(D3DKMTCreateKeyedMutex), BIT_FN(D3DKMTCreateKeyedMutex2), BIT_FN(D3DKMTOpenKeyedMutex),
          BIT_FN(D3DKMTOpenKeyedMutex2), BIT_FN(D3DKMTDestroyKeyedMutex),
          BIT_FN(D3DKMTCreateSynchronizationObject), BIT_FN(D3DKMTOpenSynchronizationObject),
          BIT_FN(D3DKMTOpenSyncObjectFromNtHandle), BIT_FN(D3DKMTOpenSyncObjectFromNtHandle2),
          BIT_FN(D3DKMTDestroySynchronizationObject),
          BIT_FN(D3DKMTCreateDCFromMemory), BIT_FN(D3DKMTDestroyDCFromMemory),
          BIT_FN(D3DKMTSetVidPnSourceOwner), BIT_FN(D3DKMTWaitForVerticalBlankEvent));
