#ifndef FAKE_ECU_H
#define FAKE_ECU_H

/*
 * A simulated car behind the KwIo interface, on a virtual clock. It decodes
 * the 5 baud address from break timing and plays the controller side of
 * KW1281: complements every byte, checks counters and the tester's
 * complements, and answers ReadIdent / faults / clear / group / ACK / End.
 * Used by test_kw1281.c and by the Mac app's demo mode.
 */

#include "kw1281.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FAKE_Q 4096
#define FAKE_MAX_MODULES 8

typedef struct FakeModule {
    uint8_t address;
    int keyword;              /* 1281, or >= 2000 for a KWP2000 controller */
    const char *ident[3];     /* ident text, one per block */
    const char *extra[2];     /* offered via "more data" + ReadIdent */
    FaultCode faults[8];
    int fault_count;
    int kind;                 /* selects the measuring groups */
} FakeModule;

typedef struct FakeEcu {
    uint64_t now;
    FakeModule modules[FAKE_MAX_MODULES];
    int module_count;
    FakeModule *active;

    /* options */
    int repeat_sync;          /* send $55 01 8A again before the first block */
    int live;                 /* vary measured values between reads */

    /* results for tests */
    int errors;
    int saw_end;

    /* internals */
    int edges;
    uint64_t edge_t[32];
    int edge_level[32];
    uint8_t rx[FAKE_Q];
    int rx_head, rx_tail;
    uint8_t tx[260];
    int tx_len, tx_idx, sending;
    uint8_t out[16][260];
    int out_len[16];
    int out_count;
    uint8_t in[260];
    int in_len;
    int state;
    uint8_t counter;
    int have_counter;
    int extra_pending;
    unsigned tick;
} FakeEcu;

typedef enum {
    FAKE_CAR_GOLF4,     /* engine 01, cluster 17, central convenience 46, KWP2000 airbag 15 */
    FAKE_CAR_EUROVAN,   /* 2002 EuroVan: engine, ABS, HVAC, cluster */
    FAKE_CAR_HARLEQUIN, /* 1996 Golf Mk3: engine, ABS, airbag */
} FakeCar;

/* The Golf IV car (what the protocol tests use). */
void fake_ecu_init(FakeEcu *f, KwIo *io);

void fake_ecu_init_car(FakeEcu *f, KwIo *io, FakeCar car);

#ifdef __cplusplus
}
#endif

#endif
