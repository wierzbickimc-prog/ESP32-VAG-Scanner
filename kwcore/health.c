#include "health.h"
#include "dashboard.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define AXK_MANUAL "https://charm.li/Volkswagen/2002/Eurovan%20GLS%20V6-2.8L%20%28AXK%29/Repair%20and%20Diagnosis/"
#define ATF_MANUAL "https://workshop-manuals.com/volkswagen/eurovan_camper/v6-2.8l_%28axk%29/transmission_and_drivetrain/automatic_transmission/transaxle/fluid_a/t/component_information/testing_and_inspection/4_speed_01p_transmission/"

/* Sources, limits and deliberate exclusions are documented in docs/health-checks.md.
 * Only the owner's identified controllers are allowlisted. Chassis/photo votes
 * and the generic @petrol labels are never evidence of test applicability. */
static const KwHealthProcedure PROCEDURES[KW_HEALTH_COUNT] = {
    {
        .title = "ATF level assistant",
        .description = "01P automatic: follow ATF temperature into the physical level-check window.",
        .preparation = "Begin with ATF at or below approximately 30 C (86 F). Keep the engine idling in P. Inspect the overflow at 35-45 C (95-113 F). The first fluid from the overflow tube alone does not establish the level; observe the subsequent flow. Use a new plug seal; factory plug torque is 15 Nm.",
        .confirmation = { "Vehicle level; parking brake applied",
            "Engine running at idle in P",
            "Consumers off; fuses and engine/body/transmission grounds checked",
            "Transmission is not in emergency running mode" },
        .source_url = ATF_MANUAL "page_3087/", .source_title = "VW 01P: ATF level prerequisites (continue through page 3089)",
        .limitations = "Ready means conditions for inspection are met. The scanner cannot measure fluid level. Record the physical result while live readiness is shown. Engine idle and emergency mode require your confirmation.",
        .address = 0x02, .groups = { 1, 2, 5 }, .group_count = 3, .required_confirmations = 15
    },
    {
        .title = "Camshaft timing / chain assessment",
        .description = "AXK 24V VR6: compare adapted intake and exhaust offsets at idle.",
        .preparation = "Correct oil level; oil at least 80 C (176 F); radiator fan has cycled. Coolant temperature is displayed for context and does not establish oil temperature. The AXK intake table specifies 650-1400 rpm, phase flank 118-134, offset -8 to +8 crankshaft degrees, status XX1XX100. Observe a stable window of at least 10 seconds and three complete reads.",
        .confirmation = { "Engine oil level checked and correct",
            "Oil at least 80 C confirmed; radiator fan has cycled",
            "Vehicle stationary; engine idling" },
        .source_url = AXK_MANUAL "Engine%2C%20Cooling%20and%20Exhaust/Engine/Camshaft%2C%20Lifters%20and%20Push%20Rods/Camshaft/Testing%20and%20Inspection/Camshaft%20Adjustment%2C%20Checking/",
        .source_title = "VW AXK: camshaft adjustment, checking",
        .limitations = "Passive screening only. The factory's full procedure also checks actuator activation and oil pressure. Intake limits are verified for AXK; exhaust offset and intake/exhaust difference are observations, without a verified AXK rejection limit. A good intake result does not establish chain or guide condition.",
        .address = 0x01, .groups = { 4, 208, 209 }, .group_count = 3, .required_confirmations = 7
    },
    {
        .title = "Misfire activity",
        .description = "AXK: monitor cylinder counters through warm-up and compare successive reads.",
        .preparation = "Park with the parking brake applied. Start the engine and observe through warm-up. Groups 015 and 016 contain cylinders 1-3 and 4-6. An increasing counter indicates observed activity, but does not identify the cause.",
        .confirmation = { "Vehicle stationary; parking brake applied" },
        .source_url = "https://workshop-manuals.com/volkswagen/eurovan_mv/v6-2.8l_%28axk%29/powertrain_management/ignition_system/ignition_coil/component_information/technical_service_bulletins/all_other_service_bulletins_for_ignition_coil/0103-02/feb/03/ignition_system_mil_on/misfire_dtcs_set/rough_running/page_2842/",
        .source_title = "VW bulletin 01-03-02: AXK misfire counters",
        .limitations = "No increase is inconclusive: recognition may be disabled, intermittent or outside its operating conditions. Recognition status from 014/4 is shown raw, without guessing its encoding. Counter resets restart the observation window. Do not clear faults to run this check.",
        .address = 0x01, .groups = { 4, 14, 15, 16 }, .group_count = 4, .required_confirmations = 1
    },
    {
        .title = "ECU supply voltage",
        .description = "AXK: check the factory minimum voltage at the engine controller.",
        .preparation = "Engine idling. Switch off lights, rear-window heater, A/C and other electrical consumers. Group 004 field 2 must be at least 11.5 V. Observe for at least 10 seconds and three complete reads.",
        .confirmation = { "Engine idling; electrical consumers and A/C off",
            "Fuses and ground connections checked" },
        .source_url = AXK_MANUAL "Powertrain%20Management/Computers%20and%20Control%20Systems/Engine%20Control%20Module/Testing%20and%20Inspection/",
        .source_title = "VW AXK: voltage supply for control module, checking",
        .limitations = "Checks the documented minimum at the ECU. It is not a battery capacity test or a complete alternator/charging-system diagnosis.",
        .address = 0x01, .groups = { 4 }, .group_count = 1, .required_confirmations = 3
    }
};

const KwHealthProcedure *kw_health_procedure(int check)
{
    return check >= 0 && check < KW_HEALTH_COUNT ? &PROCEDURES[check] : NULL;
}

const char *kw_health_status_name(int status)
{
    static const char *names[] = { "Waiting for conditions", "Ready for physical inspection",
        "Within documented limits", "Attention needed", "Inconclusive", "Unsupported" };
    return status >= 0 && status <= KW_HEALTH_UNSUPPORTED ? names[status] : "Inconclusive";
}

int kw_health_matches(int check, const char *ident)
{
    if (!kw_health_procedure(check) || !ident)
        return 0;
    /* Compare a whole part token, not a prefix that can accept a different suffix. */
    const char *part = check == KW_HEALTH_ATF ? "01P927733CG" : "022906032AF";
    size_t n = strlen(part);
    while (*ident == ' ') ident++;
    return strncmp(ident, part, n) == 0
        && (ident[n] == '\0' || ident[n] == ' ' || ident[n] == '\n');
}

void kw_health_init(KwHealthState *s, int check)
{
    memset(s, 0, sizeof(*s));
    s->check = check;
    s->last_s = -1;
}

static void reset_window(KwHealthState *s)
{
    s->frames = 0;
    s->first_s = 0;
    s->previous_valid = 0;
}

static void result(KwHealthState *s, KwHealthResult *r, int status, const char *message)
{
    r->status = status;
    snprintf(r->message, sizeof(r->message), "%s", message);
    if (status == KW_HEALTH_WAITING || status == KW_HEALTH_INCONCLUSIVE || status == KW_HEALTH_UNSUPPORTED)
        reset_window(s);
}

static const KwHealthGroup *group(const KwHealthFrame *f, int number)
{
    for (int i = 0; i < f->group_count && i < 4; i++)
        if (f->groups[i].group == number)
            return &f->groups[i];
    return NULL;
}

static const SensorValue *field(const KwHealthFrame *f, int g, int i)
{
    const KwHealthGroup *v = group(f, g);
    return v && !v->err && i >= 1 && i <= v->count && i <= 4 ? &v->values[i - 1] : NULL;
}

static int number(const KwHealthFrame *f, int g, int i, const char *expected_unit, double *out)
{
    const SensorValue *v = field(f, g, i);
    const char *unit;
    return v && kw_sensor_number(v, out, &unit) && isfinite(*out)
        && strcmp(unit, expected_unit) == 0;
}

static void observe(KwHealthState *s, const KwHealthFrame *f, KwHealthResult *r)
{
    if (!s->frames || f->finished_s != s->last_s) {
        if (!s->frames) s->first_s = f->finished_s;
        s->frames++;
        s->last_s = f->finished_s;
    }
    r->observed_s = f->finished_s - s->first_s;
}

void kw_health_evaluate(KwHealthState *s, const KwHealthFrame *f,
                        unsigned confirmations, double now_s, KwHealthResult *r)
{
    memset(r, 0, sizeof(*r));
    r->status = KW_HEALTH_INCONCLUSIVE;
    const KwHealthProcedure *p = kw_health_procedure(s->check);
    if (!p || !f || f->check != s->check) {
        result(s, r, KW_HEALTH_INCONCLUSIVE, "Start a new check to collect measurements.");
        return;
    }
    if (f->err == KW_EKWP2000) {
        result(s, r, KW_HEALTH_UNSUPPORTED, "This controller uses KWP2000, which this app does not support.");
        return;
    }
    if (!kw_health_matches(s->check, f->ident)) {
        result(s, r, f->err ? KW_HEALTH_INCONCLUSIVE : KW_HEALTH_UNSUPPORTED,
               f->err ? "Controller did not answer. Check the ignition and cable, then start again."
                      : "This controller has no verified EuroVan procedure. No health limits have been applied.");
        return;
    }
    if (f->err || !isfinite(now_s) || !isfinite(f->finished_s) || !isfinite(f->started_s)
        || now_s < f->finished_s || now_s - f->finished_s > 8
        || f->finished_s < f->started_s || f->finished_s - f->started_s > 12) {
        result(s, r, KW_HEALTH_INCONCLUSIVE, "Read failed, expired, or took too long. Collect a fresh complete set.");
        return;
    }
    for (int i = 0; i < p->group_count; i++) {
        const KwHealthGroup *g = group(f, p->groups[i]);
        if (!g || g->err || g->count < 1 || g->count > 4
            || !isfinite(g->time_s) || g->time_s < f->started_s || g->time_s > f->finished_s) {
            result(s, r, g && g->err == KW_ENAK ? KW_HEALTH_UNSUPPORTED : KW_HEALTH_INCONCLUSIVE,
                   g && g->err == KW_ENAK ? "A required measuring group is unavailable on this controller."
                                         : "A required measuring group is missing or invalid.");
            return;
        }
    }
    if (s->last_s >= 0 && (f->finished_s < s->last_s || f->finished_s - s->last_s > 8))
        reset_window(s);
    if (confirmations != s->confirmations) {
        reset_window(s);
        s->confirmations = confirmations;
    }

    double rpm, volts, coolant;
    if (s->check == KW_HEALTH_ATF) {
        double temperature;
        const SensorValue *selector = field(f, 1, 1);
        if (!number(f, 5, 1, "°C", &temperature) || !number(f, 2, 3, "V", &volts) || !selector
            || selector->sensor_id != 0x8E || selector->a < 0 || selector->a >= 32 || selector->b < 32 || selector->b > 126) {
            result(s, r, KW_HEALTH_INCONCLUSIVE, "ATF, voltage or selector fields do not match the verified 01P layout.");
            return;
        }
        snprintf(r->readings, sizeof(r->readings), "ATF %.1f C / %.1f F | Supply %.2f V | Selector %c\n02: 005/1, 002/3, 001/1",
                 temperature, temperature * 1.8 + 32, volts, selector->b);
        if (temperature <= 30 && temperature >= -40) s->cold_start_seen = 1;
        if (temperature < -40 || temperature > 160) {
            result(s, r, KW_HEALTH_INCONCLUSIVE, "ATF reading is implausible; verify the sensor."); return;
        }
        if (f->fault_err || f->fault_count) {
            result(s, r, KW_HEALTH_INCONCLUSIVE, "Transmission faults are present or could not be read. Diagnose them and verify emergency mode before proceeding."); return;
        }
        if (volts < 11.5 || selector->b != 'P') {
            result(s, r, KW_HEALTH_WAITING, "Select P and restore supply voltage to at least 11.5 V."); return;
        }
        if ((confirmations & p->required_confirmations) != p->required_confirmations) {
            result(s, r, KW_HEALTH_WAITING, "Confirm the physical prerequisites below."); return;
        }
        if (!s->cold_start_seen) {
            result(s, r, KW_HEALTH_WAITING, "Let ATF cool to approximately 30 C or below, then warm it into the inspection window."); return;
        }
        if (temperature > 45) {
            s->cold_start_seen = 0;
            result(s, r, KW_HEALTH_WAITING, "ATF is above 45 C. The inspection window has closed; cool to 30 C or below and repeat."); return;
        }
        result(s, r, temperature < 35 ? KW_HEALTH_WAITING : KW_HEALTH_READY,
               temperature < 35 ? "Keep the engine idling in P. Waiting for ATF to reach 35 C."
                                : "35-45 C window reached. Perform the physical overflow inspection now. Fluid level has not been measured by the app.");
        return;
    }

    if (!number(f, 4, 1, "rpm", &rpm) || !number(f, 4, 2, "V", &volts)
        || !number(f, 4, 3, "°C", &coolant)) {
        result(s, r, KW_HEALTH_INCONCLUSIVE, "Engine prerequisites do not match the verified AXK group 004 layout."); return;
    }
    snprintf(r->readings, sizeof(r->readings), "Engine %.0f rpm | Supply %.2f V | Coolant %.1f C / %.1f F\n01: 004/1, 004/2, 004/3",
             rpm, volts, coolant, coolant * 1.8 + 32);
    if ((confirmations & p->required_confirmations) != p->required_confirmations) {
        result(s, r, KW_HEALTH_WAITING, "Confirm the physical prerequisites below."); return;
    }
    if (rpm <= 0) {
        result(s, r, KW_HEALTH_WAITING, "Start the engine to perform this check."); return;
    }
    if (s->check == KW_HEALTH_SUPPLY) {
        if (rpm < 650 || rpm > 1400) {
            result(s, r, KW_HEALTH_WAITING, "Keep the engine at idle (650-1400 rpm sampling gate) for the supply check."); return;
        }
        if (volts < 11.5) {
            reset_window(s);
            result(s, r, KW_HEALTH_ATTENTION, "ECU supply is below the factory minimum of 11.5 V. Check supply wiring, grounds and the charging system."); return;
        }
        observe(s, f, r);
        r->status = s->frames >= 3 && r->observed_s >= 10 ? KW_HEALTH_WITHIN_LIMITS : KW_HEALTH_WAITING;
        snprintf(r->message, sizeof(r->message), "%s", r->status == KW_HEALTH_WITHIN_LIMITS
            ? "ECU supply met the 11.5 V minimum during the observation window."
            : "Observing ECU supply: need 10 seconds and at least three complete reads.");
        return;
    }
    if (s->check == KW_HEALTH_TIMING) {
        double intake, exhaust, phase, intake_rpm, exhaust_rpm;
        const SensorValue *status = field(f, 208, 4);
        if (!number(f, 208, 1, "rpm", &intake_rpm) || !number(f, 209, 1, "rpm", &exhaust_rpm)
            || !number(f, 208, 2, "", &phase) || !number(f, 208, 3, "°", &intake)
            || !number(f, 209, 3, "°", &exhaust) || !status || status->sensor_id != 16 || status->a != 0) {
            result(s, r, KW_HEALTH_INCONCLUSIVE, "Camshaft fields or adaptation status do not match the AXK layout."); return;
        }
        snprintf(r->readings, sizeof(r->readings), "Intake %+.1f deg | Exhaust %+.1f deg | Difference %.1f deg\nIntake phase %.0f | Status %02X | RPM %.0f / %.0f / %.0f\nCoolant %.1f C (oil temperature requires confirmation)\n01: 208/3, 209/3; intake limits only",
                 intake, exhaust, fabs(intake - exhaust), phase, status->b, rpm, intake_rpm, exhaust_rpm, coolant);
        if (f->fault_err || f->fault_count) {
            result(s, r, KW_HEALTH_INCONCLUSIVE, "Engine faults are present or could not be read. Diagnose them before interpreting timing. Offsets are observations only; faults have not been cleared."); return;
        }
        if (rpm < 650 || rpm > 1400 || intake_rpm < 650 || intake_rpm > 1400 || exhaust_rpm < 650 || exhaust_rpm > 1400) {
            result(s, r, KW_HEALTH_WAITING, "Keep the engine at stable idle, within the AXK table's 650-1400 rpm range."); return;
        }
        if ((status->b & 0x27) != 0x24) {
            result(s, r, KW_HEALTH_INCONCLUSIVE, "Intake adaptation status does not satisfy AXK XX1XX100. An unlearned offset cannot establish timing condition."); return;
        }
        if (intake < -8 || intake > 8 || phase < 118 || phase > 134) {
            reset_window(s);
            result(s, r, KW_HEALTH_ATTENTION, "Intake offset or phase flank is outside the AXK specification. Check mechanical timing, adaptation, adjusters and oil pressure; this alone does not prove chain elongation."); return;
        }
        if (!s->frames) {
            s->intake_min = s->intake_max = intake;
            s->exhaust_min = s->exhaust_max = exhaust;
        }
        s->intake_min = fmin(s->intake_min, intake); s->intake_max = fmax(s->intake_max, intake);
        s->exhaust_min = fmin(s->exhaust_min, exhaust); s->exhaust_max = fmax(s->exhaust_max, exhaust);
        if (s->intake_max - s->intake_min > 1 || s->exhaust_max - s->exhaust_min > 1) {
            result(s, r, KW_HEALTH_INCONCLUSIVE, "Offsets varied by more than 1 degree. Repeat at stable idle; this is a sampling-quality gate, not a factory wear limit."); return;
        }
        observe(s, f, r);
        r->status = s->frames >= 3 && r->observed_s >= 10 ? KW_HEALTH_WITHIN_LIMITS : KW_HEALTH_WAITING;
        snprintf(r->message, sizeof(r->message), "%s", r->status == KW_HEALTH_WITHIN_LIMITS
            ? "Intake timing met the documented AXK limits. Exhaust offset and difference are observations only. Chain and guide condition remain unconfirmed."
            : "Collecting stable offsets: need 10 seconds and at least three complete reads.");
        return;
    }

    /* No pass is inferred from zero counters: the recognition status encoding
     * has not been verified on the owner's controller. */
    double counts[6];
    for (int i = 0; i < 6; i++) {
        if (!number(f, i < 3 ? 15 : 16, i % 3 + 1, "", &counts[i]) || counts[i] < 0 || floor(counts[i]) != counts[i]) {
            result(s, r, KW_HEALTH_INCONCLUSIVE, "Cylinder counters do not match groups 015 and 016."); return;
        }
    }
    int fresh = !s->previous_valid || f->finished_s != s->last_s, reset = 0;
    if (fresh && s->previous_valid) {
        for (int i = 0; i < 6; i++) reset |= counts[i] < s->previous_counts[i];
        if (reset) reset_window(s);
        else for (int i = 0; i < 6; i++) s->misfire_seen |= counts[i] > s->previous_counts[i];
    }
    if (fresh) memcpy(s->previous_counts, counts, sizeof(counts));
    const SensorValue *recognition = field(f, 14, 4);
    char recognition_text[40] = "unavailable";
    if (recognition) kw_format_sensor(recognition, recognition_text, sizeof(recognition_text));
    observe(s, f, r);
    s->previous_valid = 1;
    snprintf(r->readings, sizeof(r->readings), "Cyl 1-6: %.0f / %.0f / %.0f / %.0f / %.0f / %.0f\nRecognition 014/4 (raw): %s\nCoolant %.1f C | Engine %.0f rpm | Stored engine faults %d\n01: 015/1-3, 016/1-3",
             counts[0], counts[1], counts[2], counts[3], counts[4], counts[5], recognition_text, coolant, rpm, f->fault_count);
    r->status = s->misfire_seen ? KW_HEALTH_ATTENTION : KW_HEALTH_INCONCLUSIVE;
    snprintf(r->message, sizeof(r->message), "%s", s->misfire_seen
        ? "Cylinder counters increased during this run. Misfire activity was observed; diagnose the cause using faults and further tests."
        : reset ? "Counters reset or wrapped. A new baseline has been taken; keep observing through warm-up."
                : "No counter increase observed yet. This is not a pass: recognition may be disabled or the symptom intermittent. Keep observing through warm-up.");
}

void kw_health_run(KwSession *session, KwIo *io, int check, KwHealthCallback callback, void *ctx)
{
    const KwHealthProcedure *p = kw_health_procedure(check);
    if (!p || !callback || callback(ctx, NULL)) return;
    KwHealthFrame f;
    memset(&f, 0, sizeof(f));
    f.check = check;
    f.err = kw_connect_attempts(session, io, p->address, 1);
    if (!f.err) snprintf(f.ident, sizeof(f.ident), "%s", session->ident);
    if (f.err || !kw_health_matches(check, f.ident)) {
        callback(ctx, &f);
        kw_end(session);
        return;
    }
    for (;;) {
        if (callback(ctx, NULL)) break;
        f.started_s = io->now_us(io->ctx) / 1e6;
        f.finished_s = f.started_s;
        f.err = 0;
        f.group_count = 0;
        memset(f.groups, 0, sizeof(f.groups));
        int n = kw_read_fault_codes(session, f.faults, 64);
        f.fault_err = n < 0 ? n : 0;
        f.fault_count = n < 0 ? 0 : n;
        if (n < 0) f.err = n;
        for (int i = 0; !f.err && i < p->group_count; i++) {
            if (callback(ctx, NULL)) { kw_end(session); return; }
            KwHealthGroup *g = &f.groups[f.group_count++];
            g->group = p->groups[i];
            SensorValue values[16];
            n = kw_group_read(session, g->group, values, 16);
            g->err = n < 0 ? n : 0;
            g->count = n < 0 ? 0 : n;
            g->time_s = io->now_us(io->ctx) / 1e6;
            if (n > 0) memcpy(g->values, values, sizeof(SensorValue) * (n > 4 ? 4 : n));
            if (n < 0) f.err = n;
        }
        f.finished_s = io->now_us(io->ctx) / 1e6;
        /* A NAK is represented on its group so the evaluator can distinguish
         * an unsupported group from transport failure. */
        int ended = f.err;
        if (f.err == KW_ENAK) f.err = 0;
        if (callback(ctx, &f) || ended) break;
        io->sleep_us(io->ctx, 150000);
    }
    kw_end(session);
}
