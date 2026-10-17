#include <stdio.h>
#include <windows.h>

#define MY_CODE 0xE0A1B2C3

static volatile DWORD seen;
static volatile int param;

static LONG WINAPI handler(EXCEPTION_POINTERS *info)
{
    seen = info->ExceptionRecord->ExceptionCode;
    param = (int)info->ExceptionRecord->ExceptionInformation[0];
    return EXCEPTION_CONTINUE_EXECUTION;
}

int main(void)
{
    SetUnhandledExceptionFilter(handler);
    volatile long long canary = 0x1122334455667788LL;
    ULONG_PTR args[1] = { 0xDEAD };

    RaiseException(MY_CODE, 0, 1, args);

    printf("resumed after RaiseException: yes\n");
    printf("filter saw code: %08X\n", (unsigned)seen);
    printf("filter saw param: %04X\n", (unsigned)param);
    printf("canary intact: %s\n", canary == 0x1122334455667788LL ? "yes" : "no");
    return 0;
}
