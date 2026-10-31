/*
 * Tests for kw1281.c against the simulated car in fake_ecu.c.
 *
 *   make test
 */
#include "fake_ecu.h"
#include "fault_table.h"
#include "car_profile.h"
#include "labels.h"
#include "dashboard.h"
#include "kw1281.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            fprintf(stderr, "  %s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);   \
            failures++;                                                                   \
        }                                                                                 \
    } while (0)

static void test_connect_ident(void)
{
    FakeEcu f;
    KwIo io;
    KwSession s;
    fake_ecu_init(&f, &io);

    CHECK(kw_connect(&s, &io, 0x01) == KW_OK);
    CHECK(s.protocol == 1281);
    CHECK(strstr(s.ident, "06A906032HN 1.8L R4/5VT") != NULL);
    CHECK(strstr(s.ident, "G   0003") != NULL);
    CHECK(strstr(s.ident, "\nCoding 04500") != NULL); /* second round via ReadIdent */
    CHECK(f.extra_pending == 0);

    kw_end(&s);
    CHECK(f.saw_end);
    CHECK(f.errors == 0);
}

static void test_faults(void)
{
    FakeEcu f;
    KwIo io;
    KwSession s;
    FaultCode codes[8];
    char text[32];
    fake_ecu_init(&f, &io);

    CHECK(kw_connect(&s, &io, 0x01) == KW_OK);
    int n = kw_read_fault_codes(&s, codes, 8); /* arrives as two blocks */
    CHECK(n == 3);
    if (n == 3) {
        kw_format_fault(&codes[0], text, sizeof(text));
        CHECK(strcmp(text, "00514 - 35-00") == 0);
        kw_format_fault(&codes[1], text, sizeof(text));
        CHECK(strcmp(text, "00258 - 35-10") == 0);
        kw_format_fault(&codes[2], text, sizeof(text));
        CHECK(strcmp(text, "16497 - 29-00") == 0);
    }
    /* a second request in the same session must work too */
    CHECK(kw_read_fault_codes(&s, codes, 8) == 3);
    CHECK(kw_read_fault_codes(&s, codes, 2) == KW_EOVERFLOW);

    CHECK(kw_clear_fault_codes(&s, codes, 8) == 0);
    CHECK(kw_read_fault_codes(&s, codes, 8) == 0);
    kw_end(&s);
    CHECK(f.errors == 0);
}

static void test_groups(void)
{
    FakeEcu f;
    KwIo io;
    KwSession s;
    SensorValue v[8];
    char text[64];
    fake_ecu_init(&f, &io);

    CHECK(kw_connect(&s, &io, 0x01) == KW_OK);
    int n = kw_group_read(&s, 4, v, 8);
    CHECK(n == 4);
    if (n == 4) {
        kw_format_sensor(&v[0], text, sizeof(text));
        CHECK(strcmp(text, "800 rpm") == 0);
        kw_format_sensor(&v[1], text, sizeof(text));
        CHECK(strcmp(text, "13.8 V") == 0);
        kw_format_sensor(&v[2], text, sizeof(text));
        CHECK(strcmp(text, "90.0 °C") == 0);
        kw_format_sensor(&v[3], text, sizeof(text));
        CHECK(strcmp(text, "28.0 °C") == 0);
    }
    CHECK(kw_group_read(&s, 99, v, 8) == KW_ENAK);
    CHECK(kw_keep_alive(&s) == KW_OK);
    CHECK(kw_group_read(&s, 1, v, 8) == 4);
    kw_end(&s);
    CHECK(f.errors == 0);
}

static void test_no_answer(void)
{
    FakeEcu f;
    KwIo io;
    KwSession s;
    fake_ecu_init(&f, &io);

    uint64_t t = f.now;
    CHECK(kw_connect(&s, &io, 0x02) == KW_ENOWAKE);
    CHECK(s.sync_byte == -1);
    /* 3 attempts of ~2.3 s + 2 s timeout, 1 s apart */
    CHECK(f.now - t > 3 * 4000000ull);
    CHECK(f.errors == 0);
}

static void test_kwp2000(void)
{
    FakeEcu f;
    KwIo io;
    KwSession s;
    fake_ecu_init(&f, &io);

    CHECK(kw_connect_attempts(&s, &io, 0x15, 1) == KW_EKWP2000);
    CHECK(s.protocol == 2025);
    CHECK(kw_connect(&s, &io, 0x17) == KW_OK); /* bus usable afterwards */
    kw_end(&s);
    CHECK(f.errors == 0);
}

static void test_repeated_sync(void)
{
    FakeEcu f;
    KwIo io;
    KwSession s;
    fake_ecu_init(&f, &io);
    f.repeat_sync = 1;

    CHECK(kw_connect(&s, &io, 0x46) == KW_OK);
    CHECK(strstr(s.ident, "Central Conv.") != NULL);
    kw_end(&s);
    CHECK(f.errors == 0);
}

static void test_scan(void)
{
    FakeEcu f;
    KwIo io;
    KwSession s;
    fake_ecu_init(&f, &io);

    int ok = 0, kwp = 0;
    for (int addr = 1; addr <= 0x7F; addr++) {
        int err = kw_connect_attempts(&s, &io, (uint8_t)addr, 1);
        if (err == KW_OK) {
            ok++;
            kw_end(&s);
        } else if (err == KW_EKWP2000) {
            kwp++;
        }
    }
    CHECK(ok == 3);
    CHECK(kwp == 1);
    CHECK(f.errors == 0);
}

/* Scans a simulated car end to end and returns what the idents vote for. */
static KwCar identify(FakeCar car)
{
    FakeEcu f;
    KwIo io;
    KwSession s;
    KwCarVotes votes = { { 0 } };
    fake_ecu_init_car(&f, &io, car);
    for (int addr = 1; addr <= 0x7F; addr++) {
        if (kw_connect_attempts(&s, &io, (uint8_t)addr, 1) == KW_OK) {
            kw_car_vote(&votes, s.ident);
            kw_end(&s);
        }
    }
    CHECK(f.errors == 0);
    return kw_car_best(&votes);
}

static void test_car_detection(void)
{
    CHECK(identify(FAKE_CAR_EUROVAN) == KW_CAR_EUROVAN);
    CHECK(identify(FAKE_CAR_HARLEQUIN) == KW_CAR_HARLEQUIN);
    CHECK(identify(FAKE_CAR_GOLF4) == KW_CAR_UNKNOWN);

    /* an engine-only scan still leans the right way */
    KwCarVotes v = { { 0 } };
    kw_car_vote(&v, "022906032BK 2.8L V6/5V\nG   0002");
    CHECK(kw_car_best(&v) == KW_CAR_EUROVAN);
    CHECK(strcmp(kw_car_profile(KW_CAR_HARLEQUIN)->key, "harlequin") == 0);
    CHECK(strcmp(kw_car_profile(KW_CAR_UNKNOWN)->key, "family") == 0);
}

static void test_labels(void)
{
    const char csv[] =
        "# comment\npart,group,field,label\r\n"
        "7D0907379F,1,1,Wheel speed front left\r\n"
        "7D0907379F, 1, 2 , Wheel speed front right \n";
    CHECK(kw_labels_load(csv, sizeof(csv) - 1) == 2);
    CHECK(strcmp(kw_label("7D0907379F  ABS/EDS 20 IE", 1, 1, 7), "Wheel speed front left") == 0);
    CHECK(strcmp(kw_label("7D0907379F  ABS/EDS 20 IE", 1, 2, 7), "Wheel speed front right") == 0);
    CHECK(strcmp(kw_label("7D0907379F  ABS/EDS 20 IE", 2, 1, 6), "Voltage") == 0);   /* generic */
    CHECK(strcmp(kw_label("1H0907379D", 1, 1, 7), "Speed") == 0);                     /* other part */
    CHECK(strcmp(kw_sensor_kind(1), "Engine speed") == 0);

    const char std[] =
        "part,group,field,label\n"
        "@petrol,4,2,Voltage\n"
        "037906259,4,2,Battery voltage (ABA)\n";
    CHECK(kw_labels_load(std, sizeof(std) - 1) == 2);
    CHECK(strcmp(kw_label("022906032BK 2.8L V6/5V", 4, 2, 6), "Voltage") == 0);       /* standard */
    CHECK(kw_labels_standard("022906032BK 2.8L V6/5V", 4));
    CHECK(strcmp(kw_label("037906259E  2.0l", 4, 2, 6), "Battery voltage (ABA)") == 0); /* specific wins */
    CHECK(!kw_labels_standard("037906259E  2.0l", 4));
    CHECK(strcmp(kw_label("7D0907379F  ABS", 4, 2, 6), "Voltage") == 0);                /* generic kind */
    CHECK(!kw_labels_standard("7D0907379F  ABS", 4));                                    /* not an engine */
}

static void test_fault_table(void)
{
    const char csv[] =
        "Fault Code,Meaning\r\n"
        "00515,\"Cam Sensor (G40): No Signal, \"\"odd\"\"\"\r\n"
        "16497/P0113/000275,Intake Air Temp. Sensor (G42): Signal too High\r\n"
        "P0300,Misfire (derived)\r\n"
        "16684/P0300/000768,Misfire (explicit)\r\n"
        "P1101,O2 Sensor Circuit\r\n"
        "P000A/000010,hex P-code: skipped\r\n"
        "B1000,body code: skipped\r\n";
    char pc[8];

    CHECK(kw_fault_table_load(csv, sizeof(csv) - 1) == 4);
    CHECK(strcmp(kw_fault_describe(515), "Cam Sensor (G40): No Signal, \"odd\"") == 0);
    CHECK(strcmp(kw_fault_describe(16497), "Intake Air Temp. Sensor (G42): Signal too High") == 0);
    CHECK(strcmp(kw_fault_describe(16684), "Misfire (explicit)") == 0);
    CHECK(strcmp(kw_fault_describe(17509), "O2 Sensor Circuit") == 0);
    CHECK(kw_fault_describe(514) == NULL);
    CHECK(kw_fault_pcode(16497, pc, sizeof(pc)) && strcmp(pc, "P0113") == 0);
    CHECK(kw_fault_pcode(17509, pc, sizeof(pc)) && strcmp(pc, "P1101") == 0);
    CHECK(!kw_fault_pcode(515, pc, sizeof(pc)));

#if defined(__has_include) && __has_include("fault_types.h")
    CHECK(strcmp(kw_fault_type(0x07), "Signal too Low") == 0);
    CHECK(strcmp(kw_fault_type(0x9B), "Implausible Signal") == 0); /* intermittent bit ignored */
    CHECK(strcmp(kw_fault_type(0x1D), "Short to Ground") == 0);
    CHECK(strcmp(kw_fault_type(0x2F), "No Communications or Incorrectly Connected") == 0);
    CHECK(kw_fault_type(0x00) == NULL);
    CHECK(kw_fault_type(0xA3) == NULL); /* 35 "-" */
    CHECK(kw_fault_type(0x22) == NULL); /* "No Elaboration Available" */
    CHECK(kw_fault_type(0x7F) == NULL);
#else
    CHECK(kw_fault_type(0x1D) == NULL); /* list not fetched: number only */
#endif
}


static void test_baud_switch(void)
{
    FakeEcu f;
    KwIo io;
    KwSession s;

    /* the EuroVan's ME7.1 answers at 10400: one garbled sync at 9600, then it connects */
    fake_ecu_init_car(&f, &io, FAKE_CAR_EUROVAN);
    CHECK(kw_connect_attempts(&s, &io, 0x01, 1) == KW_OK);
    CHECK(s.baud == 10400 && io.baud == 10400 && f.baud == 10400);
    CHECK(f.baud_switches == 1);
    CHECK(strstr(s.ident, "ME7.1") != NULL);
    kw_end(&s);

    /* a 9600 controller after it: switches back */
    CHECK(kw_connect_attempts(&s, &io, 0x17, 1) == KW_OK);
    CHECK(s.baud == 10400); /* the fake's other controllers answer at any speed */
    kw_end(&s);

    /* the speed stays: the next engine connect needs no switch */
    CHECK(kw_connect_attempts(&s, &io, 0x01, 1) == KW_OK);
    CHECK(f.baud_switches == 1);
    kw_end(&s);

    /* without set_baud the old behaviour remains: no response, sync byte reported */
    fake_ecu_init_car(&f, &io, FAKE_CAR_EUROVAN);
    io.set_baud = NULL;
    CHECK(kw_connect_attempts(&s, &io, 0x01, 1) == KW_ENOWAKE);
    CHECK(s.sync_byte == 0xB5);
    CHECK(f.errors == 0);
}

typedef struct {
    int count, stop_after;
    int per_address[256];
    int errors;
    double coolant, atf;
    KwDashReading order[64];
} DashLog;

static int dash_cb(void *ctx, const KwDashReading *r)
{
    DashLog *log = ctx;
    if (log->count < 64)
        log->order[log->count] = *r;
    log->count++;
    if (r->err < 0) {
        log->errors++;
    } else {
        log->per_address[r->address]++;
        if (r->address == 0x01 && r->group == 4 && r->count >= 3)
            kw_sensor_number(&r->values[2], &log->coolant, NULL);
        if (r->address == 0x02 && r->group == 5 && r->count >= 1)
            kw_sensor_number(&r->values[0], &log->atf, NULL);
    }
    return log->count >= log->stop_after;
}

static void test_dashboard_plan(void)
{
    KwDashLayout layout;
    KwDashRead plan[KW_DASH_PLAN_MAX];
    kw_dash_default(&layout);
    CHECK(strcmp(layout.slot[0].name, "Coolant temperature") == 0);
    CHECK(layout.slot[3].kind == KW_SLOT_FUEL);

    /* coolant + battery share 01/4; ATF is 02/5; fuel adds 01/2 and 01/5, grouped by controller */
    int n = kw_dash_plan(&layout, plan, KW_DASH_PLAN_MAX);
    CHECK(n == 4);
    if (n == 4) {
        CHECK(plan[0].address == 0x01 && plan[0].group == 4);
        CHECK(plan[1].address == 0x01 && plan[1].group == 2);
        CHECK(plan[2].address == 0x01 && plan[2].group == 5);
        CHECK(plan[3].address == 0x02 && plan[3].group == 5);
    }
    layout.slot[2].kind = KW_SLOT_EMPTY;
    layout.slot[3].kind = KW_SLOT_EMPTY;
    CHECK(kw_dash_plan(&layout, plan, KW_DASH_PLAN_MAX) == 1);
    for (int i = 0; i < KW_DASH_SLOTS; i++)
        layout.slot[i].kind = KW_SLOT_EMPTY;
    CHECK(kw_dash_plan(&layout, plan, KW_DASH_PLAN_MAX) == 0);
}

static void test_dashboard_run(void)
{
    FakeEcu f;
    KwIo io;
    KwSession s;
    KwDashLayout layout;
    KwDashRead plan[KW_DASH_PLAN_MAX];
    static DashLog log;

    /* two controllers: 3 passes of 01's three groups, then 02, then back */
    fake_ecu_init_car(&f, &io, FAKE_CAR_EUROVAN);
    kw_dash_default(&layout);
    int n = kw_dash_plan(&layout, plan, KW_DASH_PLAN_MAX);
    memset(&log, 0, sizeof(log));
    log.stop_after = 9 + 3 + 2;
    kw_dash_run(&s, &io, plan, n, 3, dash_cb, &log);
    CHECK(log.errors == 0);
    CHECK(log.per_address[0x01] == 11 && log.per_address[0x02] == 3);
    CHECK(log.order[8].address == 0x01 && log.order[9].address == 0x02);
    CHECK(log.order[12].address == 0x01);
    CHECK(f.wakes == 3);
    CHECK(fabs(log.coolant - 90.0) < 0.01);
    CHECK(fabs(log.atf - 75.0) < 0.01);
    CHECK(f.saw_end);
    CHECK(f.errors == 0);

    /* one controller: stays connected */
    fake_ecu_init_car(&f, &io, FAKE_CAR_EUROVAN);
    layout.slot[2].kind = KW_SLOT_EMPTY;
    n = kw_dash_plan(&layout, plan, KW_DASH_PLAN_MAX);
    memset(&log, 0, sizeof(log));
    log.stop_after = 30;
    kw_dash_run(&s, &io, plan, n, 3, dash_cb, &log);
    CHECK(f.wakes == 1);
    CHECK(log.per_address[0x01] == 30);
    CHECK(f.errors == 0);

    /* missing controller and missing group: errors, then carries on */
    fake_ecu_init_car(&f, &io, FAKE_CAR_GOLF4);
    KwDashRead missing[] = { { 0x02, 5 }, { 0x01, 99 }, { 0x01, 4 } };
    memset(&log, 0, sizeof(log));
    log.stop_after = 6;
    kw_dash_run(&s, &io, missing, 3, 2, dash_cb, &log);
    CHECK(log.order[0].err == KW_ENOWAKE);
    CHECK(log.order[1].err == KW_ENAK);
    CHECK(log.order[2].err == KW_OK && log.order[2].count == 4);
    CHECK(log.order[3].err == KW_ENAK);
    CHECK(log.order[5].err == KW_ENOWAKE); /* next round tries 02 again */
    CHECK(f.errors == 0);
}

static void test_sensor_numbers(void)
{
    /* kw_sensor_number must agree with the text kw_format_sensor shows */
    static const int ids[] = { 1, 2, 3, 5, 6, 7, 8, 9, 11, 12, 13, 14, 15, 18, 19, 20, 21, 22, 23, 24,
                               25, 26, 28, 31, 32, 33, 34, 35, 36, 40, 43, 48, 52, 53, 54, 55, 60, 64, 65, 66 };
    static const int samples[][2] = { { 0, 0 }, { 1, 1 }, { 10, 190 }, { 100, 138 }, { 200, 20 },
                                      { 255, 255 }, { 37, 128 }, { 3, 250 } };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        for (size_t j = 0; j < sizeof(samples) / sizeof(samples[0]); j++) {
            SensorValue sv = { ids[i], samples[j][0], samples[j][1] };
            char text[64];
            double v;
            kw_format_sensor(&sv, text, sizeof(text));
            CHECK(kw_sensor_number(&sv, &v, NULL));
            double shown = strtod(text, NULL);
            if (fabs(shown - v) > 0.051 + fabs(v) * 1e-6) {
                fprintf(stderr, "  type %d (%d,%d): \"%s\" vs %g\n", ids[i], sv.a, sv.b, text, v);
                failures++;
            }
        }
    }
    SensorValue status = { 16, 0, 1 };
    double v;
    CHECK(!kw_sensor_number(&status, &v, NULL));

    const char *unit;
    SensorValue coolant = { 5, 10, 190 };
    CHECK(kw_sensor_number(&coolant, &v, &unit) && strcmp(unit, "°C") == 0);
    CHECK(fabs(kw_unit_convert(v, unit, 1, &unit) - 194.0) < 1e-9 && strcmp(unit, "°F") == 0);
    CHECK(fabs(kw_unit_convert(96.56064, "km/h", 1, &unit) - 60.0) < 1e-6 && strcmp(unit, "mph") == 0);
    CHECK(kw_unit_convert(13.8, "V", 1, &unit) == 13.8 && strcmp(unit, "V") == 0);
}

static void test_fuel(void)
{
    KwFuel f;
    char text[32];
    kw_fuel_init(&f, 1.0);
    CHECK(kw_fuel_instant(&f) < 0);

    /* 30 g/s of air at 96 km/h for an hour */
    for (int t = 0; t <= 3600; t++) {
        kw_fuel_maf(&f, 30.0, t);
        kw_fuel_speed(&f, 96.0, t + 0.5);
    }
    double lph = 30.0 / 14.7 / 745.0 * 3600.0; /* 9.86 l/h */
    CHECK(fabs(f.flow_lph - lph) < 1e-9);
    CHECK(fabs(kw_fuel_instant(&f) - lph / 96.0 * 100.0) < 1e-9);
    CHECK(fabs(f.trip_km - 96.0) < 0.1);
    CHECK(fabs(f.trip_l - lph) < 0.01);
    CHECK(fabs(kw_fuel_average(&f) - kw_fuel_instant(&f)) < 0.01);
    kw_fuel_format(kw_fuel_average(&f), 1, text, sizeof(text));
    CHECK(strcmp(text, "22.9 mpg") == 0);
    kw_fuel_format(kw_fuel_average(&f), 0, text, sizeof(text));
    CHECK(strcmp(text, "10.3 l/100km") == 0);

    /* pumped 10% more than estimated: the factor scales flow */
    double factor = kw_fuel_calibrated_factor(&f, f.trip_l * 1.1);
    CHECK(fabs(factor - 1.1) < 1e-9);

    /* a long gap (reading another controller for too long) isn't integrated */
    double before = f.trip_km;
    kw_fuel_speed(&f, 96.0, 3600 + 60);
    CHECK(f.trip_km == before);

    kw_fuel_speed(&f, 0.0, 3700);
    CHECK(kw_fuel_instant(&f) < 0); /* standing: show flow instead */
    kw_fuel_reset_trip(&f);
    CHECK(kw_fuel_average(&f) < 0);
}

int main(void)
{
    struct { const char *name; void (*fn)(void); } tests[] = {
        { "connect + multi-round ident", test_connect_ident },
        { "read / clear faults (multi-block)", test_faults },
        { "measuring groups + NAK + keep-alive", test_groups },
        { "no answer, 3 retries", test_no_answer },
        { "KWP2000 controller detected", test_kwp2000 },
        { "repeated sync before first block", test_repeated_sync },
        { "full scan 01-7F", test_scan },
        { "fault code table CSV + fault types", test_fault_table },
        { "car detection from idents", test_car_detection },
        { "measuring-block labels", test_labels },
        { "baud switch to 10400 (ME7)", test_baud_switch },
        { "dashboard plan", test_dashboard_plan },
        { "dashboard reads across controllers", test_dashboard_run },
        { "sensor numbers match formatted text", test_sensor_numbers },
        { "fuel economy", test_fuel },
    };
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        int before = failures;
        tests[i].fn();
        printf("%s %s\n", failures == before ? "PASS" : "FAIL", tests[i].name);
    }
    printf(failures ? "\n%d check(s) failed\n" : "\nall tests passed\n", failures);
    return failures != 0;
}
