/*
 * KW1281 protocol core. Behaviour mirrors the upstream C# tool in
 * ../kw1281test (KwpCommon.cs, KW1281Dialog.cs); see those when in doubt.
 */
#include "kw1281.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK_END 0x03

#define TITLE_READ_IDENT   0x00
#define TITLE_FAULT_DELETE 0x05
#define TITLE_END          0x06
#define TITLE_FAULT_READ   0x07
#define TITLE_ACK          0x09
#define TITLE_NAK          0x0A
#define TITLE_GROUP_READ   0x29
#define TITLE_GROUP_RESP   0xE7
#define TITLE_ASCII        0xF6
#define TITLE_FAULT_RESP   0xFC

#define SYNC_TIMEOUT_MS 2000
#define BYTE_TIMEOUT_MS 3000
#define R6_US 2000          /* gap before an ack / the next byte */

static void sleep_ms(KwIo *io, uint32_t ms)
{
    io->sleep_us(io->ctx, ms * 1000);
}

/* Sleeps until the absolute time `t` (microseconds on io->now_us). */
static void sleep_until(KwIo *io, uint64_t t)
{
    uint64_t now = io->now_us(io->ctx);
    if (t > now)
        io->sleep_us(io->ctx, (uint32_t)(t - now));
}

static uint8_t adjust_parity(uint8_t b, int even_parity)
{
    int parity = !even_parity;
    for (int i = 0; i < 7; i++)
        parity ^= (b >> i) & 1;
    return parity ? (uint8_t)(b | 0x80) : (uint8_t)(b & 0x7F);
}

/* Sends `b` at 5 baud by toggling break. Bit edges are scheduled on an absolute clock. */
static void bit_bang_5_baud(KwIo *io, uint8_t b, int even_parity)
{
    const uint64_t bit_us = 200000;

    b = adjust_parity(b, even_parity);

    /* The first break call can be slow; get it out of the timed section. */
    io->set_break(io->ctx, 0);

    uint64_t t = io->now_us(io->ctx);
    io->set_break(io->ctx, 1); /* start bit (low) */
    for (int i = 0; i < 8; i++) {
        t += bit_us;
        sleep_until(io, t);
        io->set_break(io->ctx, !((b >> i) & 1));
    }
    t += bit_us;
    sleep_until(io, t);
    io->set_break(io->ctx, 0); /* stop bit (high) */
    t += bit_us;
    sleep_until(io, t);
}

static int wakeup_once(KwSession *s, int even_parity)
{
    KwIo *io = s->io;

    sleep_ms(io, 300);
    bit_bang_5_baud(io, s->address, even_parity);
    io->flush_in(io->ctx);

    int sync = io->read_byte(io->ctx, SYNC_TIMEOUT_MS);
    s->sync_byte = sync;
    if (sync != 0x55)
        return KW_ENOWAKE;

    int lsb = io->read_byte(io->ctx, BYTE_TIMEOUT_MS);
    int msb = io->read_byte(io->ctx, BYTE_TIMEOUT_MS);
    if (lsb < 0 || msb < 0)
        return KW_ENOWAKE;

    s->protocol = ((msb & 0x7F) << 7) + (lsb & 0x7F);

    sleep_ms(io, 25);
    io->write_byte(io->ctx, (uint8_t)~msb);

    if (s->protocol >= 2000) {
        /* KWP2000 controllers answer with the complemented address. */
        io->read_byte(io->ctx, BYTE_TIMEOUT_MS);
        return KW_EKWP2000;
    }
    return KW_OK;
}

static int wakeup(KwSession *s, int attempts)
{
    int err = KW_ENOWAKE;
    for (int attempt = 0; attempt < attempts; attempt++) {
        if (attempt > 0)
            sleep_ms(s->io, 1000);
        err = wakeup_once(s, 0);
        if (err == KW_OK || err == KW_EKWP2000)
            return err;
    }
    return err;
}

/* Reads one byte, waits R6, then sends its complement. */
static int read_and_ack(KwSession *s, uint8_t *out)
{
    KwIo *io = s->io;
    int b = io->read_byte(io->ctx, BYTE_TIMEOUT_MS);
    if (b < 0)
        return KW_ETIMEOUT;
    io->sleep_us(io->ctx, R6_US);
    io->write_byte(io->ctx, (uint8_t)~b);
    *out = (uint8_t)b;
    return KW_OK;
}

/*
 * First byte of a received block. Some controllers repeat the $55 sync and
 * keyword; acknowledge it again and carry on (upstream issue #93).
 */
static int read_and_ack_first(KwSession *s, uint8_t *out)
{
    KwIo *io = s->io;
    for (int tries = 0; tries <= 5; tries++) {
        int b = io->read_byte(io->ctx, BYTE_TIMEOUT_MS);
        if (b < 0)
            return KW_ETIMEOUT;
        if (b == 0x55) {
            io->read_byte(io->ctx, BYTE_TIMEOUT_MS);
            int msb = io->read_byte(io->ctx, BYTE_TIMEOUT_MS);
            if (msb < 0)
                return KW_ETIMEOUT;
            sleep_ms(io, 25);
            io->write_byte(io->ctx, (uint8_t)~msb);
            continue;
        }
        io->sleep_us(io->ctx, R6_US);
        io->write_byte(io->ctx, (uint8_t)~b);
        *out = (uint8_t)b;
        return KW_OK;
    }
    return KW_EPROTO;
}

static int receive_block(KwSession *s, KwBlock *blk)
{
    uint8_t len, counter, title;
    int err;

    if ((err = read_and_ack_first(s, &len)) != KW_OK)
        return err;
    if (len < 3)
        return KW_EPROTO;

    if ((err = read_and_ack(s, &counter)) != KW_OK)
        return err;
    if (!s->have_counter) {
        s->counter = counter;
        s->have_counter = 1;
    } else if (counter != s->counter) {
        return KW_EPROTO;
    }
    s->counter++;

    if ((err = read_and_ack(s, &title)) != KW_OK)
        return err;

    blk->title = title;
    blk->body_len = (uint8_t)(len - 3);
    for (int i = 0; i < blk->body_len; i++) {
        if ((err = read_and_ack(s, &blk->body[i])) != KW_OK)
            return err;
    }

    int end = s->io->read_byte(s->io->ctx, BYTE_TIMEOUT_MS);
    if (end < 0)
        return KW_ETIMEOUT;
    if (end != BLOCK_END)
        return KW_EPROTO;
    return KW_OK;
}

static int send_block(KwSession *s, uint8_t title, const uint8_t *body, int body_len)
{
    KwIo *io = s->io;
    uint8_t bytes[3 + 253];
    int n = 0;

    if (body_len > 252)
        return KW_EOVERFLOW;

    bytes[n++] = (uint8_t)(body_len + 3); /* counter + title + body + end */
    bytes[n++] = s->counter++;
    bytes[n++] = title;
    for (int i = 0; i < body_len; i++)
        bytes[n++] = body[i];

    sleep_ms(io, 25);
    io->sleep_us(io->ctx, R6_US);
    for (int i = 0; i < n; i++) {
        io->write_byte(io->ctx, bytes[i]);
        int c = io->read_byte(io->ctx, BYTE_TIMEOUT_MS);
        if (c < 0)
            return KW_ETIMEOUT;
        if (c != (uint8_t)~bytes[i])
            return KW_EPROTO;
        io->sleep_us(io->ctx, R6_US);
    }
    io->write_byte(io->ctx, BLOCK_END); /* not acknowledged */
    return KW_OK;
}

/*
 * Receives blocks until the controller hands the turn back with ACK or NAK,
 * acknowledging each data block. Data blocks land in `blocks`; the count is
 * returned. *nak is set when the exchange ended with NAK.
 */
static int receive_blocks(KwSession *s, KwBlock *blocks, int max_blocks, int *nak)
{
    int count = 0;
    KwBlock blk;

    *nak = 0;
    for (;;) {
        int err = receive_block(s, &blk);
        if (err != KW_OK)
            return err;
        if (blk.title == TITLE_ACK)
            return count;
        if (blk.title == TITLE_NAK) {
            *nak = 1;
            return count;
        }
        if (count >= max_blocks)
            return KW_EOVERFLOW;
        blocks[count++] = blk;
        if ((err = send_block(s, TITLE_ACK, NULL, 0)) != KW_OK)
            return err;
    }
}

/* Sends a request block and collects the reply. */
static int request(KwSession *s, uint8_t title, const uint8_t *body, int body_len,
                   KwBlock *blocks, int max_blocks)
{
    int nak;
    int err = send_block(s, title, body, body_len);
    if (err != KW_OK)
        return err;
    int n = receive_blocks(s, blocks, max_blocks, &nak);
    if (n < 0)
        return n;
    if (nak && n == 0)
        return KW_ENAK;
    return n;
}

static void append_ident(KwSession *s, const KwBlock *blocks, int n, int *more)
{
    size_t pos = strlen(s->ident);

    *more = 0;
    for (int i = 0; i < n; i++) {
        const KwBlock *b = &blocks[i];
        if (b->title != TITLE_ASCII || b->body_len == 0)
            continue;
        if (b->body[0] > 0x7F)
            *more = 1;
        if (b->body[0] == 0x00)
            continue; /* coding / workshop code block, not text */
        for (int j = 0; j < b->body_len && pos < sizeof(s->ident) - 1; j++)
            s->ident[pos++] = (char)(b->body[j] & 0x7F);
    }
    s->ident[pos] = '\0';
}

void kw_reset_interface(KwIo *io)
{
    io->set_dtr(io->ctx, 0);
    sleep_ms(io, 300);
    io->set_dtr(io->ctx, 1);
    sleep_ms(io, 300);
}

int kw_connect(KwSession *s, KwIo *io, uint8_t address)
{
    return kw_connect_attempts(s, io, address, 3);
}

int kw_connect_attempts(KwSession *s, KwIo *io, uint8_t address, int attempts)
{
    KwBlock blocks[KW_MAX_BLOCKS];
    int nak, more;

    memset(s, 0, sizeof(*s));
    s->io = io;
    s->address = address;
    s->sync_byte = -1;

    int err = wakeup(s, attempts);
    if (err != KW_OK)
        return err;
    s->connected = 1;

    int n = receive_blocks(s, blocks, KW_MAX_BLOCKS, &nak);
    if (n < 0)
        goto fail;
    append_ident(s, blocks, n, &more);

    while (more) {
        size_t pos = strlen(s->ident);
        if (pos < sizeof(s->ident) - 1) {
            s->ident[pos++] = '\n';
            s->ident[pos] = '\0';
        }
        n = request(s, TITLE_READ_IDENT, NULL, 0, blocks, KW_MAX_BLOCKS);
        if (n < 0)
            goto fail;
        append_ident(s, blocks, n, &more);
    }
    return KW_OK;

fail:
    s->connected = 0;
    return n;
}

void kw_end(KwSession *s)
{
    if (!s->connected)
        return;
    send_block(s, TITLE_END, NULL, 0);
    s->connected = 0;
}

int kw_keep_alive(KwSession *s)
{
    KwBlock blocks[KW_MAX_BLOCKS];
    int n = request(s, TITLE_ACK, NULL, 0, blocks, KW_MAX_BLOCKS);
    return n < 0 ? n : KW_OK;
}

static int collect_faults(const KwBlock *blocks, int n, FaultCode *codes, int max_codes)
{
    int count = 0;
    for (int i = 0; i < n; i++) {
        const KwBlock *b = &blocks[i];
        if (b->title != TITLE_FAULT_RESP)
            return KW_EUNEXPECTED;
        for (int j = 0; j + 2 < b->body_len; j += 3) {
            int dtc = b->body[j] * 256 + b->body[j + 1];
            int status = b->body[j + 2];
            if (dtc == 0xFFFF && status == 0x88)
                continue; /* "no fault codes" marker */
            if (count >= max_codes)
                return KW_EOVERFLOW;
            codes[count].dtc = dtc;
            codes[count].status = status;
            count++;
        }
    }
    return count;
}

int kw_read_fault_codes(KwSession *s, FaultCode *codes, int max_codes)
{
    KwBlock blocks[KW_MAX_BLOCKS];
    int n = request(s, TITLE_FAULT_READ, NULL, 0, blocks, KW_MAX_BLOCKS);
    if (n < 0)
        return n;
    return collect_faults(blocks, n, codes, max_codes);
}

int kw_clear_fault_codes(KwSession *s, FaultCode *remaining, int max_codes)
{
    KwBlock blocks[KW_MAX_BLOCKS];
    int n = request(s, TITLE_FAULT_DELETE, NULL, 0, blocks, KW_MAX_BLOCKS);
    if (n < 0)
        return n;
    return collect_faults(blocks, n, remaining, max_codes);
}

int kw_group_read(KwSession *s, int group, SensorValue *values, int max_values)
{
    KwBlock blocks[KW_MAX_BLOCKS];
    uint8_t body[1] = { (uint8_t)group };
    int n = request(s, TITLE_GROUP_READ, body, 1, blocks, KW_MAX_BLOCKS);
    if (n < 0)
        return n;

    int count = 0;
    for (int i = 0; i < n; i++) {
        const KwBlock *b = &blocks[i];
        if (b->title != TITLE_GROUP_RESP)
            return KW_EUNEXPECTED;
        if (b->body_len % 3 != 0)
            return KW_EPROTO;
        for (int j = 0; j < b->body_len; j += 3) {
            if (count >= max_values)
                return KW_EOVERFLOW;
            values[count].sensor_id = b->body[j];
            values[count].a = b->body[j + 1];
            values[count].b = b->body[j + 2];
            count++;
        }
    }
    return count;
}

void kw_format_fault(const FaultCode *fc, char *out, int out_len)
{
    snprintf(out, out_len, "%05d - %02d-%02d", fc->dtc, fc->status & 0x7F, (fc->status >> 7) * 10);
}

static void bits8(int v, char *out)
{
    for (int i = 0; i < 8; i++)
        out[i] = (v >> (7 - i)) & 1 ? '1' : '0';
    out[8] = '\0';
}

/* Formulas ported from kw1281test/Blocks/SensorValue.cs. */
void kw_format_sensor(const SensorValue *sv, char *out, int out_len)
{
    const int A = sv->a, B = sv->b;
    const double a = A, b = B;
    char ba[9], bb[9];

    switch (sv->sensor_id) {
    case 1:  snprintf(out, out_len, "%g rpm", 0.2 * a * b); break;
    case 2:  snprintf(out, out_len, "%.1f %%", a * 0.002 * b); break;
    case 3:  snprintf(out, out_len, "%.1f Deg", 0.002 * a * b); break;
    case 4:  snprintf(out, out_len, "%.1f °%s", abs(B - 127) * 0.01 * a, B > 127 ? "ATDC" : "BTDC"); break;
    case 5:  snprintf(out, out_len, "%.1f °C", a * (b - 100) * 0.1); break;
    case 6:  snprintf(out, out_len, "%.1f V", 0.001 * a * b); break;
    case 7:  snprintf(out, out_len, "%g km/h", 0.01 * a * b); break;
    case 8:  snprintf(out, out_len, "%.1f", 0.1 * a * b); break;
    case 9:  snprintf(out, out_len, "%.1f Deg", (b - 127) * 0.02 * a); break;
    case 10: snprintf(out, out_len, "%s", B == 0 ? "Cold" : "Warm"); break;
    case 11: snprintf(out, out_len, "%.1f", 0.0001 * a * (b - 128) + 1); break;
    case 12: snprintf(out, out_len, "%.1f Ω", 0.001 * a * b); break;
    case 13: snprintf(out, out_len, "%.1f mm", (b - 127) * 0.001 * a); break;
    case 14: snprintf(out, out_len, "%.1f bar", 0.005 * a * b); break;
    case 15: snprintf(out, out_len, "%.1f ms", 0.01 * a * b); break;
    case 16: bits8(A, ba); bits8(B, bb); snprintf(out, out_len, "%s %s", ba, bb); break;
    case 17: snprintf(out, out_len, "\"%c%c\"", A, B); break;
    case 18: snprintf(out, out_len, "%.1f mbar", 0.04 * a * b); break;
    case 19: snprintf(out, out_len, "%.1f l", a * b * 0.01); break;
    case 20: snprintf(out, out_len, "%.1f %%", a * (b - 128) / 128.0); break;
    case 21: snprintf(out, out_len, "%.1f V", 0.001 * a * b); break;
    case 22: snprintf(out, out_len, "%.1f ms", 0.001 * a * b); break;
    case 23: snprintf(out, out_len, "%.1f %%", b / 256.0 * a); break;
    case 24: snprintf(out, out_len, "%.1f A", 0.001 * a * b); break;
    case 25: snprintf(out, out_len, "%.1f g/s", (b * 1.421) + (a / 182.0)); break;
    case 26: snprintf(out, out_len, "%d C", B - A); break;
    case 27: snprintf(out, out_len, "%.1f °%s", abs(B - 128) * 0.01 * a, B < 128 ? "ATDC" : "BTDC"); break;
    case 28: snprintf(out, out_len, "%d", B - A); break;
    case 29: snprintf(out, out_len, "%s", B < A ? "Map 1" : "Map 2"); break;
    case 30: snprintf(out, out_len, "%.1f Deg k/w", b / 12.0 * a); break;
    case 31: snprintf(out, out_len, "%.1f", b / 2560.0 * a); break;
    case 32: snprintf(out, out_len, "%d", B > 128 ? B - 256 : B); break;
    case 33: snprintf(out, out_len, "%.1f %%", A == 0 ? 100.0 * b : 100.0 * b / a); break;
    case 34: snprintf(out, out_len, "%.1f kW", (b - 128) * 0.01 * a); break;
    case 35: snprintf(out, out_len, "%.1f l/h", 0.01 * a * b); break;
    case 36: snprintf(out, out_len, "%d km", A * 2560 + B * 10); break;
    case 38: snprintf(out, out_len, "%.1f Deg k/w", (b - 128) * 0.001 * a); break;
    case 39: snprintf(out, out_len, "%.1f mg/h", b / 256.0 * a); break;
    case 40: snprintf(out, out_len, "%.1f A", b * 0.1 + (25.5 * a) - 400); break;
    case 41: snprintf(out, out_len, "%d Ah", B + A * 255); break;
    case 42: snprintf(out, out_len, "%.1f Kw", b * 0.1 + (25.5 * a) - 400); break;
    case 43: snprintf(out, out_len, "%.1f V", b * 0.1 + (25.5 * a)); break;
    case 44: snprintf(out, out_len, "%02d:%02d", A, B); break;
    case 45: snprintf(out, out_len, "%.1f", 0.1 * a * b / 100.0); break;
    case 46: snprintf(out, out_len, "%.1f Deg k/w", (a * b - 3200) * 0.0027); break;
    case 47: snprintf(out, out_len, "%d ms", (B - 128) * A); break;
    case 48: snprintf(out, out_len, "%d", B + A * 255); break;
    case 49: snprintf(out, out_len, "%.1f mg/h", b / 4.0 * a * 0.1); break;
    case 50: snprintf(out, out_len, "%.1f mbar", A == 0 ? (b - 128) / 0.01 : (b - 128) / (0.01 * a)); break;
    case 51: snprintf(out, out_len, "%.1f mg/h", ((b - 128) / 255.0) * a); break;
    case 52: snprintf(out, out_len, "%.1f Nm", b * 0.02 * a - a); break;
    case 53: snprintf(out, out_len, "%.1f g/s", (b - 128) * 1.4222 + 0.006 * a); break;
    case 54: snprintf(out, out_len, "%d", A * 256 + B); break;
    case 55: snprintf(out, out_len, "%.1f s", a * b / 200.0); break;
    case 56: snprintf(out, out_len, "%d WSC", A * 256 + B); break;
    case 57: snprintf(out, out_len, "%d WSC", A * 256 + B + 65536); break;
    case 58: snprintf(out, out_len, "%.1f /s", B > 128 ? 1.0225 * (256 - b) : 1.0225 * b); break;
    case 59: snprintf(out, out_len, "%.1f", (a * 256 + b) / 32768.0); break;
    case 60: snprintf(out, out_len, "%.1f sec", (a * 256 + b) * 0.01); break;
    case 61: snprintf(out, out_len, "%d", A == 0 ? B - 128 : (B - 128) / A); break;
    case 62: snprintf(out, out_len, "%.1f S", 0.256 * a * b); break;
    case 63: snprintf(out, out_len, "\"%c%c\"?", A, B); break;
    case 64: snprintf(out, out_len, "%d Ω", A + B); break;
    case 65: snprintf(out, out_len, "%.1f mm", 0.01 * a * (b - 127)); break;
    case 66: snprintf(out, out_len, "%.1f V", (a * b) / 511.12); break;
    case 67: snprintf(out, out_len, "%.1f Deg", (640 * a) + b * 2.5); break;
    case 68: snprintf(out, out_len, "%.1f deg/s", (256 * a + b) / 7.365); break;
    case 69: snprintf(out, out_len, "%.1f Bar", (256 * a + b) * 0.3254); break;
    case 70: snprintf(out, out_len, "%.1f m/s²", (256 * a + b) * 0.192); break;
    default: snprintf(out, out_len, "(%d %d %d)", sv->sensor_id, A, B); break;
    }
}

const char *kw_strerror(int err)
{
    switch (err) {
    case KW_OK:          return "ok";
    case KW_ENOWAKE:     return "controller did not wake up";
    case KW_ETIMEOUT:    return "timeout waiting for controller";
    case KW_EPROTO:      return "protocol error";
    case KW_ENAK:        return "controller refused (NAK)";
    case KW_EUNEXPECTED: return "unexpected response";
    case KW_EKWP2000:    return "controller uses KWP2000 (not supported)";
    case KW_EOVERFLOW:   return "response too large";
    default:             return "unknown error";
    }
}

const char *kw_module_name(int address)
{
    switch (address) {
    case 0x01: return "Engine";
    case 0x02: return "Auto Trans";
    case 0x03: return "ABS Brakes";
    case 0x08: return "Auto HVAC";
    case 0x09: return "Cent. Elect.";
    case 0x15: return "Airbags";
    case 0x16: return "Steering Wheel";
    case 0x17: return "Instruments";
    case 0x19: return "CAN Gateway";
    case 0x25: return "Immobilizer";
    case 0x35: return "Central Locks";
    case 0x36: return "Seat Mem Drvr";
    case 0x37: return "Navigation";
    case 0x45: return "Inter. Monitor";
    case 0x46: return "Central Conv.";
    case 0x55: return "Xenon Range";
    case 0x56: return "Radio";
    case 0x57: return "TV Tuner";
    default:   return NULL;
    }
}
