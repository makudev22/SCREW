#include <stdio.h>
#include <windows.h>

static CRITICAL_SECTION cs;
static volatile LONG counter;
static long shared;
static HANDLE ready;

static DWORD WINAPI worker(void *arg)
{
    WaitForSingleObject(ready, INFINITE);
    for (int i = 0; i < 100000; i++) {
        EnterCriticalSection(&cs);
        shared++;
        LeaveCriticalSection(&cs);
        InterlockedIncrement(&counter);
    }
    return (DWORD)(ULONG_PTR)arg;
}

int main(void)
{
    InitializeCriticalSection(&cs);
    ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    HANDLE t[4];
    for (int i = 0; i < 4; i++)
        t[i] = CreateThread(NULL, 0, worker, (void *)(ULONG_PTR)(i + 10), 0, NULL);
    SetEvent(ready);
    WaitForMultipleObjects(4, t, TRUE, INFINITE);
    DWORD sum = 0;
    for (int i = 0; i < 4; i++) {
        DWORD code;
        GetExitCodeThread(t[i], &code);
        sum += code;
        CloseHandle(t[i]);
    }
    printf("shared=%ld counter=%ld exit_sum=%lu\n", shared, counter, sum);
    DeleteCriticalSection(&cs);
    printf("timeout: %s\n", WaitForSingleObject(CreateEventA(NULL, FALSE, FALSE, NULL), 50) == WAIT_TIMEOUT ? "yes" : "no");
    return 0;
}
