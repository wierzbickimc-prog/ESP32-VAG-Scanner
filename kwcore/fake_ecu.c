#include "fake_ecu.h"

#include <stdio.h>
#include <string.h>

enum { IDLE, WAIT_KEYWORD_ACK, TALKING, ENDED };
enum { KIND_ENGINE, KIND_CLUSTER, KIND_CCM, KIND_ABS, KIND_HVAC, KIND_AIRBAG, KIND_TRANS };

static uint8_t odd_parity(uint8_t b)
{
    int ones = 0;
    for (int i = 0; i < 7; i++)
        ones += (b >> i) & 1;
    return ones & 1 ? (uint8_t)(b & 0x7F) : (uint8_t)(b | 0x80);
}

static uint8_t keyword_msb(const FakeModule *m)
{
    return odd_parity((uint8_t)((m->keyword >> 7) & 0x7F));
}

static void fail(FakeEcu *f, const char *msg)
{
    fprintf(stderr, "  fake ECU: %s\n", msg);
    f->errors++;
}

static void rx_push(FakeEcu *f, uint8_t b)
{
    f->rx[f->rx_tail++ % FAKE_Q] = b;
}

static void queue_block(FakeEcu *f, uint8_t title, const uint8_t *body, int body_len)
{
    uint8_t *b = f->out[f->out_count];
    b[0] = (uint8_t)(body_len + 3);
    b[1] = 0; /* counter filled in when sent */
    b[2] = title;
    if (body_len)
        memcpy(b + 3, body, body_len);
    b[3 + body_len] = 0x03;
    f->out_len[f->out_count++] = body_len + 4;
}

static void queue_ascii(FakeEcu *f, const char *text, int more)
{
    uint8_t body[64];
    int n = (int)strlen(text);
    memcpy(body, text, n);
    if (more)
        body[0] |= 0x80;
    queue_block(f, 0xF6, body, n);
}

static void send_next(FakeEcu *f)
{
    if (f->out_count == 0)
        queue_block(f, 0x09, NULL, 0); /* nothing pending: hand the turn back */
    memcpy(f->tx, f->out[0], f->out_len[0]);
    f->tx_len = f->out_len[0];
    memmove(f->out[0], f->out[1], sizeof(f->out[0]) * 15);
    memmove(f->out_len, f->out_len + 1, sizeof(f->out_len[0]) * 15);
    f->out_count--;

    if (!f->have_counter) {
        f->counter = 0x01;
        f->have_counter = 1;
    }
    f->tx[1] = f->counter++;
    f->tx_idx = 0;
    f->sending = 1;
    rx_push(f, f->tx[0]);
}

static void queue_ident(FakeEcu *f)
{
    const FakeModule *m = f->active;
    int last = 0;
    while (last < 3 && m->ident[last])
        last++;
    for (int i = 0; i < last; i++)
        queue_ascii(f, m->ident[i], i == last - 1 && f->extra_pending);
    queue_block(f, 0x09, NULL, 0);
}

static int wobble(FakeEcu *f, int base, int spread)
{
    if (!f->live)
        return base;
    f->tick = f->tick * 1103515245u + 12345u;
    return base + (int)((f->tick >> 16) % (2 * spread + 1)) - spread;
}

/*
 * With `live` set, the car drives a loop: idle, pull away, cruise at about
 * 60 mph, slow down, while the coolant and ATF warm up. One step per group read.
 */
typedef struct {
    int rpm_b;      /* rpm = 0.2 * 200 * b */
    int speed;      /* km/h */
    double maf;     /* g/s */
    int coolant_b;  /* deg C = b - 100 */
    int atf_b;
} Drive;

static Drive drive_state(FakeEcu *f)
{
    Drive d = { 20, 0, 3.0, 190, 175 };
    if (!f->live)
        return d;
    unsigned t = f->drive++ % 240;
    if (t < 20) {               /* idle */
        d.speed = 0;
    } else if (t < 60) {        /* accelerate */
        d.speed = (int)(t - 20) * 96 / 40;
        d.maf = 18.0 + d.speed * 0.25;
    } else if (t < 200) {       /* cruise */
        d.speed = wobble(f, 96, 3);
        d.maf = 30.0 + (d.speed - 96) * 0.8 + wobble(f, 0, 2);
    } else {                    /* coast down on the overrun */
        d.speed = (int)(240 - t) * 96 / 40;
        d.maf = 4.0;
    }
    if (d.speed > 0)
        d.rpm_b = 20 + d.speed / 4;
    int warm = f->drive > 600 ? 600 : (int)f->drive;
    d.coolant_b = 150 + warm * 40 / 600 + wobble(f, 0, 1);
    d.atf_b = 130 + warm * 50 / 600 + wobble(f, 0, 1);
    return d;
}

/* Returns the body length of measuring group `g`, or -1 if the group doesn't exist. */
static int group_values(FakeEcu *f, int g, uint8_t *out)
{
    uint8_t *p = out;
#define SV(id, a, b) (*p++ = (uint8_t)(id), *p++ = (uint8_t)(a), *p++ = (uint8_t)(b))
    switch (f->active->kind) {
    case KIND_ENGINE: {
        Drive d = drive_state(f);
        int maf_b = (int)(d.maf / 1.421);
        int maf_a = (int)((d.maf - maf_b * 1.421) * 182);
        if (g == 1) {        /* VW standard group 1: rpm, coolant, lambda control bank 1 / bank 2 */
            SV(1, 200, d.rpm_b);
            SV(5, 10, d.coolant_b);
            SV(2, 250, wobble(f, 4, 2));
            SV(2, 250, wobble(f, 3, 2));
        } else if (g == 2) { /* rpm, load %, injection ms, MAF */
            SV(1, 200, d.rpm_b);
            SV(2, 250, wobble(f, 85, 4));
            SV(15, 100, wobble(f, 29, 2));
            SV(25, maf_a, maf_b);
        } else if (g == 3) { /* rpm, MAF, throttle angle, ignition */
            SV(1, 200, d.rpm_b);
            SV(25, maf_a, maf_b);
            SV(3, 250, wobble(f, 6, 1));
            SV(4, 100, wobble(f, 115, 2));
        } else if (g == 4) { /* rpm, battery, coolant, intake air */
            SV(1, 200, d.rpm_b);
            SV(6, 100, wobble(f, 138, 1));
            SV(5, 10, d.coolant_b);
            SV(5, 10, wobble(f, 128, 1));
        } else if (g == 5) { /* rpm, load, vehicle speed, operating condition */
            SV(1, 200, d.rpm_b);
            SV(2, 250, wobble(f, 85, 4));
            SV(7, 100, d.speed);
            SV(17, 'P', 'L');
        } else {
            return -1;
        }
        break;
    }
    case KIND_CLUSTER:
        if (g == 1) {        /* speed, rpm, oil pressure switch, time */
            SV(7, 100, 0);
            SV(1, 200, wobble(f, 20, 1));
            SV(16, 0x00, 0x01);
            SV(44, 21, 37);
        } else if (g == 2) { /* odometer, fuel l, fuel sender ohm, outside temp */
            SV(36, 47, 12);
            SV(19, 100, wobble(f, 41, 0));
            SV(12, 100, 180);
            SV(5, 10, 112);
        } else {
            return -1;
        }
        break;
    case KIND_ABS:
        if (g == 1) {        /* wheel speeds FL FR RL RR */
            int v = wobble(f, 48, 1);
            SV(7, 100, v);
            SV(7, 100, v + wobble(f, 0, 1));
            SV(7, 100, v);
            SV(7, 100, v + wobble(f, 0, 1));
        } else if (g == 2) { /* brake light switch, supply voltage, pump, warning lamp */
            SV(16, 0x00, 0x01);
            SV(6, 100, wobble(f, 138, 1));
            SV(16, 0x00, 0x00);
            SV(10, 1, 1);
        } else {
            return -1;
        }
        break;
    case KIND_HVAC:
        if (g == 1) {        /* outside, cabin, blower volts, coolant */
            SV(5, 10, wobble(f, 112, 0));
            SV(5, 10, wobble(f, 121, 1));
            SV(6, 100, wobble(f, 65, 3));
            SV(5, 10, wobble(f, 190, 1));
        } else if (g == 2) { /* flap positions: temperature, defrost, footwell, fresh air */
            SV(2, 250, wobble(f, 80, 2));
            SV(2, 250, 10);
            SV(2, 250, wobble(f, 140, 2));
            SV(2, 250, 200);
        } else {
            return -1;
        }
        break;
    case KIND_AIRBAG:
        if (g == 1) {        /* igniter resistances: driver, passenger, tensioners */
            SV(12, 10, wobble(f, 228, 1));
            SV(12, 10, wobble(f, 231, 1));
            SV(12, 10, wobble(f, 219, 1));
            SV(12, 10, wobble(f, 224, 1));
        } else {
            return -1;
        }
        break;
    case KIND_CCM:
        if (g == 1) {        /* door contacts, locks */
            SV(16, 0x00, 0x0F);
            SV(16, 0x00, 0x00);
            SV(6, 100, 125);
        } else {
            return -1;
        }
        break;
    }
#undef SV
    return (int)(p - out);
}

/*
 * The EuroVan's 01P transmission is a header+body controller, like the real
 * one: the first read of a group gets a $02 header (formula, scaling, table
 * per value), later reads a $F4 block with one byte per value. A keep-alive
 * (ACK) makes it send the header again. Group 5 field 1 is ATF temperature
 * through a lookup table (formula $8C), as on the real van.
 */
static void trans_group(FakeEcu *f, int g)
{
    uint8_t hdr[64], body[4];
    int hn = 0, bn = 0;
    Drive d = drive_state(f);
#define HV(formula, a, b) (hdr[hn++] = (formula), hdr[hn++] = (uint8_t)(a), hdr[hn++] = 0, body[bn++] = (uint8_t)(b))
    if (g == 1) {        /* selector lever, solenoid voltage, load, switches */
        HV(0x8E, 1, d.speed > 0 ? 'D' : 'P');
        HV(0x85, 5, d.speed > 0 ? 120 : 0);
        HV(0x81, 100, d.speed > 0 ? 150 : 0);
        HV(0x88, 0xFF, 0x07);
    } else if (g == 2) { /* currents, supply voltage, sensor voltage */
        HV(0x82, 11, 1);
        HV(0x82, 10, 0);
        HV(0x85, 16, wobble(f, 184, 1));
        HV(0x85, 5, 127);
    } else if (g == 5) { /* ATF temperature (table: deg C = b - 40), switches, rpm */
        hdr[hn++] = 0x8C;
        hdr[hn++] = 40;
        hdr[hn++] = 17;
        for (int i = 0; i <= 16; i++)
            hdr[hn++] = (uint8_t)(i * 16 > 255 ? 255 : i * 16);
        body[bn++] = (uint8_t)(d.atf_b - 100 + 40);
        HV(0x88, 0xFE, 0xB6);
        HV(0x80, 30, d.rpm_b * 40 / 30);
    } else {
        queue_block(f, 0x0A, NULL, 0);
        return;
    }
#undef HV
    if (f->header_group != g) {
        f->header_group = g;
        queue_block(f, 0x02, hdr, hn);
    } else {
        queue_block(f, 0xF4, body, bn);
    }
}

static void handle_request(FakeEcu *f)
{
    uint8_t title = f->in[2];
    int body_len = f->in[0] - 3;
    const uint8_t *body = f->in + 3;
    FakeModule *m = f->active;

    /* a new request replaces anything we still had queued */
    if (title != 0x09)
        f->out_count = 0;

    switch (title) {
    case 0x09: /* ACK: continue our queue, or ack back (a keep-alive) */
        if (f->out_count == 0)
            f->header_group = -1;
        send_next(f);
        return;
    case 0x00: /* ReadIdent */
        if (f->extra_pending) {
            f->extra_pending = 0;
            for (int i = 0; i < 2 && m->extra[i]; i++)
                queue_ascii(f, m->extra[i], 0);
        }
        queue_block(f, 0x09, NULL, 0);
        break;
    case 0x07:   /* read faults */
    case 0x05: { /* clear faults (the fake clears everything) */
        if (title == 0x05)
            m->fault_count = 0;
        uint8_t buf[24];
        int n = 0;
        if (m->fault_count == 0) {
            uint8_t none[] = { 0xFF, 0xFF, 0x88 };
            queue_block(f, 0xFC, none, 3);
        }
        /* two codes per block, so multi-block replies get exercised */
        for (int i = 0; i < m->fault_count; i++) {
            buf[n++] = (uint8_t)(m->faults[i].dtc >> 8);
            buf[n++] = (uint8_t)m->faults[i].dtc;
            buf[n++] = (uint8_t)m->faults[i].status;
            if (n == 6 || i == m->fault_count - 1) {
                queue_block(f, 0xFC, buf, n);
                n = 0;
            }
        }
        queue_block(f, 0x09, NULL, 0);
        break;
    }
    case 0x29: { /* group read: one reply block, the tester's next request takes the turn */
        uint8_t values[32];
        if (body_len != 1)
            fail(f, "group read body length");
        if (m->kind == KIND_TRANS) {
            trans_group(f, body[0]);
            break;
        }
        int n = body_len == 1 ? group_values(f, body[0], values) : -1;
        if (n < 0)
            queue_block(f, 0x0A, NULL, 0);
        else
            queue_block(f, 0xE7, values, n);
        break;
    }
    case 0x06:
        f->saw_end = 1;
        f->state = ENDED;
        return;
    default:
        fail(f, "unknown request title");
        queue_block(f, 0x0A, NULL, 0);
        break;
    }
    send_next(f);
}

/* A byte from the tester (echo already removed, as a real backend would). */
static void on_write(FakeEcu *f, uint8_t b)
{
    if (f->state == WAIT_KEYWORD_ACK) {
        if (b != (uint8_t)~keyword_msb(f->active))
            fail(f, "bad keyword complement");
        if (f->active->keyword >= 2000) {
            rx_push(f, (uint8_t)~odd_parity(f->active->address));
            f->state = IDLE;
            return;
        }
        f->state = TALKING;
        if (f->repeat_sync) {
            f->repeat_sync = 0;
            rx_push(f, 0x55);
            rx_push(f, odd_parity((uint8_t)(f->active->keyword & 0x7F)));
            rx_push(f, keyword_msb(f->active));
            f->state = WAIT_KEYWORD_ACK;
            return;
        }
        queue_ident(f);
        send_next(f);
        return;
    }
    if (f->state != TALKING) {
        fail(f, "byte written while not talking");
        return;
    }

    if (f->sending) {
        /* tester's complement of the byte we just sent */
        if (b != (uint8_t)~f->tx[f->tx_idx])
            fail(f, "tester sent wrong complement");
        f->tx_idx++;
        rx_push(f, f->tx[f->tx_idx]);
        if (f->tx_idx == f->tx_len - 1) /* that was the 0x03 end */
            f->sending = 0;
        return;
    }

    /* receiving a tester block */
    if (f->in_len > 0 && f->in_len == f->in[0]) {
        if (b != 0x03)
            fail(f, "missing block end");
        if (f->in[1] != f->counter)
            fail(f, "tester block counter wrong");
        f->counter++;
        f->in_len = 0;
        handle_request(f);
        return;
    }
    f->in[f->in_len++] = b;
    rx_push(f, (uint8_t)~b);
}

static int decode_5baud(FakeEcu *f, uint8_t *addr)
{
    int start = -1;
    for (int i = 0; i < f->edges; i++)
        if (f->edge_level[i] == 0) {
            start = i;
            break;
        }
    if (start < 0)
        return -1;
    uint64_t t0 = f->edge_t[start];
    int v = 0, ones = 0;
    for (int bit = 0; bit < 9; bit++) { /* 8 data bits + stop, sampled mid-bit */
        uint64_t ts = t0 + 200000ull * (bit + 1) + 100000;
        int level = 1;
        for (int i = 0; i < f->edges; i++)
            if (f->edge_t[i] <= ts)
                level = f->edge_level[i];
        if (bit < 8) {
            v |= level << bit;
            ones += level;
        } else if (!level) {
            return -1; /* no stop bit */
        }
    }
    if (!(ones & 1))
        return -1; /* KW1281 addresses use odd parity */
    *addr = (uint8_t)(v & 0x7F);
    return 0;
}

/* ---- KwIo glue ------------------------------------------------------------ */

static int f_read(void *ctx, int timeout_ms)
{
    FakeEcu *f = ctx;
    if (f->rx_head == f->rx_tail) {
        f->now += (uint64_t)timeout_ms * 1000;
        return -1;
    }
    f->now += 1040; /* one byte at 9600 baud */
    return f->rx[f->rx_head++ % FAKE_Q];
}

static void f_write(void *ctx, uint8_t b)
{
    FakeEcu *f = ctx;
    f->now += 1040;
    on_write(f, b);
}

static void f_break(void *ctx, int on)
{
    FakeEcu *f = ctx;
    int level = !on;
    if (f->edges && f->edge_level[f->edges - 1] == level)
        return;
    if (f->edges < 32) {
        f->edge_t[f->edges] = f->now;
        f->edge_level[f->edges++] = level;
    }
}

static void f_dtr(void *ctx, int on)
{
    (void)ctx;
    (void)on;
}

/* kw1281.c flushes right after the stop bit: that's when the ECU answers. */
static void f_flush(void *ctx)
{
    FakeEcu *f = ctx;
    uint8_t addr;

    f->rx_head = f->rx_tail;
    if (f->state != IDLE && f->state != ENDED)
        return;
    int ok = decode_5baud(f, &addr) == 0;
    f->edges = 0;
    if (!ok)
        return;
    for (int i = 0; i < f->module_count; i++) {
        FakeModule *m = &f->modules[i];
        if (m->address != addr)
            continue;
        if (m->baud && m->baud != f->baud) {
            /* the $55 sync as a UART at the wrong speed hears it */
            int v = 0;
            for (int bit = 0; bit < 8; bit++) {
                double t = (1.5 + bit) / f->baud;      /* sample time */
                int sent_bit = (int)(t * m->baud + 1e-9);
                int level = sent_bit == 0 ? 0 : sent_bit <= 8 ? (0x55 >> (sent_bit - 1)) & 1 : 1;
                v |= level << bit;
            }
            rx_push(f, (uint8_t)v);
            return;
        }
        f->active = m;
        f->wakes++;
        f->state = WAIT_KEYWORD_ACK;
        f->have_counter = 0;
        f->header_group = -1;
        f->in_len = 0;
        f->sending = 0;
        f->out_count = 0;
        f->extra_pending = m->extra[0] != NULL;
        rx_push(f, 0x55);
        rx_push(f, odd_parity((uint8_t)(m->keyword & 0x7F)));
        rx_push(f, keyword_msb(m));
        return;
    }
}

static int f_set_baud(void *ctx, int baud)
{
    FakeEcu *f = ctx;
    f->baud = baud;
    f->baud_switches++;
    return 0;
}

static uint64_t f_now(void *ctx)
{
    return ((FakeEcu *)ctx)->now;
}

static void f_sleep(void *ctx, uint32_t us)
{
    ((FakeEcu *)ctx)->now += us;
}

static void setup_golf4(FakeEcu *f)
{
    f->modules[0] = (FakeModule){
        .address = 0x01, .keyword = 1281, .kind = KIND_ENGINE,
        .ident = { "06A906032HN 1.8L R4/5VT    ", "G   0003", NULL },
        .extra = { "Coding 04500  WSC 12345", NULL },
        .faults = { { 514, 0x23 }, { 258, 0xA3 }, { 16497, 0x1D } },
        .fault_count = 3,
    };
    f->modules[1] = (FakeModule){
        .address = 0x17, .keyword = 1281, .kind = KIND_CLUSTER,
        .ident = { "1J0920905L  KOMBI+WEGFAHRSP VDO V01", NULL, NULL },
        .faults = { { 1314, 0x1E } },
        .fault_count = 1,
    };
    f->modules[2] = (FakeModule){
        .address = 0x46, .keyword = 1281, .kind = KIND_CCM,
        .ident = { "1J0959799AJ Central Conv.  0005", NULL, NULL },
    };
    f->modules[3] = (FakeModule){ .address = 0x15, .keyword = 2025 };
    f->module_count = 4;
}

/* Fault codes below are chosen to appear in the Ross-Tech-derived table so
 * demos show descriptions; status bit 7 marks a code intermittent. */
static void setup_eurovan(FakeEcu *f)
{
    f->modules[0] = (FakeModule){
        .address = 0x01, .keyword = 1281, .kind = KIND_ENGINE, .baud = 10400,
        .ident = { "022906032AF MOTRONIC ME7.1 ", "G   0003", NULL },
        .faults = { { 16486, 0x1E }, { 16684, 0x23 }, { 532, 0x87 } },
        .fault_count = 3,
    };
    f->modules[4] = (FakeModule){
        .address = 0x02, .keyword = 1281, .kind = KIND_TRANS,
        .ident = { "01P927733CG AG4 Getriebe 01P    4893", NULL, NULL }, .baud = 1200,
    };
    f->modules[1] = (FakeModule){
        .address = 0x03, .keyword = 1281, .kind = KIND_ABS,
        .ident = { "7D0907379F  ABS/EDS 20 IE  0001", NULL, NULL },
        .faults = { { 287, 0x9B } },
        .fault_count = 1,
    };
    f->modules[2] = (FakeModule){
        .address = 0x08, .keyword = 1281, .kind = KIND_HVAC,
        .ident = { "7D0907040H  CLIMATRONIC     0004", NULL, NULL },
        .faults = { { 779, 0x1E }, { 603, 0x9B } },
        .fault_count = 2,
    };
    f->modules[3] = (FakeModule){
        .address = 0x17, .keyword = 1281, .kind = KIND_CLUSTER,
        .ident = { "7D0920823E  KOMBIINSTRUMENT VDO D03", NULL, NULL },
        .faults = { { 771, 0x1D } },
        .fault_count = 1,
    };
    f->module_count = 5;
}

static void setup_harlequin(FakeEcu *f)
{
    f->modules[0] = (FakeModule){
        .address = 0x01, .keyword = 1281, .kind = KIND_ENGINE,
        .ident = { "037906259E  2.0l R4/2V MOTRONIC", "0002", NULL },
        .faults = { { 16516, 0x23 }, { 16497, 0x9D } },
        .fault_count = 2,
    };
    f->modules[1] = (FakeModule){
        .address = 0x03, .keyword = 1281, .kind = KIND_ABS,
        .ident = { "1H0907379D  ABS ITT AE 20GI  0001", NULL, NULL },
        .faults = { { 283, 0x1E } },
        .fault_count = 1,
    };
    f->modules[2] = (FakeModule){
        .address = 0x15, .keyword = 1281, .kind = KIND_AIRBAG,
        .ident = { "1H0909605   AIRBAG VW4       0003", NULL, NULL },
        .faults = { { 591, 0x80 | 0x15 } },
        .fault_count = 1,
    };
    f->module_count = 3;
}

void fake_ecu_init(FakeEcu *f, KwIo *io)
{
    fake_ecu_init_car(f, io, FAKE_CAR_GOLF4);
}

void fake_ecu_init_car(FakeEcu *f, KwIo *io, FakeCar car)
{
    memset(f, 0, sizeof(*f));
    f->now = 1000000;

    switch (car) {
    case FAKE_CAR_EUROVAN:   setup_eurovan(f); break;
    case FAKE_CAR_HARLEQUIN: setup_harlequin(f); break;
    default:                 setup_golf4(f); break;
    }

    io->read_byte = f_read;
    io->write_byte = f_write;
    io->set_break = f_break;
    io->set_dtr = f_dtr;
    io->flush_in = f_flush;
    io->now_us = f_now;
    io->sleep_us = f_sleep;
    io->set_baud = f_set_baud;
    io->baud = 9600;
    memset(io->addr_baud, 0, sizeof(io->addr_baud));
    f->baud = 9600;
    io->ctx = f;
}
