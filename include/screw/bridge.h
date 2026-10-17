#ifndef SCREW_BRIDGE_H
#define SCREW_BRIDGE_H

#define BRIDGE_MAX_ARGS 16

void *bridge_to_host(void *host_fn);
void *bridge_to_windows(void *win_fn);

#endif
