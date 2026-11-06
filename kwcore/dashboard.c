#include "dashboard.h"

#include <stdio.h>
#include <string.h>

/* VW standardized petrol-engine groups (see assets/labels.csv, "@petrol"), and
 * the 01P transmission's ATF temperature, checked on the EuroVan. */
static const KwDashPreset PRESETS[] = {
    { "Coolant temperature", KW_SLOT_SENSOR, 0x01, 4, 3, NULL },
    { "Battery voltage", KW_SLOT_SENSOR, 0x01, 4, 2, NULL },
    { "ATF temperature", KW_SLOT_SENSOR, 0x02, 5, 1, NULL },
    { "Fuel economy", KW_SLOT_FUEL, KW_FUEL_ADDRESS, 0, 0, NULL },
    { "Engine speed", KW_SLOT_SENSOR, 0x01, 1, 1, NULL },
    { "Vehicle speed", KW_SLOT_SENSOR, 0x01, 5, 3, NULL },
    { "Intake air temperature", KW_SLOT_SENSOR, 0x01, 4, 4, NULL },
    { "Engine load", KW_SLOT_SENSOR, 0x01, 2, 2, NULL },
    { "Air mass", KW_SLOT_SENSOR, 0x01, 2, 4, NULL },
    { "Throttle angle", KW_SLOT_SENSOR, 0x01, 3, 3, NULL },
    { "Ignition timing", KW_SLOT_SENSOR, 0x01, 3, 4, NULL },
};

const KwDashPreset *kw_dash_preset(int i)
{
    if (i < 0 || i >= (int)(sizeof(PRESETS) / sizeof(PRESETS[0])))
        return NULL;
    return &PRESETS[i];
}

void kw_dash_slot_from_preset(KwDashSlot *slot, const KwDashPreset *p)
{
    memset(slot, 0, sizeof(*slot));
    slot->kind = p->kind;
    slot->address = p->address;
    slot->group = p->group;
    slot->field = p->field;
    snprintf(slot->name, sizeof(slot->name), "%s", p->name);
}

void kw_dash_default(KwDashLayout *layout)
{
    for (int i = 0; i < KW_DASH_SLOTS; i++)
        kw_dash_slot_from_preset(&layout->slot[i], &PRESETS[i]);
}

static int plan_add(KwDashRead *plan, int n, int max, uint8_t address, uint8_t group)
{
    for (int i = 0; i < n; i++)
        if (plan[i].address == address && plan[i].group == group)
            return n;
    if (n >= max)
        return n;
    plan[n] = (KwDashRead){ address, group };
    return n + 1;
}

int kw_dash_plan(const KwDashLayout *layout, KwDashRead *plan, int max)
{
    KwDashRead all[KW_DASH_PLAN_MAX];
    int n = 0;
    for (int i = 0; i < KW_DASH_SLOTS; i++) {
        const KwDashSlot *s = &layout->slot[i];
        if (s->kind == KW_SLOT_SENSOR) {
            n = plan_add(all, n, KW_DASH_PLAN_MAX, s->address, s->group);
        } else if (s->kind == KW_SLOT_FUEL) {
            n = plan_add(all, n, KW_DASH_PLAN_MAX, KW_FUEL_ADDRESS, KW_FUEL_MAF_GROUP);
            n = plan_add(all, n, KW_DASH_PLAN_MAX, KW_FUEL_ADDRESS, KW_FUEL_SPEED_GROUP);
        }
    }

    /* stable grouping by controller, in order of first appearance */
    int out = 0;
    for (int i = 0; i < n; i++) {
        int seen = 0;
        for (int j = 0; j < i; j++)
            seen |= all[j].address == all[i].address;
        if (seen)
            continue;
        for (int j = i; j < n && out < max; j++)
            if (all[j].address == all[i].address)
                plan[out++] = all[j];
    }
    return out;
}

void kw_dash_run(KwSession *s, KwIo *io, const KwDashRead *plan, int n, int passes,
                 KwDashCallback callback, void *ctx)
{
    if (n <= 0)
        return;
    int single = 1;
    for (int i = 1; i < n; i++)
        single &= plan[i].address == plan[0].address;
    if (passes < 1)
        passes = 1;
    uint8_t visited[128] = {0};

    for (;;) {
        for (int start = 0; start < n;) {
            int end = start;
            while (end < n && plan[end].address == plan[start].address)
                end++;

            KwDashReading connecting = { .address = plan[start].address,
                .state = KW_DASH_CONNECTING };
            if (callback(ctx, &connecting))
                return;
            int index = plan[start].address & 0x7F;
            /* Discover even when a wrong-speed UART delivers no sync byte.
             * Later visits remain quick, including absent controllers. */
            int rc = kw_connect_attempts(s, io, plan[start].address, visited[index] ? 1 : 3);
            visited[index] = 1;
            if (rc != KW_OK) {
                for (int i = start; i < end; i++) {
                    KwDashReading r = { .address = plan[i].address, .group = plan[i].group, .err = rc, .ident = "" };
                    if (callback(ctx, &r))
                        return;
                }
                start = end;
                continue;
            }

            int alive = 1;
            for (int pass = 0; alive && (single || pass < passes); pass++) {
                for (int i = start; i < end && alive; i++) {
                    SensorValue values[16];
                    int got = kw_group_read(s, plan[i].group, values, 16);
                    if (got < 0 && io->trace) {
                        char line[128];
                        snprintf(line, sizeof(line), "dashboard address=%02X phase=group group=%u baud=%d err=%d",
                                 plan[i].address, (unsigned)plan[i].group, io->baud, got);
                        io->trace(io->ctx, line);
                    }
                    KwDashReading r = { .address = plan[i].address, .group = plan[i].group,
                        .err = got < 0 ? got : KW_OK, .count = got < 0 ? 0 : got,
                        .values = values, .ident = s->ident };
                    /* a NAK only means "no such group"; anything else drops the link */
                    if (got < 0 && got != KW_ENAK) {
                        s->connected = 0;
                        alive = 0;
                    }
                    if (callback(ctx, &r)) {
                        kw_end(s);
                        return;
                    }
                }
            }
            kw_end(s);
            start = end;
        }
    }
}

/* Formulas as in kw_format_sensor (kw1281.c), for the types that are numbers. */
int kw_sensor_number(const SensorValue *sv, double *value, const char **unit)
{
    const int A = sv->a, B = sv->b;
    const double a = A, b = B;
    double v;
    const char *u;

    switch (sv->sensor_id) {
    case 1:  v = 0.2 * a * b; u = "rpm"; break;
    case 2:  v = a * 0.002 * b; u = "%"; break;
    case 3:  v = 0.002 * a * b; u = "°"; break;
    case 4:  v = (127 - B) * 0.01 * a; u = "° BTDC"; break;
    case 5:  v = a * (b - 100) * 0.1; u = "°C"; break;
    case 6:  v = 0.001 * a * b; u = "V"; break;
    case 7:  v = 0.01 * a * b; u = "km/h"; break;
    case 8:  v = 0.1 * a * b; u = ""; break;
    case 9:  v = (b - 127) * 0.02 * a; u = "°"; break;
    case 11: v = 0.0001 * a * (b - 128) + 1; u = ""; break;
    case 12: v = 0.001 * a * b; u = "Ω"; break;
    case 13: v = (b - 127) * 0.001 * a; u = "mm"; break;
    case 14: v = 0.005 * a * b; u = "bar"; break;
    case 15: v = 0.01 * a * b; u = "ms"; break;
    case 18: v = 0.04 * a * b; u = "mbar"; break;
    case 19: v = a * b * 0.01; u = "l"; break;
    case 20: v = a * (b - 128) / 128.0; u = "%"; break;
    case 21: v = 0.001 * a * b; u = "V"; break;
    case 22: v = 0.001 * a * b; u = "ms"; break;
    case 23: v = b / 256.0 * a; u = "%"; break;
    case 24: v = 0.001 * a * b; u = "A"; break;
    case 25: v = (b * 1.421) + (a / 182.0); u = "g/s"; break;
    case 26: v = B - A; u = "°C"; break;
    case 27: v = (B - 128) * 0.01 * a; u = "° BTDC"; break;
    case 28: v = B - A; u = ""; break;
    case 31: v = b / 2560.0 * a; u = ""; break;
    case 32: v = B > 128 ? B - 256 : B; u = ""; break;
    case 33: v = A == 0 ? 100.0 * b : 100.0 * b / a; u = "%"; break;
    case 34: v = (b - 128) * 0.01 * a; u = "kW"; break;
    case 35: v = 0.01 * a * b; u = "l/h"; break;
    case 36: v = A * 2560 + B * 10; u = "km"; break;
    case 40: v = b * 0.1 + (25.5 * a) - 400; u = "A"; break;
    case 43: v = b * 0.1 + (25.5 * a); u = "V"; break;
    case 48: v = B + A * 255; u = ""; break;
    case 52: v = b * 0.02 * a - a; u = "Nm"; break;
    case 53: v = (b - 128) * 1.4222 + 0.006 * a; u = "g/s"; break;
    case 54: v = A * 256 + B; u = ""; break;
    case 55: v = a * b / 200.0; u = "s"; break;
    case 60: v = (a * 256 + b) * 0.01; u = "s"; break;
    case 64: v = A + B; u = "Ω"; break;
    case 65: v = 0.01 * a * (b - 127); u = "mm"; break;
    case 66: v = (a * b) / 511.12; u = "V"; break;
    case 0x80: v = a * b; u = "rpm"; break;
    case 0x81: v = a * b / 256.0; u = "%"; break;
    case 0x82: v = a * b / 2560.0; u = "A"; break;
    case 0x83: v = a * b * 0.5 - 30; u = "°"; break;
    case 0x84: v = a * b * 0.5; u = "°"; break;
    case 0x85: v = a * b / 256.0; u = "V"; break;
    case 0x86: v = a * b; u = "km/h"; break;
    case 0x87: v = a * b; u = ""; break;
    case 0x89: v = a * b * 0.01; u = "ms"; break;
    case 0x8A: v = a * b * 0.001; u = "V"; break;
    case 0x8F: v = (b - 128) * a * 0.01; u = "°"; break;
    case 0x90: v = a * b * 0.01; u = "l/h"; break;
    case 0x92: v = 1 + (b - 128) * a * 0.0001; u = ""; break;
    case 0x95: v = (b - 100) * a * 0.1; u = "°C"; break;
    default: return 0;
    }
    *value = v;
    if (unit)
        *unit = u;
    return 1;
}

double kw_unit_convert(double value, const char *unit, int imperial, const char **shown_unit)
{
    const char *u = unit ? unit : "";
    if (imperial) {
        if (strcmp(u, "°C") == 0) {
            value = value * 9.0 / 5.0 + 32.0;
            u = "°F";
        } else if (strcmp(u, "km/h") == 0) {
            value /= 1.609344;
            u = "mph";
        } else if (strcmp(u, "km") == 0) {
            value /= 1.609344;
            u = "mi";
        } else if (strcmp(u, "l") == 0) {
            value /= 3.785411784;
            u = "gal";
        } else if (strcmp(u, "l/h") == 0) {
            value /= 3.785411784;
            u = "gal/h";
        }
    }
    if (shown_unit)
        *shown_unit = u;
    return value;
}

/* ---- fuel economy -------------------------------------------------------- */

#define AFR 14.7           /* stoichiometric petrol */
#define DENSITY_G_L 745.0  /* petrol */
#define MAX_GAP_S 10.0     /* longer gaps (another controller, a stall) aren't integrated */
#define MOVING_KMH 3.0

void kw_fuel_init(KwFuel *f, double factor)
{
    memset(f, 0, sizeof(*f));
    f->factor = factor > 0 ? factor : 1.0;
}

void kw_fuel_reset_trip(KwFuel *f)
{
    f->trip_l = 0;
    f->trip_km = 0;
    f->have_last = 0;
}

/* Integrates the rates held since the last sample up to `t`. */
static void advance(KwFuel *f, double t)
{
    if (f->have_maf && f->have_speed) {
        double dt = t - f->last_t;
        if (f->have_last && dt > 0 && dt <= MAX_GAP_S) {
            f->trip_l += f->flow_lph * dt / 3600.0;
            f->trip_km += f->speed_kmh * dt / 3600.0;
        }
        f->last_t = t;
        f->have_last = 1;
    }
}

void kw_fuel_maf(KwFuel *f, double grams_per_s, double t)
{
    advance(f, t);
    f->flow_lph = grams_per_s / AFR / DENSITY_G_L * 3600.0 * f->factor;
    f->have_maf = 1;
    advance(f, t);
}

void kw_fuel_speed(KwFuel *f, double kmh, double t)
{
    advance(f, t);
    f->speed_kmh = kmh;
    f->have_speed = 1;
    advance(f, t);
}

double kw_fuel_instant(const KwFuel *f)
{
    if (!f->have_maf || !f->have_speed || f->speed_kmh < MOVING_KMH)
        return -1;
    return f->flow_lph / f->speed_kmh * 100.0;
}

double kw_fuel_average(const KwFuel *f)
{
    if (f->trip_km < 0.1)
        return -1;
    return f->trip_l / f->trip_km * 100.0;
}

double kw_fuel_calibrated_factor(const KwFuel *f, double actual_l)
{
    if (f->trip_l <= 0 || actual_l <= 0)
        return f->factor;
    return f->factor * actual_l / f->trip_l;
}

void kw_fuel_format(double l_per_100km, int imperial, char *out, int out_len)
{
    if (l_per_100km <= 0)
        snprintf(out, out_len, "--");
    else if (imperial)
        snprintf(out, out_len, "%.1f mpg", 235.214583 / l_per_100km);
    else
        snprintf(out, out_len, "%.1f l/100km", l_per_100km);
}
