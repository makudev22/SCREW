#include "cli.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define BINFMT_DIR "/proc/sys/fs/binfmt_misc"
#define BINFMT_CONF "/etc/binfmt.d/screw.conf"

static int write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "screw: cannot write %s: %s\n", path, strerror(errno));
        return -1;
    }
    fputs(text, f);
    return fclose(f) ? -1 : 0;
}

int cli_register(void)
{
    char self[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
    if (n <= 0) {
        fprintf(stderr, "screw: cannot find own path\n");
        return 1;
    }
    self[n] = 0;

    if (access(BINFMT_DIR "/register", W_OK)) {
        fprintf(stderr, "screw: run as root, and make sure binfmt_misc is mounted at " BINFMT_DIR "\n");
        return 1;
    }
    if (!access(BINFMT_DIR "/screw", F_OK))
        write_file(BINFMT_DIR "/screw", "-1");

    char rule[PATH_MAX + 64];
    snprintf(rule, sizeof rule, ":screw:M::MZ::%s:F\n", self);
    if (write_file(BINFMT_DIR "/register", rule))
        return 1;
    write_file(BINFMT_CONF, rule);
    printf("registered: .exe files now start with %s\n", self);
    return 0;
}

int cli_unregister(void)
{
    if (access(BINFMT_DIR "/screw", F_OK)) {
        printf("screw is not registered\n");
        return 0;
    }
    if (write_file(BINFMT_DIR "/screw", "-1"))
        return 1;
    unlink(BINFMT_CONF);
    printf("unregistered\n");
    return 0;
}
