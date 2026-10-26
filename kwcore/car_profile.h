#ifndef CAR_PROFILE_H
#define CAR_PROFILE_H

/*
 * Recognises the owner's cars from the controller idents a scan collects.
 * VW part numbers start with a model code (7D = T4/EuroVan, 1H = Golf/Jetta
 * Mk3), so each ident votes; engine ECU part numbers add a weaker vote.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    KW_CAR_UNKNOWN = 0,
    KW_CAR_EUROVAN,
    KW_CAR_HARLEQUIN,
    KW_CAR_COUNT
} KwCar;

typedef struct {
    KwCar car;
    const char *key;     /* asset name: hero_<key> */
    const char *name;    /* "EuroVan" */
    const char *detail;  /* "2002 · Silver" */
} KwCarProfile;

typedef struct {
    int score[KW_CAR_COUNT];
} KwCarVotes;

const KwCarProfile *kw_car_profile(KwCar car);

/* Adds one controller's ident text to the votes. */
void kw_car_vote(KwCarVotes *votes, const char *ident);

/* The best-supported car, or KW_CAR_UNKNOWN if nothing matched. */
KwCar kw_car_best(const KwCarVotes *votes);

#ifdef __cplusplus
}
#endif

#endif
