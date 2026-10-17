#include <screw/codegen.h>
#include <screw/loader.h>
#include <screw/log.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static _Noreturn void trap_fire(const char *what)
{
    die("missing %s (not implemented yet; SCREW_LENIENT=1 makes it return 0)", what);
}

void *trap_make(const char *dll, const char *func)
{
    static const uint8_t zero_ret[] = { 0x31, 0xC0, 0xC3 };

    char *what = malloc(strlen(dll) + strlen(func) + 2);
    sprintf(what, "%s!%s", dll, func);
    if (log_lenient()) {
        warn("missing %s, returning 0", what);
        free(what);
        return code_emit(zero_ret, sizeof zero_ret);
    }
    trace("missing %s, trapping on call", what);
    return code_call1((uint64_t)what, (void *)trap_fire);
}
