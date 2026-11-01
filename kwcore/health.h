#ifndef KW_HEALTH_H
#define KW_HEALTH_H

#include "kw1281.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { KW_HEALTH_ATF, KW_HEALTH_TIMING, KW_HEALTH_MISFIRE,
               KW_HEALTH_SUPPLY, KW_HEALTH_COUNT } KwHealthCheck;
typedef enum { KW_HEALTH_WAITING, KW_HEALTH_READY, KW_HEALTH_WITHIN_LIMITS,
               KW_HEALTH_ATTENTION, KW_HEALTH_INCONCLUSIVE, KW_HEALTH_UNSUPPORTED } KwHealthStatus;

/* Confirmations are per run, never persisted. They are not sensor readings. */
#define KW_HEALTH_CONFIRM_MAX 4
typedef struct {
    const char *title;
    const char *description;
    const char *preparation;
    const char *confirmation[KW_HEALTH_CONFIRM_MAX];
    const char *source_url;
    const char *source_title;
    const char *limitations;
    uint8_t address;
    uint8_t groups[4];
    int group_count;
    unsigned required_confirmations;
} KwHealthProcedure;

const KwHealthProcedure *kw_health_procedure(int check);
const char *kw_health_status_name(int status);
int kw_health_matches(int check, const char *ident);

typedef struct {
    int group, err, count;
    double time_s;
    SensorValue values[4];
} KwHealthGroup;

typedef struct {
    int check, err;
    char ident[KW_IDENT_MAX];
    double started_s, finished_s;
    int fault_err, fault_count;
    FaultCode faults[64];
    KwHealthGroup groups[4];
    int group_count;
} KwHealthFrame;

typedef struct {
    int status;
    char message[384];
    char readings[512];
    double observed_s;
} KwHealthResult;

typedef struct {
    int check, frames, cold_start_seen, previous_valid, misfire_seen;
    double first_s, last_s;
    unsigned confirmations;
    double previous_counts[6];
    double intake_min, intake_max, exhaust_min, exhaust_max;
} KwHealthState;

void kw_health_init(KwHealthState *state, int check);
/* now_s uses the same monotonic clock as the frame. Re-evaluating one frame
 * does not accumulate samples. Invalid/stale data breaks the observation window. */
void kw_health_evaluate(KwHealthState *state, const KwHealthFrame *frame,
                        unsigned confirmations, double now_s, KwHealthResult *result);

/* Read-only, one controller/session. Unsupported identity is rejected BEFORE
 * any group is read. callback(NULL) polls cancellation between group reads.
 * A nonzero return stops; errors and unsupported groups end the run. */
typedef int (*KwHealthCallback)(void *ctx, const KwHealthFrame *frame);
void kw_health_run(KwSession *session, KwIo *io, int check,
                   KwHealthCallback callback, void *ctx);

#ifdef __cplusplus
}
#endif
#endif
