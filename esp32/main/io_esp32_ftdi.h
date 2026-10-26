#pragma once

#include <stdbool.h>
#include "kw1281.h"

/* Installs the USB host stack and the CDC-ACM host driver. Call once at boot. */
void ftdi_usb_start(void);

/*
 * Waits up to `timeout_ms` for the FTDI KKL cable (0403:6001) and configures it
 * (latency 1 ms, 8N1, `baud`, DTR on). Returns true once the cable is ready.
 */
bool ftdi_open(int baud, int timeout_ms);

/* True while the cable is open and attached. */
bool ftdi_connected(void);

/* Closes the cable after a disconnect (or on request). */
void ftdi_close(void);

/* Fills `io` with the FTDI backend. Valid while ftdi_connected(). */
void ftdi_attach(KwIo *io);
