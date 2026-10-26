/*
 * Tests for kw1281.c against the simulated car in fake_ecu.c.
 *
 *   make test
 */
#include "fake_ecu.h"
#include "fault_table.h"
#include "car_profile.h"
#include "labels.h"
#include "kw1281.h"

#include <stdio.h>
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
    };
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        int before = failures;
        tests[i].fn();
        printf("%s %s\n", failures == before ? "PASS" : "FAIL", tests[i].name);
    }
    printf(failures ? "\n%d check(s) failed\n" : "\nall tests passed\n", failures);
    return failures != 0;
}
