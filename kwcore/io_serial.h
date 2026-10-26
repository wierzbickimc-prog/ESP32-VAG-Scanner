#ifndef IO_SERIAL_H
#define IO_SERIAL_H

#include "kw1281.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opens a KKL cable on `port` (use /dev/cu.*, not /dev/tty.*). Returns 0 or -1. */
int kw_serial_open(KwIo *io, const char *port, int baud);

/* Bytes written whose echo didn't come back intact (a healthy cable stays at 0). */
int kw_serial_echo_errors(KwIo *io);

void kw_serial_close(KwIo *io);

#ifdef __cplusplus
}
#endif

#endif
