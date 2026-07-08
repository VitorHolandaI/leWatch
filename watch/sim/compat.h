/* PC-only compatibility shims (force-included by the sim Makefile). */
#pragma once
#ifndef ARDUINO
#include <stdio.h>
#include <stdlib.h>

/* Arduino provides itoa(); glibc does not. Minimal base-10/16 implementation. */
static inline char *itoa(int value, char *str, int base)
{
    if (base == 10) {
        sprintf(str, "%d", value);
    } else if (base == 16) {
        sprintf(str, "%x", value);
    } else {
        sprintf(str, "%d", value);
    }
    return str;
}
#endif
