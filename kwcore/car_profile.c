#include "car_profile.h"

#include <string.h>

static const KwCarProfile PROFILES[KW_CAR_COUNT] = {
    { KW_CAR_UNKNOWN, "family", "Volkswagen", "Golf Mk1 \xE2\x80\x93 Mk8" },
    { KW_CAR_EUROVAN, "eurovan", "EuroVan", "2002 \xC2\xB7 Silver" },
    { KW_CAR_HARLEQUIN, "harlequin", "Golf Harlequin", "1996 \xC2\xB7 Mk3 \xC2\xB7 Ginster Yellow base" },
};

typedef struct {
    KwCar car;
    const char *prefix;
    int weight;
} Rule;

static const Rule RULES[] = {
    /* chassis codes at the start of any part number */
    { KW_CAR_EUROVAN, "7D", 3 },
    { KW_CAR_EUROVAN, "70", 3 },
    { KW_CAR_HARLEQUIN, "1H", 3 },
    { KW_CAR_HARLEQUIN, "1E", 3 },
    /* engine ECUs shared with other models: weaker */
    { KW_CAR_EUROVAN, "022906", 1 },
    { KW_CAR_EUROVAN, "021906", 1 },
    { KW_CAR_HARLEQUIN, "037906", 1 },
};

const KwCarProfile *kw_car_profile(KwCar car)
{
    if (car < 0 || car >= KW_CAR_COUNT)
        car = KW_CAR_UNKNOWN;
    return &PROFILES[car];
}

void kw_car_vote(KwCarVotes *votes, const char *ident)
{
    /* each line of the ident can carry its own part number */
    const char *line = ident;
    while (line && *line) {
        while (*line == ' ' || *line == '\n')
            line++;
        for (size_t i = 0; i < sizeof(RULES) / sizeof(RULES[0]); i++) {
            size_t n = strlen(RULES[i].prefix);
            if (strncmp(line, RULES[i].prefix, n) == 0) {
                votes->score[RULES[i].car] += RULES[i].weight;
                break;
            }
        }
        line = strchr(line, '\n');
    }
}

KwCar kw_car_best(const KwCarVotes *votes)
{
    KwCar best = KW_CAR_UNKNOWN;
    int best_score = 0;
    for (int c = 1; c < KW_CAR_COUNT; c++) {
        if (votes->score[c] > best_score) {
            best_score = votes->score[c];
            best = (KwCar)c;
        }
    }
    return best;
}
