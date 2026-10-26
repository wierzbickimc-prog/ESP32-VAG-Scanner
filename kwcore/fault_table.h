#ifndef FAULT_TABLE_H
#define FAULT_TABLE_H

/*
 * Optional fault code descriptions, loaded from a user-supplied CSV with a
 * header row and two columns: code, meaning. The code may be a 5-digit VAG
 * code, "16497/P0113/..." (first part used) or a numeric P-code (converted);
 * quoted fields with commas are handled.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Parses `csv` (copied; need not be NUL-terminated). Replaces any earlier table. Returns entries loaded. */
int kw_fault_table_load(const char *csv, size_t len);

/* Description for `dtc`, or NULL if the table doesn't have it. */
const char *kw_fault_describe(int dtc);

/* OBD-II P-code for a VAG code from 16384 up (16497 -> "P0113"). Returns 0 if there is none. */
int kw_fault_pcode(int dtc, char *out, int out_len);

/*
 * Fault type for a KW1281 status byte (bits 0-6; bit 7 is the intermittent
 * flag and is ignored), e.g. 0x9D -> "Short to Ground". Compiled in from
 * fault_types.h if assets/tools/fetch_fault_types.py has been run. Returns NULL
 * when the list has no text for it ("-", "No Elaboration Available") or is absent.
 */
const char *kw_fault_type(int status);

/* Number of entries currently loaded. */
int kw_fault_table_size(void);

#ifdef __cplusplus
}
#endif

#endif
