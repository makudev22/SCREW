#ifndef SCREW_ADV_H
#define SCREW_ADV_H

#include <screw/bit.h>
#include <screw/handle.h>
#include <screw/teb.h>
#include <screw/unicode.h>
#include <screw/win.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

extern const struct bit_table adv_registry;
extern const struct bit_table adv_security;
extern const struct bit_table adv_notify;

char *reg_key_dir(void *key);

#endif
