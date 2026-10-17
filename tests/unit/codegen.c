#include <screw/codegen.h>
#include <screw/win.h>

#include <stdint.h>
#include <stdio.h>

typedef uint64_t WINAPI (*ms6)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

static uint64_t seen[6];

static uint64_t WINAPI record(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f)
{
    seen[0] = a, seen[1] = b, seen[2] = c, seen[3] = d, seen[4] = e, seen[5] = f;
    return 99;
}

int main(void)
{
    int fails = 0;
    for (int index = 0; index < 6; index++) {
        ms6 fn = (ms6)code_null_arg(index, (void *)record);
        uint64_t r = fn(11, 22, 33, 44, 55, 66);
        for (int i = 0; i < 6; i++) {
            uint64_t want = i == index ? 0 : (uint64_t)(i + 1) * 11;
            fails += seen[i] != want;
        }
        fails += r != 99;
    }
    printf("codegen: null argument %s\n", fails ? "BAD" : "ok");
    return fails != 0;
}
