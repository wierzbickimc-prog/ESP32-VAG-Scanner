#ifndef LABELS_H
#define LABELS_H

/*
 * Names for measuring-block fields. KW1281 sends only (formula type, a, b),
 * so names come from a user-maintained CSV, falling back to a generic name
 * for the formula type ("Engine speed", "Temperature", ...).
 *
 * CSV: part,group,field,label   e.g.  7D0907379F,1,1,Wheel speed front left
 * `part` matches the start of the controller's ident; '#' lines are comments.
 * part "@petrol" holds VW's standard petrol-engine groups, used for engine
 * ECUs (part number xxx906...) that have no entry of their own.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Parses `csv` (copied). Replaces any earlier table. Returns entries loaded. */
int kw_labels_load(const char *csv, size_t len);

/* Label for field `field` (1-based) of `group` on the controller with `ident`, or NULL. */
const char *kw_label_find(const char *ident, int group, int field);

/* True when `group`'s names come from VW's standard petrol-engine set rather
 * than an entry for this exact controller (so they are typical, not certain). */
int kw_labels_standard(const char *ident, int group);

/* Generic name for a measuring-block formula type; never NULL. */
const char *kw_sensor_kind(int sensor_id);

/* kw_label_find, else kw_sensor_kind. */
const char *kw_label(const char *ident, int group, int field, int sensor_id);

#ifdef __cplusplus
}
#endif

#endif
