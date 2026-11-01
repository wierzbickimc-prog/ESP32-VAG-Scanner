#include "health.h"
#include "fake_ecu.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static KwHealthFrame fixture(int check, double t)
{
    KwHealthFrame f = { .check = check, .started_s = t - 1, .finished_s = t };
    snprintf(f.ident, sizeof(f.ident), "%s", check == KW_HEALTH_ATF ? "01P927733CG AG4 Getriebe 01P" : "022906032AF MOTRONIC ME7.1");
    const KwHealthProcedure *p = kw_health_procedure(check);
    f.group_count = p->group_count;
    for (int i = 0; i < p->group_count; i++) {
        KwHealthGroup *g = &f.groups[i];
        g->group = p->groups[i]; g->count = 4; g->time_s = t;
        switch (g->group) {
        case 1: g->values[0] = (SensorValue){ 0x8E, 1, 'P' }; break;
        case 2: g->values[2] = (SensorValue){ 6, 100, 138 }; break;
        case 5: g->values[0] = (SensorValue){ 5, 10, 125 }; break;
        case 4:
            g->values[0] = (SensorValue){ 1, 200, 20 };
            g->values[1] = (SensorValue){ 6, 100, 138 };
            g->values[2] = (SensorValue){ 5, 10, 190 };
            break;
        case 208: case 209:
            g->values[0] = (SensorValue){ 1, 200, 20 };
            g->values[1] = (SensorValue){ 54, 0, 130 };
            g->values[2] = (SensorValue){ 9, 50, g->group == 208 ? 123 : 124 };
            g->values[3] = (SensorValue){ 16, 0, g->group == 208 ? 0x24 : 0xB4 };
            break;
        case 14: g->values[3] = (SensorValue){ 16, 0, 1 }; break;
        case 15: case 16:
            for (int j = 0; j < 3; j++) g->values[j] = (SensorValue){ 54, 0, 0 };
            break;
        }
    }
    return f;
}

static SensorValue *value(KwHealthFrame *f, int group, int field)
{
    for (int i = 0; i < f->group_count; i++)
        if (f->groups[i].group == group) return &f->groups[i].values[field - 1];
    assert(0); return NULL;
}

static void advance(KwHealthFrame *f, double delta)
{
    f->started_s += delta; f->finished_s += delta;
    for (int i = 0; i < f->group_count; i++) f->groups[i].time_s += delta;
}

static int evaluate(KwHealthState *s, KwHealthFrame *f, unsigned confirmations, KwHealthResult *r)
{
    kw_health_evaluate(s, f, confirmations, f->finished_s, r);
    return r->status;
}

static void test_atf(void)
{
    KwHealthState s; KwHealthResult r;
    KwHealthFrame f = fixture(KW_HEALTH_ATF, 10);
    kw_health_init(&s, KW_HEALTH_ATF);
    assert(evaluate(&s, &f, 0, &r) == KW_HEALTH_WAITING);
    assert(evaluate(&s, &f, 15, &r) == KW_HEALTH_WAITING);
    *value(&f, 5, 1) = (SensorValue){ 5, 10, 135 };
    advance(&f, 1);
    assert(evaluate(&s, &f, 15, &r) == KW_HEALTH_READY); /* 35 inclusive */
    *value(&f, 5, 1) = (SensorValue){ 5, 10, 145 };
    advance(&f, 1);
    assert(evaluate(&s, &f, 15, &r) == KW_HEALTH_READY); /* 45 inclusive */
    assert(evaluate(&s, &f, 7, &r) == KW_HEALTH_WAITING); /* manual prereq revoked */
    *value(&f, 1, 1) = (SensorValue){ 0x8E, 0, 'D' };
    assert(evaluate(&s, &f, 15, &r) == KW_HEALTH_WAITING);
    *value(&f, 1, 1) = (SensorValue){ 0x8E, 0, 'P' };
    *value(&f, 2, 3) = (SensorValue){ 6, 100, 114 };
    assert(evaluate(&s, &f, 15, &r) == KW_HEALTH_WAITING);
    *value(&f, 2, 3) = (SensorValue){ 6, 100, 115 };
    assert(evaluate(&s, &f, 15, &r) == KW_HEALTH_READY); /* 11.5 inclusive */
    *value(&f, 5, 1) = (SensorValue){ 5, 10, 146 };
    assert(evaluate(&s, &f, 15, &r) == KW_HEALTH_WAITING);
    *value(&f, 5, 1) = (SensorValue){ 5, 10, 140 };
    assert(evaluate(&s, &f, 15, &r) == KW_HEALTH_WAITING); /* must cool/restart */
    kw_health_init(&s, KW_HEALTH_ATF);
    assert(evaluate(&s, &f, 15, &r) == KW_HEALTH_WAITING); /* initially warm */
    *value(&f, 5, 1) = (SensorValue){ 5, 10, 130 };
    assert(evaluate(&s, &f, 15, &r) == KW_HEALTH_WAITING);
    *value(&f, 5, 1) = (SensorValue){ 5, 10, 140 };
    assert(evaluate(&s, &f, 15, &r) == KW_HEALTH_READY);
    f.fault_count = 1;
    assert(evaluate(&s, &f, 15, &r) == KW_HEALTH_INCONCLUSIVE);
    f.fault_count = 0;
    kw_health_evaluate(&s, &f, 15, f.finished_s + 8.01, &r);
    assert(r.status == KW_HEALTH_INCONCLUSIVE);
    puts("PASS ATF window, cold start, prerequisites, faults, expiry");
}

static void test_timing(void)
{
    KwHealthState s; KwHealthResult r;
    KwHealthFrame f = fixture(KW_HEALTH_TIMING, 10);
    kw_health_init(&s, KW_HEALTH_TIMING);
    assert(evaluate(&s, &f, 7, &r) == KW_HEALTH_WAITING);
    for (int i = 0; i < 20; i++) evaluate(&s, &f, 7, &r);
    assert(s.frames == 1); /* re-rendering one frame cannot fabricate observations */
    advance(&f, 5); assert(evaluate(&s, &f, 7, &r) == KW_HEALTH_WAITING);
    advance(&f, 5); assert(evaluate(&s, &f, 7, &r) == KW_HEALTH_WITHIN_LIMITS);
    assert(r.observed_s == 10);
    *value(&f, 208, 4) = (SensorValue){ 16, 0, 0 };
    assert(evaluate(&s, &f, 7, &r) == KW_HEALTH_INCONCLUSIVE); /* unlearned zero not healthy */
    *value(&f, 208, 4) = (SensorValue){ 16, 0, 0x24 };
    *value(&f, 208, 3) = (SensorValue){ 9, 50, 118 }; /* -9 */
    assert(evaluate(&s, &f, 7, &r) == KW_HEALTH_ATTENTION);
    *value(&f, 208, 3) = (SensorValue){ 9, 50, 119 }; /* -8 inclusive */
    *value(&f, 208, 2) = (SensorValue){ 54, 0, 118 };
    *value(&f, 208, 1) = (SensorValue){ 1, 50, 65 }; /* 650 inclusive */
    assert(evaluate(&s, &f, 7, &r) == KW_HEALTH_WAITING);
    kw_health_init(&s, KW_HEALTH_TIMING);
    *value(&f, 208, 3) = (SensorValue){ 9, 50, 135 }; /* +8 inclusive */
    *value(&f, 208, 2) = (SensorValue){ 54, 0, 134 };
    *value(&f, 208, 1) = (SensorValue){ 1, 50, 140 }; /* 1400 inclusive */
    assert(evaluate(&s, &f, 7, &r) == KW_HEALTH_WAITING);
    *value(&f, 208, 3) = (SensorValue){ 9, 50, 136 };
    assert(evaluate(&s, &f, 7, &r) == KW_HEALTH_ATTENTION);
    *value(&f, 208, 3) = (SensorValue){ 9, 50, 123 };
    *value(&f, 208, 2) = (SensorValue){ 54, 0, 135 };
    assert(evaluate(&s, &f, 7, &r) == KW_HEALTH_ATTENTION);
    f = fixture(KW_HEALTH_TIMING, 50);
    kw_health_init(&s, KW_HEALTH_TIMING);
    evaluate(&s, &f, 7, &r);
    advance(&f, 5);
    *value(&f, 209, 3) = (SensorValue){ 9, 50, 127 }; /* exhaust changes 3 deg */
    assert(evaluate(&s, &f, 7, &r) == KW_HEALTH_INCONCLUSIVE);
    assert(evaluate(&s, &f, 5, &r) == KW_HEALTH_WAITING); /* oil confirmation missing */
    f.fault_count = 1;
    assert(evaluate(&s, &f, 7, &r) == KW_HEALTH_INCONCLUSIVE);
    assert(strstr(r.readings, "Intake") != NULL); /* readings remain available */
    puts("PASS timing limits, adaptation status, sampling window and quality");
}

static void test_integrity(void)
{
    KwHealthState s; KwHealthResult r;
    KwHealthFrame f = fixture(KW_HEALTH_SUPPLY, 10);
    kw_health_init(&s, KW_HEALTH_SUPPLY);
    assert(!kw_health_matches(KW_HEALTH_TIMING, "022906032AFX"));
    assert(!kw_health_matches(KW_HEALTH_TIMING, "037906259E"));
    assert(!kw_health_matches(-1, f.ident));
    assert(evaluate(&s, &f, 3, &r) == KW_HEALTH_WAITING);
    advance(&f, 5); evaluate(&s, &f, 3, &r);
    advance(&f, 5); assert(evaluate(&s, &f, 3, &r) == KW_HEALTH_WITHIN_LIMITS);
    *value(&f, 4, 1) = (SensorValue){ 1, 200, 50 };
    assert(evaluate(&s, &f, 3, &r) == KW_HEALTH_WAITING);
    *value(&f, 4, 1) = (SensorValue){ 1, 200, 20 };
    *value(&f, 4, 2) = (SensorValue){ 6, 100, 114 };
    assert(evaluate(&s, &f, 3, &r) == KW_HEALTH_ATTENTION);
    *value(&f, 4, 2) = (SensorValue){ 5, 10, 138 }; /* temperature cannot masquerade as volts */
    assert(evaluate(&s, &f, 3, &r) == KW_HEALTH_INCONCLUSIVE);
    f = fixture(KW_HEALTH_SUPPLY, 30);
    f.started_s = 1;
    assert(evaluate(&s, &f, 3, &r) == KW_HEALTH_INCONCLUSIVE);
    f = fixture(KW_HEALTH_SUPPLY, 40);
    f.groups[0].time_s = 20;
    assert(evaluate(&s, &f, 3, &r) == KW_HEALTH_INCONCLUSIVE);
    f = fixture(KW_HEALTH_SUPPLY, 50);
    f.groups[0].err = KW_ENAK;
    assert(evaluate(&s, &f, 3, &r) == KW_HEALTH_UNSUPPORTED);
    f = fixture(KW_HEALTH_SUPPLY, 60);
    f.err = KW_EKWP2000;
    assert(evaluate(&s, &f, 3, &r) == KW_HEALTH_UNSUPPORTED);
    f = fixture(KW_HEALTH_SUPPLY, 60);
    f.finished_s = NAN;
    assert(evaluate(&s, &f, 3, &r) == KW_HEALTH_INCONCLUSIVE);
    f = fixture(KW_HEALTH_SUPPLY, 60);
    snprintf(f.ident, sizeof(f.ident), "06A906032HN");
    assert(evaluate(&s, &f, 3, &r) == KW_HEALTH_UNSUPPORTED);
    puts("PASS identity, missing/expired data, units and voltage minimum");
}

static void test_misfire(void)
{
    KwHealthState s; KwHealthResult r;
    KwHealthFrame f = fixture(KW_HEALTH_MISFIRE, 10);
    kw_health_init(&s, KW_HEALTH_MISFIRE);
    assert(evaluate(&s, &f, 1, &r) == KW_HEALTH_INCONCLUSIVE);
    advance(&f, 1);
    *value(&f, 16, 2) = (SensorValue){ 54, 0, 1 };
    assert(evaluate(&s, &f, 1, &r) == KW_HEALTH_ATTENTION);
    assert(strstr(r.readings, "0 / 1 / 0") != NULL);
    kw_health_init(&s, KW_HEALTH_MISFIRE);
    evaluate(&s, &f, 1, &r);
    advance(&f, 1);
    *value(&f, 16, 2) = (SensorValue){ 54, 0, 0 };
    assert(evaluate(&s, &f, 1, &r) == KW_HEALTH_INCONCLUSIVE);
    assert(strstr(r.message, "reset") != NULL);
    advance(&f, 1);
    assert(evaluate(&s, &f, 1, &r) == KW_HEALTH_INCONCLUSIVE); /* no false negative pass */
    puts("PASS misfire increments, counter resets and inactive recognition uncertainty");
}

typedef struct { KwHealthFrame frame; int frames, stop_after, polls, cancel_after; } Capture;
static int capture(void *ctx, const KwHealthFrame *f)
{
    Capture *c = ctx;
    if (!f) return c->cancel_after && ++c->polls >= c->cancel_after;
    c->frame = *f; c->frames++;
    return c->frames >= c->stop_after;
}

static void test_runner(void)
{
    FakeEcu f; KwIo io; KwSession session;
    for (int check = 0; check < KW_HEALTH_COUNT; check++) {
        fake_ecu_init_car(&f, &io, FAKE_CAR_EUROVAN);
        f.health_mode = 1;
        Capture c = { .stop_after = 3 };
        kw_health_run(&session, &io, check, capture, &c);
        assert(c.frames == 3 && c.frame.err == 0);
        assert(c.frame.group_count == kw_health_procedure(check)->group_count);
        assert(c.frame.fault_count == 0);
        assert(f.saw_end && f.errors == 0 && !session.connected);
        assert(f.modules[0].fault_count == 3); /* never cleared */
    }
    fake_ecu_init_car(&f, &io, FAKE_CAR_GOLF4);
    Capture c = { .stop_after = 1 };
    kw_health_run(&session, &io, KW_HEALTH_TIMING, capture, &c);
    assert(c.frames == 1 && c.frame.group_count == 0);
    assert(!kw_health_matches(KW_HEALTH_TIMING, c.frame.ident));
    assert(!session.connected && f.errors == 0);
    fake_ecu_init_car(&f, &io, FAKE_CAR_EUROVAN);
    c = (Capture){ .stop_after = 100, .cancel_after = 3 };
    kw_health_run(&session, &io, KW_HEALTH_TIMING, capture, &c);
    assert(c.frames == 0 && !session.connected && f.saw_end);
    /* Required health group unavailable outside the specialized health demo. */
    fake_ecu_init_car(&f, &io, FAKE_CAR_EUROVAN);
    c = (Capture){ .stop_after = 100 };
    kw_health_run(&session, &io, KW_HEALTH_TIMING, capture, &c);
    assert(c.frames == 1 && c.frame.group_count == 2);
    KwHealthState s; KwHealthResult r;
    kw_health_init(&s, KW_HEALTH_TIMING);
    assert(evaluate(&s, &c.frame, 7, &r) == KW_HEALTH_UNSUPPORTED);
    assert(!session.connected && f.errors == 0);
    puts("PASS real protocol runner, 1200-baud headers, cancellation, unsupported groups; no writes");
}

int main(void)
{
    test_atf(); test_timing(); test_integrity(); test_misfire(); test_runner();
    puts("All health checks passed");
    return 0;
}
