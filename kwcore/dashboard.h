#ifndef DASHBOARD_H
#define DASHBOARD_H

/*
 * Four-slot measuring-block dashboard, shared by both apps.
 *
 * A slot shows one field of one measuring group on one controller, or the
 * fuel economy estimate. KW1281 talks to one controller at a time and every
 * switch costs a 5 baud wake-up (2-3 s), so kw_dash_run reads all of one
 * controller's groups several times before moving on. With every slot on one
 * controller it never disconnects.
 *
 * Fuel economy is estimated from the engine ECU's air mass and vehicle speed
 * (VW standard groups 2 and 5): fuel = air / 14.7 (stoichiometric). It can't
 * see overrun fuel cut-off or enrichment, so a calibration factor from a
 * real fill-up corrects it.
 */

#include <stdint.h>
#include "kw1281.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KW_DASH_SLOTS 4
#define KW_DASH_PLAN_MAX 10

/* Where the fuel estimate reads from: engine ECU, VW standard groups. */
#define KW_FUEL_ADDRESS 0x01
#define KW_FUEL_MAF_GROUP 2
#define KW_FUEL_MAF_FIELD 4
#define KW_FUEL_SPEED_GROUP 5
#define KW_FUEL_SPEED_FIELD 3

typedef enum {
    KW_SLOT_EMPTY = 0,
    KW_SLOT_SENSOR,
    KW_SLOT_FUEL,
} KwSlotKind;

typedef struct {
    uint8_t kind;      /* KwSlotKind */
    uint8_t address;   /* hex controller address, 0x01 = engine */
    uint8_t group;
    uint8_t field;     /* 1-based */
    char name[32];
} KwDashSlot;

typedef struct {
    KwDashSlot slot[KW_DASH_SLOTS];
} KwDashLayout;

/* A ready-made slot. `note` says how sure the location is (NULL = standard group). */
typedef struct {
    const char *name;
    uint8_t kind, address, group, field;
    const char *note;
} KwDashPreset;

/* Preset `i`, or NULL past the end. */
const KwDashPreset *kw_dash_preset(int i);

/* Fills `slot` from a preset. */
void kw_dash_slot_from_preset(KwDashSlot *slot, const KwDashPreset *p);

/* EuroVan default: coolant, battery, ATF temperature, fuel economy. */
void kw_dash_default(KwDashLayout *layout);

/* One measuring group to read. */
typedef struct {
    uint8_t address;
    uint8_t group;
} KwDashRead;

/*
 * The groups the layout needs, each once, ordered so a controller's groups
 * are adjacent (controllers in the order their first slot appears).
 * Returns the count.
 */
int kw_dash_plan(const KwDashLayout *layout, KwDashRead *plan, int max);

/* Connecting notifications have no values; they precede each controller visit. */
typedef enum { KW_DASH_READING = 0, KW_DASH_CONNECTING } KwDashState;

/* One group read during kw_dash_run. On error, `err` < 0 and `count` = 0. */
typedef struct {
    uint8_t address;
    uint8_t group;
    int err;
    int count;
    const SensorValue *values;
    const char *ident;
    KwDashState state;
} KwDashReading;

/* Called before connecting and after every read; return nonzero to stop. */
typedef int (*KwDashCallback)(void *ctx, const KwDashReading *reading);

/*
 * Reads `plan` round and round until the callback asks to stop. Each
 * controller gets `passes` rounds of its groups per visit. A controller that
 * doesn't answer gets an error reading for each of its groups, and the next
 * visit tries again. The first visit to each controller allows three wakeup
 * attempts plus bounded speed discovery, including after sync timeouts.
 * Subsequent visits use one attempt with the remembered speed. Connecting
 * notifications use only `address` and `state` (no group values).
 */
void kw_dash_run(KwSession *s, KwIo *io, const KwDashRead *plan, int n, int passes,
                 KwDashCallback callback, void *ctx);

/*
 * The numeric value of a field and its unit ("°C", "V", "km/h", ...).
 * Returns 0 for fields that aren't numbers (status bits, text, time).
 */
int kw_sensor_number(const SensorValue *sv, double *value, const char **unit);

/* Converts a metric value for display. Imperial: °C to °F, km/h to mph, l to gal, km to mi. */
double kw_unit_convert(double value, const char *unit, int imperial, const char **shown_unit);

/* ---- fuel economy -------------------------------------------------------- */

typedef struct {
    double factor;      /* calibration, 1.0 until calibrated against a fill-up */
    double flow_lph;    /* latest fuel flow, l/h */
    double speed_kmh;
    double trip_l;      /* since the last reset */
    double trip_km;
    double last_t;      /* seconds, caller's clock */
    int have_maf, have_speed, have_last;
} KwFuel;

void kw_fuel_init(KwFuel *f, double factor);
void kw_fuel_reset_trip(KwFuel *f);

/* New samples at time `t` (seconds, any monotonic clock). */
void kw_fuel_maf(KwFuel *f, double grams_per_s, double t);
void kw_fuel_speed(KwFuel *f, double kmh, double t);

/* l/100 km now, or -1 when standing still (show flow_lph instead). */
double kw_fuel_instant(const KwFuel *f);

/* l/100 km over the trip, or -1 before 0.1 km. */
double kw_fuel_average(const KwFuel *f);

/* The factor that makes the trip's fuel match `actual_l` from the pump. */
double kw_fuel_calibrated_factor(const KwFuel *f, double actual_l);

/* "21.4 mpg" (imperial, US gallons) or "11.0 l/100km". */
void kw_fuel_format(double l_per_100km, int imperial, char *out, int out_len);

#ifdef __cplusplus
}
#endif

#endif
