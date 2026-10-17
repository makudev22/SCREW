#include <setjmp.h>
#include <stdio.h>

static jmp_buf target;

static void deep(int depth)
{
    volatile int marker = 0xABCD + depth;
    if (depth > 0)
        deep(depth - 1);
    else
        longjmp(target, 42);
    printf("unreachable %d\n", marker);
}

int main(void)
{
    volatile long long saved = 0x1234567890ABCDEFLL;
    int r = setjmp(target);
    if (r == 0) {
        printf("armed, saved=%llx\n", saved);
        deep(5);
    }
    printf("returned %d, saved intact: %s\n", r, saved == 0x1234567890ABCDEFLL ? "yes" : "no");
    return 0;
}
