#ifndef SCREW_CLI_H
#define SCREW_CLI_H

#define SCREW_VERSION "0.1.0"

int cli_run(int argc, char **argv);
int cli_info(const char *path);
int cli_register(void);
int cli_unregister(void);

#endif
