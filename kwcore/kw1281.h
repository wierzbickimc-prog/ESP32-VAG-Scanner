#ifndef KW1281_H
#define KW1281_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Abstract K-line I/O. The protocol code is hardware-independent; each
 * platform supplies a backend:
 *   io_serial.c           - POSIX termios (macOS/Linux, FTDI KKL cable)
 *   esp32/main/io_esp32_ftdi.c - ESP-IDF USB host + FTDI (ESP32-P4)
 *
 * Contract:
 *   read_byte   returns the next byte from the ECU, or -1 after timeout_ms.
 *   write_byte  sends one byte AND consumes its echo before returning
 *               (the K-line is half-duplex, a KKL cable hears itself).
 *   set_break   on=1 pulls TxD low, on=0 releases it to idle high.
 *   set_dtr     many KKL cables power their transceiver from DTR.
 *   flush_in    discards anything buffered on the receive side.
 *   now_us      monotonic clock in microseconds.
 *   sleep_us    sleeps at least `us` microseconds.
 */
typedef struct KwIo {
    int (*read_byte)(void *ctx, int timeout_ms);
    void (*write_byte)(void *ctx, uint8_t b);
    void (*set_break)(void *ctx, int on);
    void (*set_dtr)(void *ctx, int on);
    void (*flush_in)(void *ctx);
    uint64_t (*now_us)(void *ctx);
    void (*sleep_us)(void *ctx, uint32_t us);
    void *ctx;
} KwIo;

/* Error codes (all negative). */
#define KW_OK          0
#define KW_ENOWAKE    -1   /* controller did not answer the 5 baud wakeup */
#define KW_ETIMEOUT   -2   /* byte timeout mid-block */
#define KW_EPROTO     -3   /* malformed block, wrong complement, bad counter */
#define KW_ENAK       -4   /* controller answered NAK (unsupported / denied) */
#define KW_EUNEXPECTED -5  /* controller answered with an unexpected block */
#define KW_EKWP2000   -6   /* controller speaks KWP2000, not KW1281 */
#define KW_EOVERFLOW  -7   /* response larger than our buffers */

#define KW_IDENT_MAX 512
#define KW_MAX_BLOCKS 16

typedef struct KwBlock {
    uint8_t title;
    uint8_t body_len;
    uint8_t body[253];
} KwBlock;

typedef struct KwSession {
    KwIo *io;
    uint8_t address;
    int protocol;          /* keyword from the wakeup, e.g. 1281 */
    int sync_byte;         /* last byte seen where $55 was expected, -1 if none */
    uint8_t counter;       /* next expected/sent block counter */
    int have_counter;      /* 0 until the first block is received */
    int connected;
    char ident[KW_IDENT_MAX];
} KwSession;

typedef struct FaultCode {
    int dtc;
    int status;
} FaultCode;

typedef struct SensorValue {
    int sensor_id;
    int a;
    int b;
} SensorValue;

/* Pulse DTR to power-cycle the cable's transceiver. Call once after opening the port. */
void kw_reset_interface(KwIo *io);

/*
 * Wakes `address` (with up to 3 attempts), then reads the identification
 * blocks the controller sends unprompted into s->ident. Returns KW_OK or an error.
 */
int kw_connect(KwSession *s, KwIo *io, uint8_t address);

/* As kw_connect with a chosen number of wakeup attempts (1 is right for scanning). */
int kw_connect_attempts(KwSession *s, KwIo *io, uint8_t address, int attempts);

/* Sends the End Communication block. Safe to call when not connected. */
void kw_end(KwSession *s);

/* Reads fault codes. Returns count (>= 0) or an error. */
int kw_read_fault_codes(KwSession *s, FaultCode *codes, int max_codes);

/* Clears fault codes; fills `remaining` with any codes still present. Returns count or an error. */
int kw_clear_fault_codes(KwSession *s, FaultCode *remaining, int max_codes);

/* Reads measuring group `group`. Returns sensor count, KW_ENAK if the group doesn't exist, or an error. */
int kw_group_read(KwSession *s, int group, SensorValue *values, int max_values);

/* Keeps an idle session alive (send at least once a second). Returns KW_OK or an error. */
int kw_keep_alive(KwSession *s);

/* "01234 - 35-10", matching VCDS-Lite / kw1281test. */
void kw_format_fault(const FaultCode *fc, char *out, int out_len);

/* Decodes a raw sensor value into a human-readable string. */
void kw_format_sensor(const SensorValue *sv, char *out, int out_len);

const char *kw_strerror(int err);

/* Common controller names, or NULL. Addresses are hex, as VCDS writes them (0x17 = cluster). */
const char *kw_module_name(int address);

#ifdef __cplusplus
}
#endif

#endif
