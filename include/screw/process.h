#ifndef SCREW_PROCESS_H
#define SCREW_PROCESS_H

#include <screw/win.h>

void process_init(int argc, char **argv);

int process_argc(void);
char **process_argv(void);
WCHAR **process_wargv(void);
const char *process_cmdline(void);
const WCHAR *process_wcmdline(void);
const char *process_exe_host(void);
const char *process_exe_windows(void);

const char *env_get(const char *name);
void env_set(const char *name, const char *value);
char *env_block(void);
WCHAR *env_block_w(void);
char **env_vector(void);

_Noreturn void process_exit(UINT code);

#endif
