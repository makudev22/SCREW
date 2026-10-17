#include "cli.h"

#include <screw/log.h>

#include <stdio.h>
#include <string.h>

static int usage(FILE *out)
{
    fprintf(out,
            "Screw " SCREW_VERSION " - run Windows programs natively on Linux\n"
            "\n"
            "usage: screw PROGRAM.exe [ARGS...]\n"
            "       screw info PROGRAM.exe     show imports and their support\n"
            "       screw register             run .exe files directly (root)\n"
            "       screw unregister           undo register (root)\n"
            "\n"
            "environment: SCREW_TRACE=1  SCREW_LENIENT=1  SCREW_C=DIR  SCREW_DLL_PATH=DIRS\n");
    return out == stderr ? 2 : 0;
}

int main(int argc, char **argv)
{
    log_init();
    if (argc < 2)
        return usage(stderr);

    const char *cmd = argv[1];
    if (!strcmp(cmd, "-h") || !strcmp(cmd, "--help"))
        return usage(stdout);
    if (!strcmp(cmd, "-v") || !strcmp(cmd, "--version")) {
        puts("screw " SCREW_VERSION);
        return 0;
    }
    if (!strcmp(cmd, "info"))
        return argc == 3 ? cli_info(argv[2]) : usage(stderr);
    if (!strcmp(cmd, "register"))
        return cli_register();
    if (!strcmp(cmd, "unregister"))
        return cli_unregister();
    if (!strcmp(cmd, "--"))
        return argc > 2 ? cli_run(argc - 2, argv + 2) : usage(stderr);
    return cli_run(argc - 1, argv + 1);
}
