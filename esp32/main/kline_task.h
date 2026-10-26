#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "fake_ecu.h"
#include "kw1281.h"

/*
 * One FreeRTOS task owns the cable and every kwcore call. The UI submits
 * requests and gets results through the callback (called from the K-line
 * task: take the LVGL lock before touching widgets).
 *
 * KL_DEMO switches to a simulated car (kwcore/fake_ecu.c) and scans it; later
 * requests go to that car until a real KL_SCAN.
 */

typedef enum {
    KL_SCAN,
    KL_DEMO,
    KL_FAULTS,
    KL_CLEAR,
    KL_GROUP,
    KL_GROUP_LIVE,  /* repeats the group read in one session until kline_cancel() */
} kl_op_t;

typedef struct {
    kl_op_t op;
    uint8_t address;
    int group;
    FakeCar demo_car;
} kl_request_t;

typedef enum {
    KL_EV_CABLE,        /* cable attached / detached: `ok`; `demo` while simulating */
    KL_EV_PROGRESS,     /* scan probing `address` (`count` = index, `total`) */
    KL_EV_MODULE,       /* scan found `address`, `ident`, `protocol`; ok=false for KWP2000 */
    KL_EV_FAULTS,       /* `count` codes in `faults`, or `err` */
    KL_EV_GROUP,        /* `count` values in `values` named by `labels`, or `err` */
    KL_EV_DONE,         /* request `op` finished (always sent last) */
} kl_event_type_t;

typedef struct {
    kl_event_type_t type;
    kl_op_t op;
    bool ok;
    bool demo;
    int err;
    uint8_t address;
    int protocol;
    const char *ident;
    int count;
    int total;
    const FaultCode *faults;
    const char (*values)[32];
    const char *const *labels;
} kl_event_t;

typedef void (*kl_callback_t)(const kl_event_t *event);

void kline_start(kl_callback_t callback);

/* Queues a request. Returns false if the queue is full. */
bool kline_submit(const kl_request_t *request);

/* Stops a scan or live read at the next step. */
void kline_cancel(void);
