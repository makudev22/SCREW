#ifndef SCREW_CODEGEN_H
#define SCREW_CODEGEN_H

#include <stddef.h>
#include <stdint.h>

void *code_emit(const void *bytes, size_t n);
void *code_jump(uint64_t r11_value, void *target);
void *code_call1(uint64_t rdi_value, void *target);

#endif
