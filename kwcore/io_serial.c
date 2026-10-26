#define _DEFAULT_SOURCE
#define _DARWIN_C_SOURCE
/*
 * POSIX serial backend for KKL USB cables (macOS / Linux).
 * Set KW_TRACE=1 in the environment to log every byte to stderr.
 */
#include "io_serial.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#ifdef __APPLE__
#include <IOKit/serial/ioss.h>
#endif

#define ECHO_TIMEOUT_MS 500

typedef struct SerialCtx {
    int fd;
    int trace;
    int echo_errors;
} SerialCtx;

static uint64_t serial_now_us(void *ctx)
{
    (void)ctx;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

/* nanosleep for the bulk, spin for the last millisecond: the protocol needs ~2 ms gaps. */
static void serial_sleep_us(void *ctx, uint32_t us)
{
    uint64_t end = serial_now_us(ctx) + us;
    if (us > 1500) {
        struct timespec ts = { .tv_sec = (us - 1000) / 1000000, .tv_nsec = ((us - 1000) % 1000000) * 1000L };
        nanosleep(&ts, NULL);
    }
    while (serial_now_us(ctx) < end)
        ;
}

static int raw_read(SerialCtx *c, int timeout_ms)
{
    struct pollfd p = { .fd = c->fd, .events = POLLIN };
    for (;;) {
        int r = poll(&p, 1, timeout_ms);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            return -1;
        unsigned char b;
        ssize_t n = read(c->fd, &b, 1);
        if (n == 1)
            return b;
        if (n < 0 && (errno == EAGAIN || errno == EINTR))
            continue;
        return -1;
    }
}

static int serial_read(void *ctx, int timeout_ms)
{
    SerialCtx *c = ctx;
    int b = raw_read(c, timeout_ms);
    if (c->trace) {
        if (b < 0)
            fprintf(stderr, "RX timeout (%d ms)\n", timeout_ms);
        else
            fprintf(stderr, "RX %02X\n", b);
    }
    return b;
}

static void serial_write(void *ctx, uint8_t b)
{
    SerialCtx *c = ctx;
    if (write(c->fd, &b, 1) != 1)
        fprintf(stderr, "write failed: %s\n", strerror(errno));

    /* Half-duplex: the cable hears its own byte. Swallow it. */
    int echo = raw_read(c, ECHO_TIMEOUT_MS);
    if (echo != b)
        c->echo_errors++;
    if (c->trace) {
        if (echo == b)
            fprintf(stderr, "TX %02X\n", b);
        else if (echo < 0)
            fprintf(stderr, "TX %02X (no echo)\n", b);
        else
            fprintf(stderr, "TX %02X (echo %02X)\n", b, echo);
    }
}

static void serial_break(void *ctx, int on)
{
    SerialCtx *c = ctx;
    if (ioctl(c->fd, on ? TIOCSBRK : TIOCCBRK) != 0 && c->trace)
        fprintf(stderr, "break ioctl failed: %s\n", strerror(errno));
    if (c->trace)
        fprintf(stderr, "BREAK %s\n", on ? "on" : "off");
}

static void serial_dtr(void *ctx, int on)
{
    SerialCtx *c = ctx;
    int bits = TIOCM_DTR;
    ioctl(c->fd, on ? TIOCMBIS : TIOCMBIC, &bits);
}

static void serial_flush(void *ctx)
{
    SerialCtx *c = ctx;
    tcflush(c->fd, TCIFLUSH);
}

int kw_serial_open(KwIo *io, const char *port, int baud)
{
    SerialCtx *c = calloc(1, sizeof(*c));
    if (!c)
        return -1;

    int fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "Could not open %s: %s\n", port, strerror(errno));
        free(c);
        return -1;
    }

    struct termios t;
    if (tcgetattr(fd, &t) != 0)
        goto fail;
    cfmakeraw(&t);
    t.c_cflag &= ~(PARENB | PARODD | CSTOPB | CSIZE | CRTSCTS);
    t.c_cflag |= CS8 | CREAD | CLOCAL;
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;

#ifdef __APPLE__
    /* Any rate (10400 is not a standard termios speed) via IOSSIOSPEED after tcsetattr. */
    cfsetspeed(&t, B9600);
    if (tcsetattr(fd, TCSANOW, &t) != 0)
        goto fail;
    speed_t speed = (speed_t)baud;
    if (ioctl(fd, IOSSIOSPEED, &speed) != 0) {
        fprintf(stderr, "Could not set %d baud: %s\n", baud, strerror(errno));
        goto fail;
    }
#else
    speed_t speed = baud == 4800 ? B4800 : baud == 19200 ? B19200 : B9600;
    if (baud != 4800 && baud != 9600 && baud != 19200) {
        fprintf(stderr, "Baud %d not supported by this backend\n", baud);
        goto fail;
    }
    cfsetspeed(&t, speed);
    if (tcsetattr(fd, TCSANOW, &t) != 0)
        goto fail;
#endif

    tcflush(fd, TCIOFLUSH);

    c->fd = fd;
    c->trace = getenv("KW_TRACE") != NULL;
    io->read_byte = serial_read;
    io->write_byte = serial_write;
    io->set_break = serial_break;
    io->set_dtr = serial_dtr;
    io->flush_in = serial_flush;
    io->now_us = serial_now_us;
    io->sleep_us = serial_sleep_us;
    io->ctx = c;
    return 0;

fail:
    fprintf(stderr, "Could not configure %s: %s\n", port, strerror(errno));
    close(fd);
    free(c);
    return -1;
}

int kw_serial_echo_errors(KwIo *io)
{
    return ((SerialCtx *)io->ctx)->echo_errors;
}

void kw_serial_close(KwIo *io)
{
    SerialCtx *c = io->ctx;
    if (!c)
        return;
    close(c->fd);
    free(c);
    io->ctx = NULL;
}
